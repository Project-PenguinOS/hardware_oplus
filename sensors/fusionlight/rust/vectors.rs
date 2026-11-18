/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

//! Host tool for the FusionLight differential test: parses a profile and evaluates input
//! vectors, printing JSON that compare_fusionlight.py diffs against the C++ reference.
//! Usage: fusionlight_rust_vectors <profile.json> <sensor module id> <inputs.jsonl>

use fusionlight::config::{self, FusionConfig, ValueRange};
use fusionlight::lux::{self, Reading, ScreenColor};
use serde_json::{json, Value};
use std::io::{BufRead, Write};

fn ranges(r: &[ValueRange]) -> Value {
    json!(r.iter().map(|r| [r.min, r.max]).collect::<Vec<_>>())
}

fn dump(c: &FusionConfig) -> Value {
    let seg: Vec<Value> = c
        .segments
        .iter()
        .map(|s| {
            json!({
                "ir_brightness": ranges(&s.ir_brightness),
                "lux_coefficients": s.lux_coefficients,
                "golden": s.golden,
                "grey_scale": s.grey_scale,
                "leakage": s.leakage,
            })
        })
        .collect();
    let leakage: Vec<Vec<Vec<f64>>> = c.leakage.iter().map(|l| l.iter().map(|m| m.to_vec()).collect()).collect();
    let ratio: Vec<Vec<Vec<f64>>> = c.leakage_ratio.iter().map(|l| l.iter().map(|m| m.to_vec()).collect()).collect();
    json!({
        "sensor_module_id": c.sensor_module_id,
        "ir_ratio_formula": c.ir_ratio_formula,
        "low_light_accuracy": c.low_light_accuracy,
        "flags": [c.fusion_rgb_supported, c.cwb_supported, c.black_frame_supported, c.screenshot_v2_1_supported,
                  c.screen_off_lux_supported, c.channel_count_policy_supported, c.fod_linearity_segment_supported,
                  c.cwb.screenshot_weighted],
        "cwb": [c.cwb.crop_left, c.cwb.crop_top, c.cwb.crop_right, c.cwb.crop_bottom, c.cwb.reference_width,
                c.cwb.reference_height, c.cwb.screenshot_period_ms],
        "linearity_brightness": ranges(&c.linearity_brightness),
        "ir_brightness": ranges(&c.ir_brightness),
        "ir_thresholds": ranges(&c.ir_thresholds),
        "linearity": c.linearity,
        "leakage": leakage,
        "leakage_ratio": ratio,
        "leakage_golden": c.leakage_golden,
        "lux_coefficients": c.lux_coefficients,
        "channel_count_coefficients": c.channel_count_coefficients,
        "screen_off_coefficients": c.screen_off_coefficients,
        "channel_thresholds": c.channel_thresholds,
        "c_zero_thresholds": c.c_zero_thresholds,
        "segment_brightness": c.segment_brightness,
        "segments": seg,
        "fod_linearity": c.fod_linearity,
        "fod_linearity_constant": c.fod_linearity_constant,
    })
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let text = std::fs::read_to_string(&args[1]).expect("profile");
    let module: i32 = args[2].parse().expect("module id");
    let root: Value = serde_json::from_str(&text).expect("profile json");
    let mut config = match config::parse_config(&root, module) {
        Ok(c) => c,
        Err(e) => {
            println!("{}", json!({ "config_error": e }));
            return;
        }
    };
    let weights = std::fs::read_to_string(config::CWB_WEIGHTS).ok();
    config::load_cwb_weights(&mut config.cwb, weights.as_deref());
    let mut out = std::io::stdout().lock();
    writeln!(out, "{}", json!({ "config": dump(&config) })).unwrap();
    let calibration_text = std::fs::read_to_string(&args[3].replace("inputs", "calibration")).ok();
    let device = calibration_text.as_deref().and_then(|t| serde_json::from_str::<Value>(t).ok());
    let calibration = config::calibration(&config, device.as_ref());
    let view_calibration = config::view_calibration(&config, device.as_ref());
    writeln!(out, "{}", json!({ "calibration": calibration, "view_calibration": view_calibration })).unwrap();

    let inputs = std::io::BufReader::new(std::fs::File::open(&args[3]).expect("inputs"));
    for line in inputs.lines() {
        let v: Value = serde_json::from_str(&line.unwrap()).unwrap();
        let f = |k: &str| v[k].as_f64().unwrap();
        let channels = [f("r"), f("g"), f("b"), f("c")];
        let reading = Reading { raw_channels: channels, brightness: v["brightness"].as_i64().unwrap() as i32, dc_mode: v["dc"].as_bool().unwrap() };
        let color = ScreenColor { r: v["sr"].as_i64().unwrap() as i32, g: v["sg"].as_i64().unwrap() as i32, b: v["sb"].as_i64().unwrap() as i32 };
        let brightness = f64::from(reading.brightness);
        let result = json!({
            "band": lux::find_ir_band(&config.ir_thresholds, lux::ir_ratio(&config, &channels)),
            "policy": lux::find_channel_policy(&config, &channels).map_or(-1, |l| l as i64),
            "czero": lux::find_c_zero_level(&config, channels[3]),
            "upper": lux::find_upper_range(&config.ir_brightness, brightness),
            "lin_level": lux::find_range(&config.linearity_brightness, brightness).map_or(-1, |l| l as i64),
            "ir_ratio": lux::ir_ratio(&config, &channels),
            "lux_on": lux::calculate_lux(&config, &channels, reading.brightness, false),
            "lux_off": lux::calculate_lux(&config, &channels, reading.brightness, true),
            "corrected": if config.screenshot_v2_1_supported { lux::corrected_lux(&config, &calibration, &reading, &color) } else { None },
            "view": if config.screenshot_v2_1_supported { None } else { lux::view_lux(&config, &view_calibration, &reading, &color) },
        });
        writeln!(out, "{result}").unwrap();
    }
}
