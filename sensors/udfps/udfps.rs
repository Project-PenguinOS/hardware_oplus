/*
 * SPDX-FileCopyrightText: 2022-2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

//! UDFPS one-shot sensor: reports the touch position when the display driver flags a finger
//! on the sensor area through /sys/kernel/oplus_display/fp_state. The sub-HAL shim (C++) owns
//! the HIDL side and receives events through `udfps_sensor_start`'s callback.

use log::{error, info};

mod tap;
use std::ffi::c_void;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::sync::{Condvar, Mutex, OnceLock};
use std::thread;

const FP_STATE: &str = "/sys/kernel/oplus_display/fp_state";

/// `x,y,state` as the display driver writes it; a finger is down when state > 0.
fn parse_fp_state(text: &str) -> Option<(i32, i32, bool)> {
    let mut fields = text.trim().split(',').map(|f| f.trim().parse::<i32>());
    let x = fields.next()?.ok()?;
    let y = fields.next()?.ok()?;
    let state = fields.next()?.ok()?;
    Some((x, y, state > 0))
}

type EventCallback = extern "C" fn(context: *mut c_void, x: i32, y: i32);

struct Callback {
    function: EventCallback,
    context: *mut c_void,
}

// SAFETY: the shim's context is its static sub-HAL object, which outlives the process and
// is safe to call from any thread.
unsafe impl Send for Callback {}

#[derive(Default)]
struct State {
    enabled: bool,
    injection: bool,
}

struct Sensor {
    state: Mutex<State>,
    changed: Condvar,
    /// Write end of the pipe that wakes the poll when the state changes.
    wake: OwnedFd,
}

static SENSOR: OnceLock<Sensor> = OnceLock::new();

fn pipe() -> Option<(OwnedFd, OwnedFd)> {
    let mut fds = [0; 2];
    // SAFETY: `fds` is a valid out array for pipe2.
    if unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_CLOEXEC | libc::O_NONBLOCK) } != 0 {
        return None;
    }
    // SAFETY: both fds were just created and are owned here.
    Some(unsafe { (OwnedFd::from_raw_fd(fds[0]), OwnedFd::from_raw_fd(fds[1])) })
}

impl Sensor {
    fn interrupt(&self) {
        // SAFETY: writes one byte from a valid buffer; a full pipe already wakes the poll.
        unsafe { libc::write(self.wake.as_raw_fd(), b"1".as_ptr() as *const c_void, 1) };
    }

    fn update(&self, change: impl FnOnce(&mut State)) {
        change(&mut self.state.lock().unwrap());
        self.changed.notify_all();
        self.interrupt();
    }

    fn wait_until_armed(&self) {
        let state = self.state.lock().unwrap();
        drop(self.changed.wait_while(state, |s| !s.enabled || s.injection).unwrap());
    }

    fn armed(&self) -> bool {
        let state = self.state.lock().unwrap();
        state.enabled && !state.injection
    }

    /// Disarms the one-shot sensor if it is still armed; true when this call did it.
    fn fire(&self) -> bool {
        let mut state = self.state.lock().unwrap();
        let armed = state.enabled && !state.injection;
        state.enabled = false;
        armed
    }

    fn run(&self, mut fp_state: File, wake_read: OwnedFd, callback: Callback) {
        let mut buf = [0u8; 512];
        loop {
            self.wait_until_armed();
            // The first notification after arming is the initial sysfs state, not a press.
            let mut report = false;
            while self.armed() {
                let mut polls = [
                    libc::pollfd { fd: wake_read.as_raw_fd(), events: libc::POLLIN, revents: 0 },
                    libc::pollfd {
                        fd: fp_state.as_raw_fd(),
                        events: libc::POLLERR | libc::POLLPRI,
                        revents: 0,
                    },
                ];
                // SAFETY: `polls` is a valid array of two pollfds.
                if unsafe { libc::poll(polls.as_mut_ptr(), 2, -1) } < 0 {
                    let e = std::io::Error::last_os_error();
                    if e.kind() == std::io::ErrorKind::Interrupted {
                        continue;
                    }
                    error!("failed to poll: {e}");
                    return;
                }
                if polls[0].revents & libc::POLLIN != 0 {
                    // SAFETY: drains into a valid buffer; the fd is non-blocking.
                    while unsafe { libc::read(wake_read.as_raw_fd(), buf.as_mut_ptr() as *mut c_void, buf.len()) } > 0 {}
                }
                if polls[1].revents & (libc::POLLERR | libc::POLLPRI) != 0 {
                    let mut text = String::new();
                    let pressed = fp_state
                        .seek(SeekFrom::Start(0))
                        .and_then(|_| fp_state.read_to_string(&mut text))
                        .ok()
                        .and_then(|_| parse_fp_state(&text));
                    match pressed {
                        Some((x, y, true)) if report && self.fire() => {
                            (callback.function)(callback.context, x, y)
                        }
                        Some(_) => {}
                        None => error!("failed to read fp state: {text:?}"),
                    }
                }
                report = true;
            }
        }
    }
}

/// Starts the sensor thread. Returns false when the fp_state node is missing.
#[no_mangle]
pub extern "C" fn udfps_sensor_start(function: EventCallback, context: *mut c_void) -> bool {
    logger::init(logger::Config::default().with_tag_on_device("sensors.oplus"));
    if SENSOR.get().is_some() {
        return true;
    }
    let fp_state = match File::open(FP_STATE) {
        Ok(file) => file,
        Err(e) => {
            error!("failed to open {FP_STATE}: {e}");
            return false;
        }
    };
    let Some((wake_read, wake)) = pipe() else {
        error!("failed to open wait pipe");
        return false;
    };
    let sensor = SENSOR.get_or_init(|| Sensor { state: Mutex::default(), changed: Condvar::new(), wake });
    let callback = Callback { function, context };
    thread::Builder::new()
        .name("udfps-sensor".to_owned())
        .spawn(move || sensor.run(fp_state, wake_read, callback))
        .is_ok_and(|_| {
            info!("UDFPS sensor ready");
            true
        })
}

#[no_mangle]
pub extern "C" fn udfps_sensor_activate(enabled: bool) {
    if let Some(sensor) = SENSOR.get() {
        sensor.update(|s| s.enabled = enabled);
    }
}

/// Data injection mode pauses the sensor, as for the other sub-HAL sensors.
#[no_mangle]
pub extern "C" fn udfps_sensor_set_injection_mode(injection: bool) {
    if let Some(sensor) = SENSOR.get() {
        sensor.update(|s| s.injection = injection);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_driver_state() {
        assert_eq!(parse_fp_state("540,2100,1\n"), Some((540, 2100, true)));
        assert_eq!(parse_fp_state("540,2100,0"), Some((540, 2100, false)));
        assert_eq!(parse_fp_state(" 1, 2, 3 "), Some((1, 2, true)));
        assert_eq!(parse_fp_state("540,2100"), None);
        assert_eq!(parse_fp_state(""), None);
    }
}
