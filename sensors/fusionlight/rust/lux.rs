/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

//! FusionLight lux: the stock high-PWM RGB light sensor model, with the screen's own light
//! leakage taken out using the colour of the screen above the sensor. Every table lookup is
//! checked; a value the profile does not cover gives None, and the caller reports raw lux.

use crate::config::{
    Channels, FusionConfig, LeakageModel, Polynomial, RatioModel, ValueRange, ViewCalibration, CHANNELS, IR_BANDS,
};

/// The screen colour above the sensor, 0-255 per channel.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ScreenColor {
    pub r: i32,
    pub g: i32,
    pub b: i32,
}

/// What a light event needs for the lux model.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Reading {
    pub raw_channels: Channels,
    pub brightness: i32,
    pub dc_mode: bool,
}

fn dot<const N: usize>(a: &[f64; N], b: &[f64; N]) -> f64 {
    a.iter().zip(b).map(|(x, y)| x * y).sum()
}

pub fn find_range(ranges: &[ValueRange], value: f64) -> Option<usize> {
    if let Some(level) = ranges.iter().position(|r| value >= r.min && value < r.max) {
        return Some(level);
    }
    // Some profile tables use adjacent inclusive integer ranges (for example, 1..419 followed by
    // 420..728). Recover their upper endpoint only when no half-open range matched.
    ranges.iter().position(|r| value >= r.min && value <= r.max)
}

/// The view model matches brightness above each lower bound, except for the first IR
/// brightness range. Unmatched brightness falls back to the first range.
pub fn find_view_range(ranges: &[ValueRange], value: f64, include_first_min: bool) -> usize {
    ranges
        .iter()
        .enumerate()
        .position(|(level, r)| {
            let above_min = value > r.min || (include_first_min && level == 0 && value == r.min);
            above_min && value <= r.max
        })
        .unwrap_or(0)
}

/// FusionLightBrightnesses lists the upper brightness of the default, medium and low
/// brightness segments.
pub fn find_view_segment(config: &FusionConfig, brightness: i32) -> usize {
    if config.segments.len() != IR_BANDS {
        return 0;
    }
    let (Some(&medium), Some(&low)) = (config.segment_brightness.get(1), config.segment_brightness.get(2)) else {
        return 0;
    };
    if brightness <= low {
        2
    } else if brightness <= medium {
        1
    } else {
        0
    }
}

/// V2.1 selects the first IR brightness range whose upper bound covers the brightness,
/// ignoring lower bounds. Unmatched brightness falls back to the first range.
pub fn find_upper_range(ranges: &[ValueRange], value: f64) -> usize {
    ranges.iter().position(|r| value <= r.max).unwrap_or(0)
}

/// Stock compares IR ratios and lux in fixed point with 1e-7 resolution.
fn to_fixed_point(value: f64) -> i64 {
    const LIMIT: f64 = 9.2e18;
    (value * 1e7).clamp(-LIMIT, LIMIT) as i64
}

/// The LIR band has no lower bound and the super HIR band no upper bound. Ratios outside every
/// band fall back to LIR.
pub fn find_ir_band(thresholds: &[ValueRange], ratio: f64) -> usize {
    let [lir, hir, super_hir] = thresholds else { return 0 };
    let value = to_fixed_point(ratio);
    if value <= to_fixed_point(lir.max) {
        0
    } else if value > to_fixed_point(hir.min) && value <= to_fixed_point(hir.max) {
        1
    } else if value > to_fixed_point(super_hir.min) {
        2
    } else {
        0
    }
}

pub fn find_channel_policy(config: &FusionConfig, channels: &Channels) -> Option<usize> {
    if !config.channel_count_policy_supported {
        return None;
    }
    config
        .channel_thresholds
        .iter()
        .position(|limits| limits.iter().zip(channels).all(|(&max, &value)| max == -1 || value < f64::from(max)))
}

pub fn find_c_zero_level(config: &FusionConfig, clear: f64) -> usize {
    config.c_zero_thresholds.iter().rposition(|&min| clear >= min).unwrap_or(0)
}

fn has_absolute_ir_ratio(sensor_module_id: i32) -> bool {
    [4, 5, 6, 8, 11].contains(&sensor_module_id)
}

