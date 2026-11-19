/*
 * SPDX-FileCopyrightText: 2026 The PenguinOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

//! Screen-off single tap sensor. The touch firmware reports a single tap on the dark screen as
//! scan code KEY_GESTURE_START + SINGLE_TAP on the touchpanel input device once that gesture is
//! enabled (the shim enables it while the sensor is active). One-shot, like the UDFPS sensor.

use log::{error, info};
use std::ffi::c_void;
use std::fs::{self, File, OpenOptions};
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::os::unix::fs::OpenOptionsExt;
use std::sync::{Condvar, Mutex, OnceLock};
use std::thread;

const INPUT_DIR: &str = "/dev/input";
const TOUCH_DEVICE_NAME: &str = "touchpanel";
const EV_KEY: u16 = 0x01;
/// KEY_GESTURE_START (246) + SINGLE_TAP (16) in the oplus touch drivers.
const SINGLE_TAP_CODE: u16 = 262;
/// struct input_event on 64-bit: timeval (16), type (2), code (2), value (4).
const INPUT_EVENT_SIZE: usize = 24;

type EventCallback = extern "C" fn(context: *mut c_void);

struct Callback {
    function: EventCallback,
    context: *mut c_void,
}

// SAFETY: the context is the sub-HAL object, which outlives the sensor thread.
unsafe impl Send for Callback {}

#[derive(Default)]
struct State {
    enabled: bool,
    injection: bool,
}

struct TapSensor {
    state: Mutex<State>,
    changed: Condvar,
    wake: OwnedFd,
}

static SENSOR: OnceLock<TapSensor> = OnceLock::new();

/// True for a key press of the single tap gesture in one input_event.
fn is_single_tap(event: &[u8]) -> bool {
    if event.len() < INPUT_EVENT_SIZE {
        return false;
    }
    let kind = u16::from_ne_bytes([event[16], event[17]]);
    let code = u16::from_ne_bytes([event[18], event[19]]);
    let value = i32::from_ne_bytes([event[20], event[21], event[22], event[23]]);
    kind == EV_KEY && code == SINGLE_TAP_CODE && value == 1
}

/// EVIOCGNAME(len): _IOC(_IOC_READ, 'E', 0x06, len).
fn eviocgname(len: usize) -> libc::c_ulong {
    (2 << 30) | ((len as libc::c_ulong) << 16) | ((b'E' as libc::c_ulong) << 8) | 0x06
}

fn device_name(file: &File) -> Option<String> {
    let mut name = [0u8; 128];
    // SAFETY: EVIOCGNAME writes at most `name.len()` bytes into the buffer.
    let len = unsafe { libc::ioctl(file.as_raw_fd(), eviocgname(name.len()) as _, name.as_mut_ptr()) };
    if len <= 0 {
        return None;
    }
    let end = name.iter().position(|&b| b == 0).unwrap_or(len as usize);
    Some(String::from_utf8_lossy(&name[..end]).into_owned())
}

/// The touchpanel event device.
fn open_touch_device() -> Option<File> {
    for entry in fs::read_dir(INPUT_DIR).ok()?.flatten() {
        if !entry.file_name().to_string_lossy().starts_with("event") {
            continue;
        }
        let Ok(file) = OpenOptions::new()
            .read(true)
            .custom_flags(libc::O_CLOEXEC | libc::O_NONBLOCK)
            .open(entry.path())
        else {
            continue;
        };
        if device_name(&file).as_deref() == Some(TOUCH_DEVICE_NAME) {
            return Some(file);
        }
    }
    None
}

fn pipe() -> Option<(OwnedFd, OwnedFd)> {
    let mut fds = [0; 2];
    // SAFETY: `fds` has room for the two descriptors pipe2 returns.
    if unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_CLOEXEC | libc::O_NONBLOCK) } != 0 {
        return None;
    }
    // SAFETY: pipe2 succeeded, so both descriptors are open and owned by nothing else.
    Some(unsafe { (OwnedFd::from_raw_fd(fds[0]), OwnedFd::from_raw_fd(fds[1])) })
}

fn drain(fd: libc::c_int, buf: &mut [u8]) {
    // SAFETY: reads into `buf`, which is valid for `buf.len()` bytes.
    while unsafe { libc::read(fd, buf.as_mut_ptr() as *mut c_void, buf.len()) } > 0 {}
}

impl TapSensor {
    fn update(&self, change: impl FnOnce(&mut State)) {
        change(&mut self.state.lock().unwrap());
        self.changed.notify_all();
        // SAFETY: writes one byte from a static buffer to the non-blocking wake pipe.
        unsafe { libc::write(self.wake.as_raw_fd(), b"1".as_ptr() as *const c_void, 1) };
    }

    fn armed(&self) -> bool {
        let state = self.state.lock().unwrap();
        state.enabled && !state.injection
    }

    fn wait_until_armed(&self) {
        let state = self.state.lock().unwrap();
        drop(self.changed.wait_while(state, |s| !s.enabled || s.injection).unwrap());
    }

    /// One-shot: the first tap after activation disarms the sensor.
    fn fire(&self) -> bool {
        let mut state = self.state.lock().unwrap();
        let armed = state.enabled && !state.injection;
        state.enabled = false;
        armed
    }

    fn run(&self, touch: File, wake_read: OwnedFd, callback: Callback) {
        let mut buf = [0u8; INPUT_EVENT_SIZE * 64];
        loop {
            self.wait_until_armed();
            // Taps reported before the sensor was armed are stale.
            drain(touch.as_raw_fd(), &mut buf);
            while self.armed() {
                let mut polls = [
                    libc::pollfd { fd: wake_read.as_raw_fd(), events: libc::POLLIN, revents: 0 },
                    libc::pollfd { fd: touch.as_raw_fd(), events: libc::POLLIN, revents: 0 },
                ];
                // SAFETY: `polls` holds two valid pollfd entries.
                if unsafe { libc::poll(polls.as_mut_ptr(), 2, -1) } < 0 {
                    let e = std::io::Error::last_os_error();
                    if e.kind() == std::io::ErrorKind::Interrupted {
                        continue;
                    }
                    error!("failed to poll the touchpanel: {e}");
                    return;
                }
                if polls[0].revents & libc::POLLIN != 0 {
                    drain(wake_read.as_raw_fd(), &mut buf);
                }
                if polls[1].revents & libc::POLLIN == 0 {
                    continue;
                }
                // SAFETY: reads into `buf`, which is valid for `buf.len()` bytes.
                let n = unsafe {
                    libc::read(touch.as_raw_fd(), buf.as_mut_ptr() as *mut c_void, buf.len())
                };
                if n <= 0 {
                    continue;
                }
                let tapped =
                    buf[..n as usize].chunks_exact(INPUT_EVENT_SIZE).any(is_single_tap);
                if tapped && self.fire() {
                    info!("single tap");
                    (callback.function)(callback.context);
                }
            }
        }
    }
}

/// Whether this device reports screen-off single taps; the sub-HAL lists the sensor only then.
#[no_mangle]
pub extern "C" fn tap_sensor_available() -> bool {
    open_touch_device().is_some()
}

#[no_mangle]
pub extern "C" fn tap_sensor_start(function: EventCallback, context: *mut c_void) -> bool {
    if SENSOR.get().is_some() {
        return true;
    }
    let Some(touch) = open_touch_device() else {
        error!("no {TOUCH_DEVICE_NAME} input device");
        return false;
    };
    let Some((wake_read, wake)) = pipe() else {
        error!("failed to open the tap wait pipe");
        return false;
    };
    let sensor =
        SENSOR.get_or_init(|| TapSensor { state: Mutex::default(), changed: Condvar::new(), wake });
    let callback = Callback { function, context };
    thread::Builder::new()
        .name("tap-sensor".to_owned())
        .spawn(move || sensor.run(touch, wake_read, callback))
        .is_ok_and(|_| {
            info!("single tap sensor ready");
            true
        })
}

#[no_mangle]
pub extern "C" fn tap_sensor_activate(enabled: bool) {
    if let Some(sensor) = SENSOR.get() {
        sensor.update(|s| s.enabled = enabled);
    }
}

#[no_mangle]
pub extern "C" fn tap_sensor_set_injection_mode(injection: bool) {
    if let Some(sensor) = SENSOR.get() {
        sensor.update(|s| s.injection = injection);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn event(kind: u16, code: u16, value: i32) -> Vec<u8> {
        let mut e = vec![0u8; 16];
        e.extend_from_slice(&kind.to_ne_bytes());
        e.extend_from_slice(&code.to_ne_bytes());
        e.extend_from_slice(&value.to_ne_bytes());
        e
    }

    #[test]
    fn only_the_tap_key_press_counts() {
        assert!(is_single_tap(&event(EV_KEY, SINGLE_TAP_CODE, 1)));
        assert!(!is_single_tap(&event(EV_KEY, SINGLE_TAP_CODE, 0)));
        // Double tap wakes through KEY_WAKEUP, not through this sensor.
        assert!(!is_single_tap(&event(EV_KEY, 143, 1)));
        assert!(!is_single_tap(&event(0x03, SINGLE_TAP_CODE, 1)));
        assert!(!is_single_tap(&event(EV_KEY, SINGLE_TAP_CODE, 1)[..20]));
    }

    #[test]
    fn evdev_name_request() {
        assert_eq!(eviocgname(128), 0x8080_4506);
    }
}
