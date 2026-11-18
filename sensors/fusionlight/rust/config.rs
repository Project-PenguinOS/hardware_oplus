/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

//! FusionLight profiles (/odm/etc/fusionlight_profile/*.json) and the device calibration string.

use crate::json::{as_bool, elements, is_member, member, parse_int, read_int, read_number, read_required_int, size};
use serde_json::Value;

pub const CHANNELS: usize = 4;
pub const IR_BANDS: usize = 3;
pub const LEAKAGE_TERMS: usize = 35;
pub const RATIO_TERMS: usize = 20;

/// Profiles index levels by number; anything past this is a broken profile, not a table.
const MAX_LEVELS: i32 = 4096;
pub const DEFAULT_SCREENSHOT_PERIOD_MS: i32 = 250;

pub type Channels = [f64; CHANNELS];
pub type Polynomial = [f64; 4];
pub type LeakageModel = [f64; LEAKAGE_TERMS];
pub type RatioModel = [f64; RATIO_TERMS];
pub type GreyScale = [f64; 3];
pub type ChannelLimits = [i32; CHANNELS];
/// Rows are sensor channels, columns are the R/G/B/W screen views.
pub type ViewCalibration = [[f64; CHANNELS]; CHANNELS];

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ValueRange {
    pub min: f64,
    pub max: f64,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct CwbConfig {
    pub crop_left: i32,
    pub crop_top: i32,
    pub crop_right: i32,
    pub crop_bottom: i32,
    pub reference_width: i32,
    pub reference_height: i32,
    pub screenshot_weighted: bool,
    pub screenshot_period_ms: i32,
    /// Per pixel weights of the capture rectangle, row major, when screenshot_weighted.
    pub weights: Vec<i32>,
}

/// Pre-V2.1 model for one FusionLightBrightnesses segment.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ViewSegment {
    pub ir_brightness: Vec<ValueRange>,
    pub lux_coefficients: [Vec<Channels>; IR_BANDS],
    /// Indexed by sensor channel, then by R/G/B/W screen view.
    pub golden: [Channels; CHANNELS],
    pub grey_scale: [GreyScale; CHANNELS],
    pub leakage: [[Polynomial; CHANNELS]; CHANNELS],
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct FusionConfig {
    pub sensor_module_id: i32,
    pub ir_ratio_formula: i32,
    pub low_light_accuracy: f64,
    pub fusion_rgb_supported: bool,
    pub cwb_supported: bool,
    pub black_frame_supported: bool,
    pub screenshot_v2_1_supported: bool,
    pub screen_off_lux_supported: bool,
    pub channel_count_policy_supported: bool,
    pub fod_linearity_segment_supported: bool,
    pub cwb: CwbConfig,
    pub linearity_brightness: Vec<ValueRange>,
    pub ir_brightness: Vec<ValueRange>,
    pub ir_thresholds: Vec<ValueRange>,
    pub linearity: Vec<[Polynomial; CHANNELS]>,
    pub leakage: Vec<[LeakageModel; CHANNELS]>,
    pub leakage_ratio: Vec<[RatioModel; CHANNELS]>,
    pub leakage_golden: Vec<Channels>,
    pub lux_coefficients: [Vec<Channels>; IR_BANDS],
    pub channel_count_coefficients: [Vec<Channels>; IR_BANDS],
    pub screen_off_coefficients: [Vec<Channels>; IR_BANDS],
    pub channel_thresholds: Vec<ChannelLimits>,
    pub c_zero_thresholds: Vec<f64>,
    pub segment_brightness: Vec<i32>,
    pub segments: Vec<ViewSegment>,
    pub fod_linearity: Vec<[Polynomial; CHANNELS]>,
    pub fod_linearity_constant: Channels,
}

/// The levels of `items` (each with a `key` level), as a vector sized to the highest one.
fn level_count(items: &[&Value], key: &str) -> Option<usize> {
    let mut max = -1;
    for item in items {
        let level = read_required_int(item, key)?;
        if !(0..MAX_LEVELS).contains(&level) {
            return None;
        }
        max = max.max(level);
    }
    usize::try_from(max + 1).ok()
}

/// Fills one slot per level of `array`; every level must appear exactly once.
fn parse_levels<T: Clone + Default>(
    array: &Value,
    key: &str,
    mut read: impl FnMut(&Value) -> Option<T>,
) -> Option<Vec<T>> {
    if !array.is_array() || size(array) == 0 {
        return None;
    }
    let items = elements(array);
    let mut values = vec![None; level_count(&items, key)?];
    for item in items {
        let level = read_required_int(item, key)? as usize;
        if values[level].is_some() {
            return None;
        }
        values[level] = Some(read(item)?);
    }
    values.into_iter().collect()
}

fn parse_ranges(array: &Value, min: &str, max: &str) -> Option<Vec<ValueRange>> {
    parse_levels(array, "Level", |item| {
        let range = ValueRange { min: read_number(member(item, min))?, max: read_number(member(item, max))? };
        (range.min <= range.max).then_some(range)
    })
}

fn read_channels(item: &Value, names: [&str; CHANNELS]) -> Option<Channels> {
    let mut channels = [0.0; CHANNELS];
    for (value, name) in channels.iter_mut().zip(names) {
        *value = read_number(member(item, name))?;
    }
    Some(channels)
}

fn parse_lux_coefficients(array: &Value) -> Option<Vec<Channels>> {
    parse_levels(array, "Level", |item| {
        read_channels(item, ["ChannelR", "ChannelG", "ChannelB", "ChannelC"])
    })
}

/// Per channel entries `{channel, Parameter: [{level, Parameter0..N}]}`, as [level][channel].
fn parse_channel_models<const N: usize>(array: &Value) -> Option<Vec<[[f64; N]; CHANNELS]>> {
    if !array.is_array() || size(array) != CHANNELS {
        return None;
    }
    let entries = elements(array);
    let mut max = -1;
    for entry in &entries {
        let parameters = member(entry, "Parameter");
        if !parameters.is_array() {
            return None;
        }
        let count = level_count(&elements(parameters), "level")?;
        max = max.max(count as i32 - 1);
    }
    if max < 0 {
        return None;
    }
    let mut models = vec![[[0.0; N]; CHANNELS]; max as usize + 1];
    let mut seen = vec![[false; CHANNELS]; models.len()];
    for entry in entries {
        let channel = read_required_int(entry, "channel")?;
        let channel = usize::try_from(channel).ok().filter(|&c| c < CHANNELS)?;
        for item in elements(member(entry, "Parameter")) {
            let level = read_required_int(item, "level")? as usize;
            if seen[level][channel] {
                return None;
            }
            for (p, value) in models[level][channel].iter_mut().enumerate() {
                *value = read_number(member(item, &format!("Parameter{p}")))?;
            }
            seen[level][channel] = true;
        }
    }
    seen.iter().all(|level| level.iter().all(|&s| s)).then_some(models)
}

fn parse_golden(array: &Value) -> Option<Vec<Channels>> {
    parse_levels(array, "level", |item| read_channels(item, ["RGolden", "GGolden", "BGolden", "CGolden"]))
}

fn parse_channel_thresholds(array: &Value) -> Option<Vec<ChannelLimits>> {
    if !array.is_array() {
        return None;
    }
    elements(array)
        .into_iter()
        .map(|item| {
            let mut limits = [0; CHANNELS];
            for (limit, name) in limits.iter_mut().zip(["RMax", "GMax", "BMax", "CMax"]) {
                *limit = read_int(member(item, name))?;
            }
            Some(limits)
        })
        .collect()
}

/// Levels must be dense: 0..len, each once.
fn parse_dense_levels<T: Clone + Default>(
    array: &Value,
    read: impl Fn(&Value) -> Option<T>,
) -> Option<Vec<T>> {
    if !array.is_array() || size(array) == 0 {
        return None;
    }
    let items = elements(array);
    let mut values = vec![None; items.len()];
    for item in items {
        let level = usize::try_from(read_required_int(item, "Level")?).ok().filter(|&l| l < values.len())?;
        if values[level].is_some() {
            return None;
        }
        values[level] = Some(read(item)?);
    }
    values.into_iter().collect()
}

fn parse_c_zero_thresholds(array: &Value) -> Option<Vec<f64>> {
    parse_dense_levels(array, |item| read_number(member(item, "CZeroMin")))
}

/// `{index_name: i, names...}` rows, one per channel.
fn parse_channel_rows<const N: usize>(array: &Value, index_name: &str, names: [&str; N]) -> Option<[[f64; N]; CHANNELS]> {
    if !array.is_array() || size(array) != CHANNELS {
        return None;
    }
    let mut rows = [[0.0; N]; CHANNELS];
    let mut seen = [false; CHANNELS];
    for item in elements(array) {
        let index = usize::try_from(read_required_int(item, index_name)?).ok().filter(|&i| i < CHANNELS)?;
        if seen[index] {
            return None;
        }
        for (value, name) in rows[index].iter_mut().zip(names) {
            *value = read_number(member(item, name))?;
        }
        seen[index] = true;
    }
    Some(rows)
}

fn parse_linearity_functions(array: &Value) -> Option<Vec<[Polynomial; CHANNELS]>> {
    parse_levels(array, "Function", |item| {
        parse_channel_rows(
            member(item, "LinearityParameter"),
            "Channel",
            ["Parameter0", "Parameter1", "Parameter2", "Parameter3"],
        )
    })
}

/// FusionLightBrightnesses, which may be absent. The QTI code did not require every level.
fn parse_segment_brightness(array: &Value) -> Option<Vec<i32>> {
    if array.is_null() {
        return Some(vec![]);
    }
    if !array.is_array() {
        return None;
    }
    let items = elements(array);
    let mut brightness = vec![0; items.len()];
    let mut seen = vec![false; items.len()];
    for item in items {
        let level = usize::try_from(read_required_int(item, "Level")?).ok().filter(|&l| l < brightness.len())?;
        if seen[level] {
            return None;
        }
        brightness[level] = read_required_int(item, "Brightness")?;
        seen[level] = true;
    }
    Some(brightness)
}

fn parse_view_segment(root: &Value, prefix: &str) -> Option<ViewSegment> {
    let field = |name: &str| member(root, &format!("{prefix}{name}"));
    let golden = parse_channel_rows(field("Golden"), "Channel", ["RGolden", "GGolden", "BGolden", "WGolden"])?;
    let grey_scale = parse_channel_rows(field("GreyScale"), "Channel", ["RGreyscale", "GGreyscale", "BGreyscale"])?;
    let ir_brightness = parse_ranges(field("IRBrightness"), "BrightnessMin", "BrightnessMax")?;
    let mut leakage = [[[0.0; 4]; CHANNELS]; CHANNELS];
    for (channel, table) in ["RParameters", "GParameters", "BParameters", "CParameters"].iter().enumerate() {
        // Leakage polynomials list the highest-order coefficient last.
        leakage[channel] =
            parse_channel_rows(field(table), "Color", ["Parameter3", "Parameter2", "Parameter1", "Parameter0"])?;
    }
    let mut lux_coefficients: [Vec<Channels>; IR_BANDS] = Default::default();
    for (band, table) in ["LuxCoeffLIR", "LuxCoeffHIR", "LuxCoeffSuperHIR"].iter().enumerate() {
        let coefficients = parse_lux_coefficients(field(table))?;
        if coefficients.len() < ir_brightness.len() {
            return None;
        }
        lux_coefficients[band] = coefficients;
    }
    Some(ViewSegment { ir_brightness, lux_coefficients, golden, grey_scale, leakage })
}

fn parse_v21_model(root: &Value, config: &mut FusionConfig) -> Result<(), String> {
    let invalid = || "invalid V2.1 model".to_owned();
    config.linearity_brightness =
        parse_ranges(member(root, "LinearityBrightnessRange"), "BrightnessMin", "BrightnessMax").ok_or_else(invalid)?;
    config.ir_brightness =
        parse_ranges(member(root, "IRBrightness_V2_1"), "BrightnessMin", "BrightnessMax").ok_or_else(invalid)?;
    config.ir_thresholds =
        parse_ranges(member(root, "IRThreshold_V2_1"), "IR_Ratio_Min", "IR_Ratio_Max").ok_or_else(invalid)?;

    // Profiles may omit the leakage model entirely. Stock then keeps zeroed tables, which
    // leaves the raw channels uncompensated.
    let tables = ["LinearityCompensation", "LightLeakageCalculation", "LightLeakageRatio", "LightLeakageGolden"];
    let has_leakage_model = tables.iter().any(|t| !member(root, t).is_null());
    if has_leakage_model {
        let leakage_invalid = || "invalid V2.1 leakage model".to_owned();
        config.linearity = parse_channel_models::<4>(member(root, "LinearityCompensation")).ok_or_else(leakage_invalid)?;
        config.leakage =
            parse_channel_models::<LEAKAGE_TERMS>(member(root, "LightLeakageCalculation")).ok_or_else(leakage_invalid)?;
        config.leakage_ratio =
            parse_channel_models::<RATIO_TERMS>(member(root, "LightLeakageRatio")).ok_or_else(leakage_invalid)?;
        config.leakage_golden = parse_golden(member(root, "LightLeakageGolden")).ok_or_else(leakage_invalid)?;
    }
    for (band, table) in ["LuxCoeffLIR_V2_1", "LuxCoeffHIR_V2_1", "LuxCoeffSuperHIR_V2_1"].iter().enumerate() {
        config.lux_coefficients[band] =
            parse_lux_coefficients(member(root, table)).ok_or("invalid normal lux coefficient table")?;
    }
    let levels = config.linearity_brightness.len();
    if (has_leakage_model
        && (config.linearity.len() != levels
            || config.leakage.len() != levels
            || config.leakage_ratio.len() != levels
            || config.leakage_golden.len() != levels))
        || config.ir_brightness.is_empty()
    {
        return Err("inconsistent V2.1 model dimensions".to_owned());
    }
    if config.lux_coefficients.iter().any(|t| t.len() < config.ir_brightness.len()) {
        return Err("coefficient table does not cover its selector".to_owned());
    }
    Ok(())
}

fn parse_view_model(root: &Value, config: &mut FusionConfig) -> Result<(), String> {
    let invalid = || "invalid linearity model".to_owned();
    config.linearity_brightness =
        parse_ranges(member(root, "LinearityBrightnessRange"), "BrightnessMin", "BrightnessMax").ok_or_else(invalid)?;
    config.ir_thresholds =
        parse_ranges(member(root, "IRThreshold"), "IR_Ratio_Min", "IR_Ratio_Max").ok_or_else(invalid)?;
    config.linearity = parse_linearity_functions(member(root, "Linearity")).ok_or_else(invalid)?;
    if config.fod_linearity_segment_supported {
        config.fod_linearity = parse_linearity_functions(member(root, "LinearityForFODSegment")).ok_or_else(invalid)?;
    }
    config.segment_brightness = parse_segment_brightness(member(root, "FusionLightBrightnesses")).ok_or_else(invalid)?;

    let fod = member(root, "LinearityForFOD");
    if !fod.is_null() {
        let names = ["FOD_Linearity_R", "FOD_Linearity_G", "FOD_Linearity_B", "FOD_Linearity_C"];
        config.fod_linearity_constant = read_channels(fod, names).ok_or("invalid LinearityForFOD")?;
    }
    // The low and medium brightness models are only used with three segments.
    let prefixes: &[&str] = if config.segment_brightness.len() == IR_BANDS { &["", "M_", "L_"] } else { &[""] };
    config.segments = prefixes
        .iter()
        .map(|prefix| {
            parse_view_segment(root, prefix).ok_or_else(|| {
                let name = if prefix.is_empty() { "default" } else { prefix };
                format!("invalid {name} screen leakage model")
            })
        })
        .collect::<Result<_, _>>()?;
    Ok(())
}

/// Parses a profile. `sensor_module_id` is the module derived from the sensor name, which the
/// profile's own SensorModuleId overrides.
pub fn parse_config(root: &Value, sensor_module_id: i32) -> Result<FusionConfig, String> {
    let mut config = FusionConfig { sensor_module_id, ..Default::default() };
    let common = member(root, "CommonConfig");
    let crop = member(common, "ScreenShotRect");
    let resolution = member(common, "ScreenResolution");
    let invalid = || "invalid CommonConfig".to_owned();
    if !common.is_object() || !crop.is_object() || !resolution.is_object() {
        return Err(invalid());
    }
    let cwb = &mut config.cwb;
    cwb.crop_left = read_required_int(crop, "LeftTopX").ok_or_else(invalid)?;
    cwb.crop_top = read_required_int(crop, "LeftTopY").ok_or_else(invalid)?;
    cwb.crop_right = read_required_int(crop, "RightBottomX").ok_or_else(invalid)?;
    cwb.crop_bottom = read_required_int(crop, "RightBottomY").ok_or_else(invalid)?;
    cwb.reference_width = read_required_int(resolution, "Width").ok_or_else(invalid)?;
    cwb.reference_height = read_required_int(resolution, "Height").ok_or_else(invalid)?;
    cwb.screenshot_period_ms = if is_member(common, "CWBScreenshotPeriod") {
        read_required_int(common, "CWBScreenshotPeriod").ok_or_else(invalid)?
    } else {
        DEFAULT_SCREENSHOT_PERIOD_MS
    };
    if is_member(common, "IRRatioFormulaType") {
        config.ir_ratio_formula = read_int(member(common, "IRRatioFormulaType")).ok_or_else(invalid)?;
    }
    if is_member(common, "LowLightAccuracy") {
        config.low_light_accuracy = read_number(member(common, "LowLightAccuracy")).ok_or_else(invalid)?;
    }
    let module = member(common, "SensorModule");
    if is_member(module, "SensorModuleId") {
        config.sensor_module_id = read_int(member(module, "SensorModuleId")).ok_or_else(invalid)?;
    }
    if config.ir_ratio_formula != 0 && config.ir_ratio_formula != 2 {
        return Err(format!("unsupported IR ratio formula {}", config.ir_ratio_formula));
    }
    let flag = |name| as_bool(member(common, name));
    config.fusion_rgb_supported = flag("FusionRGBSupported");
    config.cwb_supported = flag("CWBSupported");
    config.black_frame_supported = flag("BlackFrame30Support");
    config.cwb.screenshot_weighted = flag("SupportCWBScreenshotWeighted");
    config.screenshot_v2_1_supported = flag("SupportScreenshotAlgorithm_V2_1");
    config.screen_off_lux_supported = flag("ScreenOffCalLuxSupported");
    config.channel_count_policy_supported = flag("ChannelCountPolicySupported");
    config.fod_linearity_segment_supported = flag("FODLinearityParasSegmentSupported");

    if config.screenshot_v2_1_supported {
        parse_v21_model(root, &mut config)?;
    } else {
        parse_view_model(root, &mut config)?;
    }

    let count_tables = ["LuxCoeffLirChCountPolicy", "LuxCoeffHirChCountPolicy", "LuxCoeffSuperHirChCountPolicy"];
    let screen_off_tables = ["LuxCoeffLirScreenOff", "LuxCoeffHirScreenOff", "LuxCoeffSuperHirScreenOff"];
    for band in 0..IR_BANDS {
        if config.channel_count_policy_supported {
            config.channel_count_coefficients[band] = parse_lux_coefficients(member(root, count_tables[band]))
                .ok_or("invalid channel-count coefficient table")?;
        }
        if config.screen_off_lux_supported {
            config.screen_off_coefficients[band] = parse_lux_coefficients(member(root, screen_off_tables[band]))
                .ok_or("invalid screen-off coefficient table")?;
        }
    }
    if config.channel_count_policy_supported {
        config.channel_thresholds =
            parse_channel_thresholds(member(root, "ChannelCountThreshold")).ok_or("invalid channel-count thresholds")?;
    }
    if config.screen_off_lux_supported {
        config.c_zero_thresholds =
            parse_c_zero_thresholds(member(root, "CZeroThreshold")).ok_or("invalid C-zero thresholds")?;
    }

    let cwb = &config.cwb;
    if config.linearity_brightness.is_empty()
        || config.ir_thresholds.len() != IR_BANDS
        || cwb.crop_left < 0
        || cwb.crop_top < 0
        || cwb.crop_right <= cwb.crop_left
        || cwb.crop_bottom <= cwb.crop_top
        || cwb.crop_right > cwb.reference_width
        || cwb.crop_bottom > cwb.reference_height
        || cwb.screenshot_period_ms <= 0
    {
        return Err("inconsistent profile dimensions".to_owned());
    }
    for band in 0..IR_BANDS {
        if (config.channel_count_policy_supported
            && config.channel_count_coefficients[band].len() < config.channel_thresholds.len())
            || (config.screen_off_lux_supported
                && config.screen_off_coefficients[band].len() < config.c_zero_thresholds.len())
        {
            return Err("coefficient table does not cover its selector".to_owned());
        }
    }
    Ok(config)
}

pub const CWB_WEIGHTS: &str = "/odm/etc/display/cwb_weightspos.json";

/// Loads the capture weights (`text` is cwb_weightspos.json); sampling falls back to
/// unweighted when they are missing or do not match the capture rectangle.
pub fn load_cwb_weights(cwb: &mut CwbConfig, text: Option<&str>) {
    if !cwb.screenshot_weighted {
        return;
    }
    let pixels = i64::from(cwb.crop_right - cwb.crop_left) * i64::from(cwb.crop_bottom - cwb.crop_top);
    let weights = text.and_then(|t| serde_json::from_str::<Value>(t).ok()).and_then(|root| {
        let count = read_int(member(&root, "count"))?;
        let weights = member(&root, "Weights");
        if count <= 0 || !weights.is_array() || size(weights) != count as usize || i64::from(count) != pixels {
            return None;
        }
        elements(weights).into_iter().map(|w| read_int(w).filter(|&w| w >= 0)).collect::<Option<Vec<_>>>()
    });
    match weights {
        Some(weights) => cwb.weights = weights,
        None => cwb.screenshot_weighted = false,
    }
}

/// The sensor module of the ALS chip, from its name.
pub fn sensor_module_id(sensor_name: &str) -> i32 {
    let name = sensor_name.to_lowercase();
    const MODULES: [(&str, i32); 8] = [
        ("tcs3720", 3),
        ("sip3625", 4),
        ("sip3635", 5),
        ("stk36c01", 6),
        ("tcs3743", 8),
        ("tcs3760", 10),
        ("stk3bfx", 11),
        ("stk32f02", 12),
    ];
    MODULES.iter().find(|(chip, _)| name.contains(chip)).map_or(3, |&(_, id)| id)
}

/// The panel id the profile file names use, from the panel name the display HAL reports.
pub fn panel_id(panel_name: &str) -> Option<i32> {
    let lower = panel_name.to_lowercase();
    if lower.contains("samsung") {
        return Some(0);
    }
    if lower.contains("boe") {
        return Some(1);
    }
    if panel_name.contains("TM") || lower.contains("tianma") {
        return Some(2);
    }
    if lower.starts_with("visionox") {
        return Some(3);
    }
    if panel_name.contains("HX") {
        return Some(4);
    }
    let mut parts = panel_name.split('_');
    if parts.next()? != "P" {
        return None;
    }
    let id = match parts.next()? {
        "1" => 0,
        "3" => 1,
        "7" => 2,
        "B" => 3,
        "D" => 4,
        _ => return None,
    };
    let (Some(version), None) = (parts.next(), parts.next()) else { return Some(id) };
    match parse_int(version) {
        Some(v) if (1..=255).contains(&v) => Some((id << 8) | v),
        _ => Some(id),
    }
}

/// The profile path for a panel, sensor module and optional panel stage.
pub fn profile_path(panel_id: i32, module_id: i32, panel_stage: Option<i32>) -> String {
    match panel_stage {
        Some(stage) => format!("/odm/etc/fusionlight_profile/fusionlight_Main_{panel_id:X}_{module_id:X}_{stage:X}.json"),
        None => format!("/odm/etc/fusionlight_profile/fusionlight_Main_{panel_id:X}_{module_id:X}.json"),
    }
}

/// The first `name` member anywhere under `value`, depth first with members in name order,
/// as jsoncpp's getMemberNames() returns them.
fn find_member<'a>(value: &'a Value, name: &str) -> Option<&'a Value> {
    let object = value.as_object()?;
    if let Some(found) = object.get(name) {
        return Some(found);
    }
    let mut names: Vec<&String> = object.keys().collect();
    names.sort();
    names.into_iter().find_map(|key| find_member(&object[key], name))
}