pub fn ir_ratio(config: &FusionConfig, c: &Channels) -> f64 {
    let has_clear = to_fixed_point(c[3]) > 0;
    if config.ir_ratio_formula == 2 {
        if !has_clear {
            return 1.0;
        }
        return (1.0 - (c[0] * 0.4379 + c[1] * 0.243 + c[2] * 0.2435) / c[3]).max(0.0);
    }
    if !has_clear {
        return 0.0;
    }
    let mut ratio = (c[0] + c[1] + c[2] - c[3]) / c[3] * 0.5;
    // Stock folds negative ratios back for these sensor modules.
    if has_absolute_ir_ratio(config.sensor_module_id) {
        ratio = ratio.abs();
    }
    ratio.max(0.0)
}

/// Black-frame profiles, and V2.1 profiles without a leakage model, report lux from the raw
/// channels without screen captures.
pub fn uses_screen_capture(config: &FusionConfig) -> bool {
    !config.black_frame_supported && (!config.screenshot_v2_1_supported || !config.leakage.is_empty())
}

fn evaluate_polynomial(p: &Polynomial, x: f64) -> f64 {
    ((p[0] * x + p[1]) * x + p[2]) * x + p[3]
}

/// Lux from leakage-free channels.
pub fn calculate_lux(config: &FusionConfig, channels: &Channels, brightness: i32, screen_off: bool) -> Option<f64> {
    let band = find_ir_band(&config.ir_thresholds, ir_ratio(config, channels));
    let (table, level) = if screen_off {
        if !config.screen_off_lux_supported {
            return None;
        }
        (&config.screen_off_coefficients[band], find_c_zero_level(config, channels[3]))
    } else if let Some(level) = find_channel_policy(config, channels) {
        (&config.channel_count_coefficients[band], level)
    } else if config.screenshot_v2_1_supported {
        (&config.lux_coefficients[band], find_upper_range(&config.ir_brightness, f64::from(brightness)))
    } else {
        let segment = config.segments.get(find_view_segment(config, brightness))?;
        (&segment.lux_coefficients[band], find_view_range(&segment.ir_brightness, f64::from(brightness), true))
    };
    let mut lux = dot(channels, table.get(level)?);
    if !lux.is_finite() {
        return None;
    }
    if to_fixed_point(lux) < to_fixed_point(config.low_light_accuracy) {
        lux = 0.0;
    }
    Some(lux.max(0.0))
}

fn leakage_terms(l: f64, r: f64, g: f64, b: f64) -> LeakageModel {
    let (l2, r2, g2, b2) = (l * l, r * r, g * g, b * b);
    [
        1.0, l, r, g, b, l2, l * r, l * g, l * b, r2, r * g, r * b, g2, g * b, b2, l2 * l, l2 * r, l2 * g, l2 * b,
        l * r2, l * r * g, l * r * b, l * g2, l * g * b, l * b2, r2 * r, r2 * g, r2 * b, r * g2, r * g * b, r * b2,
        g2 * g, g2 * b, g * b2, b2 * b,
    ]
}

fn ratio_terms(r: f64, g: f64, b: f64) -> RatioModel {
    let (r2, g2, b2) = (r * r, g * g, b * b);
    [
        1.0, r, g, b, r2, r * g, r * b, g2, g * b, b2, r2 * r, r2 * g, r2 * b, r * g2, r * g * b, r * b2, g2 * g,
        g2 * b, g * b2, b2 * b,
    ]
}

/// V2.1: per channel leakage from the brightness linearity, the screen colour and the device
/// calibration.
pub fn corrected_lux(config: &FusionConfig, calibration: &[Channels], reading: &Reading, color: &ScreenColor) -> Option<f64> {
    let brightness = reading.brightness;
    let level = find_range(&config.linearity_brightness, f64::from(brightness))?;
    let calibration = calibration.get(level)?;
    let (linearity, leakage, leakage_ratio) =
        (config.linearity.get(level)?, config.leakage.get(level)?, config.leakage_ratio.get(level)?);
    let (r, g, b) = (f64::from(color.r), f64::from(color.g), f64::from(color.b));
    let ratio_basis = ratio_terms(r, g, b);
    let mut compensated = [0.0; CHANNELS];
    for channel in 0..CHANNELS {
        let linear = evaluate_polynomial(&linearity[channel], f64::from(brightness));
        let parameter = dot(&leakage[channel], &leakage_terms(linear, r, g, b));
        let ratio = dot(&leakage_ratio[channel], &ratio_basis);
        let light = (parameter * ratio * calibration[channel]).max(0.0);
        compensated[channel] = (reading.raw_channels[channel] - light).max(0.0);
    }
    calculate_lux(config, &compensated, brightness, false)
}

