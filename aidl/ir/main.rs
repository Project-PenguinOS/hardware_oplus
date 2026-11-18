/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

//! Consumer IR HAL for the oplus IR blaster (/dev/oplus_consumer_ir).

// The test build has no caller for main() and the service type.
#![cfg_attr(test, allow(dead_code))]

use android_hardware_ir::aidl::android::hardware::ir::{
    ConsumerIrFreqRange::ConsumerIrFreqRange,
    IConsumerIr::{BnConsumerIr, IConsumerIr},
};
use binder::{BinderFeatures, ExceptionCode, Interface, Status};
use log::error;
use std::fs::OpenOptions;
use std::os::fd::AsRawFd;

const LOG_TAG: &str = "android.hardware.ir-service.oplus";
const DEVICE: &str = "/dev/oplus_consumer_ir";

const MAX_PATTERN_TIME: i64 = 2_000_000;
const MIN_FREQ: i32 = 20_000;
const MAX_FREQ: i32 = 60_000;

// include/oplus/oplus_ir_core.h: IR_SEND_PATTERN = _IO(0xE5, 0x02), argument is
// struct pattern_params { int32_t carrier_freq; uint32_t size; uint32_t pattern[]; }.
nix::ioctl_write_ptr_bad!(
    /// Sends a pattern_params buffer to the IR driver.
    ir_send_pattern,
    nix::request_code_none!(0xE5, 0x02),
    i32
);

fn unsupported() -> Status {
    Status::new_exception(ExceptionCode::UNSUPPORTED_OPERATION, None)
}

/// Builds the pattern_params buffer for IR_SEND_PATTERN. Every field is 4 bytes,
/// so the struct is laid out as consecutive 32-bit words with no padding.
fn pattern_params(carrier_freq_hz: i32, pattern: &[i32]) -> Result<Vec<i32>, String> {
    if !(MIN_FREQ..=MAX_FREQ).contains(&carrier_freq_hz) {
        return Err(format!("Invalid carrier frequency: {carrier_freq_hz}"));
    }
    if let Some(v) = pattern.iter().find(|&&v| v < 0) {
        return Err(format!("Invalid pattern value: {v}"));
    }
    let total: i64 = pattern.iter().map(|&v| i64::from(v)).sum();
    if total > MAX_PATTERN_TIME {
        return Err(format!("Pattern is too long: {total} us"));
    }

    let mut params = Vec::with_capacity(2 + pattern.len());
    params.push(carrier_freq_hz);
    params.push(pattern.len() as i32);
    params.extend_from_slice(pattern);
    Ok(params)
}

struct ConsumerIr;

impl Interface for ConsumerIr {}

#[allow(non_snake_case)]
impl IConsumerIr for ConsumerIr {
    fn getCarrierFreqs(&self) -> binder::Result<Vec<ConsumerIrFreqRange>> {
        Ok(vec![ConsumerIrFreqRange { minHz: MIN_FREQ, maxHz: MAX_FREQ }])
    }

    fn transmit(&self, carrierFreqHz: i32, pattern: &[i32]) -> binder::Result<()> {
        let params = pattern_params(carrierFreqHz, pattern).map_err(|e| {
            error!("{e}");
            unsupported()
        })?;

        let dev = OpenOptions::new().write(true).open(DEVICE).map_err(|e| {
            error!("Failed to open {DEVICE}: {e}");
            unsupported()
        })?;

        // SAFETY: `params` is a live, correctly laid out pattern_params buffer that
        // outlives the call, and the driver only reads from it.
        unsafe { ir_send_pattern(dev.as_raw_fd(), params.as_ptr()) }.map_err(|e| {
            error!("IR_SEND_PATTERN failed: {e}");
            unsupported()
        })?;
        Ok(())
    }
}

fn main() {
    logger::init(logger::Config::default().with_tag_on_device(LOG_TAG));

    binder::ProcessState::set_thread_pool_max_thread_count(0);

    let service = BnConsumerIr::new_binder(ConsumerIr, BinderFeatures::default());
    let instance = format!("{}/default", <ConsumerIr as IConsumerIr>::get_descriptor());
    binder::add_service(&instance, service.as_binder()).expect("Failed to register service");

    binder::ProcessState::join_thread_pool();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layout_matches_pattern_params() {
        assert_eq!(pattern_params(38_000, &[100, 200]).unwrap(), vec![38_000, 2, 100, 200]);
    }

    #[test]
    fn rejects_out_of_range_frequency() {
        assert!(pattern_params(MIN_FREQ - 1, &[1]).is_err());
        assert!(pattern_params(MAX_FREQ + 1, &[1]).is_err());
    }

    #[test]
    fn rejects_negative_values() {
        assert!(pattern_params(38_000, &[100, -1]).is_err());
    }

    #[test]
    fn rejects_long_pattern_without_overflow() {
        assert!(pattern_params(38_000, &[i32::MAX, i32::MAX]).is_err());
        assert!(pattern_params(38_000, &[1_000_000, 1_000_001]).is_err());
        assert!(pattern_params(38_000, &[1_000_000, 1_000_000]).is_ok());
    }
}