/// V2.1 calibration: measured W_VIEW_<channel>_<level> over the profile's golden value.
pub fn calibration(config: &FusionConfig, device: Option<&Value>) -> Vec<Channels> {
    let mut calibration = vec![[1.0; CHANNELS]; config.leakage_golden.len()];
    let Some(root) = device else { return calibration };
    for (level, row) in calibration.iter_mut().enumerate() {
        for (channel, name) in ["R", "G", "B", "C"].iter().enumerate() {
            let golden = config.leakage_golden[level][channel];
            let measured = find_member(root, &format!("W_VIEW_{name}_{level}")).and_then(read_number);
            if let Some(measured) = measured.filter(|&m| m != 0.0 && golden != 0.0) {
                row[channel] = measured / golden;
            }
        }
    }
    calibration
}

/// View model calibration, indexed [segment][channel][view]; unit when any value is missing.
pub fn view_calibration(config: &FusionConfig, device: Option<&Value>) -> Vec<ViewCalibration> {
    let unit = vec![[[1.0; CHANNELS]; CHANNELS]; config.segments.len()];
    let Some(root) = device else { return unit };
    let mut calibration = unit.clone();
    for (segment, suffix) in ["", "_M", "_L"].iter().enumerate().take(config.segments.len()) {
        // Measured leakage is indexed by screen view, then by sensor channel.
        let mut measured = [[0.0; CHANNELS]; CHANNELS];
        for (view, view_name) in ["R", "G", "B", "W"].iter().enumerate() {
            for (channel, name) in ["R", "G", "B", "C"].iter().enumerate() {
                let key = format!("{view_name}_VIEW_{name}_MAX{suffix}");
                let Some(value) = find_member(root, &key).and_then(read_number) else { return unit };
                measured[view][channel] = value;
            }
        }
        let golden = &config.segments[segment].golden;
        let result = &mut calibration[segment];
        for channel in 0..CHANNELS {
            let mut measured_sum = 0.0;
            let mut golden_sum = 0.0;
            for view in 0..CHANNELS - 1 {
                result[channel][view] = measured[view][channel] / golden[channel][view];
                measured_sum += measured[view][channel];
                golden_sum += golden[channel][view];
            }
            // The white view is calibrated from what the color views leave unexplained.
            let mut golden_white = golden_sum - golden[channel][CHANNELS - 1];
            if golden_white.abs() < 1e-7 {
                golden_white = 1.0;
            }
            result[channel][CHANNELS - 1] = (measured_sum - measured[CHANNELS - 1][channel]) / golden_white;
        }
        // Stock leaves unmeasured entries at unity, indexing them as measured.
        for row in 0..CHANNELS {
            for column in 0..CHANNELS {
                if measured[row][column].abs() < 1e-7 {
                    result[row][column] = 1.0;
                }
            }
        }
    }
    calibration
}