fn view_linearity(config: &FusionConfig, brightness: i32, dc_mode: bool) -> Option<Channels> {
    if brightness == 0 {
        return Some([0.0; CHANNELS]);
    }
    if dc_mode && !config.fod_linearity_segment_supported {
        return Some(config.fod_linearity_constant);
    }
    let functions = if dc_mode { &config.fod_linearity } else { &config.linearity };
    let mut level = find_view_range(&config.linearity_brightness, f64::from(brightness), false);
    if level >= functions.len() {
        level = 0;
    }
    let functions = functions.get(level)?;
    let mut linearity = [0.0; CHANNELS];
    for (value, function) in linearity.iter_mut().zip(functions) {
        *value = evaluate_polynomial(function, f64::from(brightness));
    }
    Some(linearity)
}

/// Pre-V2.1: leakage per R/G/B view, minus the overlap the white view accounts for.
pub fn view_lux(config: &FusionConfig, calibration: &[ViewCalibration], reading: &Reading, color: &ScreenColor) -> Option<f64> {
    let brightness = reading.brightness;
    let segment_index = find_view_segment(config, brightness);
    let calibration = calibration.get(segment_index)?;
    let segment = config.segments.get(segment_index)?;
    let screen = [f64::from(color.r), f64::from(color.g), f64::from(color.b)];
    let linearity = view_linearity(config, brightness, reading.dc_mode)?;
    let mut compensated = [0.0; CHANNELS];
    for channel in 0..CHANNELS {
        let mut light = 0.0;
        for view in 0..CHANNELS {
            let white = view == CHANNELS - 1;
            let x = if white { dot(&segment.grey_scale[channel], &screen) } else { screen[view] };
            let term = evaluate_polynomial(&segment.leakage[channel][view], x) * calibration[channel][view];
            light += if white { -term } else { term };
        }
        light = (light * linearity[channel]).max(0.0);
        compensated[channel] = (reading.raw_channels[channel] - light).max(0.0);
    }
    calculate_lux(config, &compensated, brightness, false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ranges() {
        let r = [ValueRange { min: 1.0, max: 419.0 }, ValueRange { min: 420.0, max: 728.0 }];
        assert_eq!(find_range(&r, 419.0), Some(0));
        assert_eq!(find_range(&r, 420.0), Some(1));
        assert_eq!(find_range(&r, 728.0), Some(1));
        assert_eq!(find_range(&r, 729.0), None);
        assert_eq!(find_upper_range(&r, 800.0), 0);
        assert_eq!(find_view_range(&r, 1.0, true), 0);
        assert_eq!(find_view_range(&r, 420.0, false), 0);
    }

    #[test]
    fn ir_bands() {
        let t = [
            ValueRange { min: 0.0, max: 0.3 },
            ValueRange { min: 0.3, max: 0.8 },
            ValueRange { min: 0.8, max: 100.0 },
        ];
        assert_eq!(find_ir_band(&t, 0.3), 0);
        assert_eq!(find_ir_band(&t, 0.5), 1);
        assert_eq!(find_ir_band(&t, 5.0), 2);
        assert_eq!(find_ir_band(&t[..2], 5.0), 0);
    }

    #[test]
    fn short_tables_do_not_panic() {
        let config = FusionConfig { screenshot_v2_1_supported: true, ..Default::default() };
        let reading = Reading { raw_channels: [1.0, 2.0, 3.0, 4.0], brightness: 100, dc_mode: false };
        assert_eq!(calculate_lux(&config, &reading.raw_channels, 100, false), None);
        assert_eq!(corrected_lux(&config, &[], &reading, &ScreenColor::default()), None);
        assert_eq!(view_lux(&config, &[], &reading, &ScreenColor::default()), None);
        assert_eq!(calculate_lux(&config, &[f64::MAX; 4], 0, true), None);
    }
}
