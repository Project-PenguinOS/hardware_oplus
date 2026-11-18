/*
 * Copyright (C) 2021-2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

//! Display-independent parts of the area capture: grab area, weights, crop mapping and
//! averaging.

use std::sync::OnceLock;

/// A rectangle in pixels; right and bottom are exclusive.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Rect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

impl Rect {
    pub fn width(&self) -> i32 {
        self.right - self.left
    }

    pub fn height(&self) -> i32 {
        self.bottom - self.top
    }

    pub fn is_empty(&self) -> bool {
        self.width() <= 0 || self.height() <= 0
    }
}

/// Primary display state needed to place the grab area.
#[derive(Clone, Copy, Debug)]
pub struct Geometry {
    /// Resolution of the largest display mode.
    pub panel_width: i32,
    pub panel_height: i32,
    pub layer_stack_width: i32,
    pub layer_stack_height: i32,
    /// ui::Rotation of the layer stack relative to the panel, 0-3.
    pub rotation: i32,
}

/// Grab area weights in panel orientation, row major.
pub struct Weighting<'a> {
    pub weights: &'a [f32],
    pub rotation: i32,
    pub panel_width: i32,
    pub panel_height: i32,
}

/// Parses vendor.sensors.als_correction.grabrect ("left top right bottom").
pub fn parse_grab_rect(value: &str) -> Option<Rect> {
    let mut values = value.split_whitespace().map(str::parse::<i32>);
    let mut next = || values.next()?.ok();
    let rect = Rect {
        left: next()?,
        top: next()?,
        right: next()?,
        bottom: next()?,
    };
    if rect.left < 0 || rect.top < 0 || rect.right <= rect.left || rect.bottom <= rect.top {
        return None;
    }
    Some(rect)
}

/// Parses the stock CWB weights, which must cover exactly `count` pixels.
pub fn parse_weights(json: &str, count: usize) -> Result<Vec<f32>, &'static str> {
    let root: serde_json::Value =
        serde_json::from_str(json).map_err(|_| "does not match the grab area")?;
    let values = root
        .get("Weights")
        .and_then(|weights| weights.as_array())
        .filter(|weights| weights.len() == count)
        .ok_or("does not match the grab area")?;
    values
        .iter()
        .map(|value| {
            value
                .as_f64()
                .map(|w| w as f32)
                .filter(|w| w.is_finite() && *w >= 0.0)
        })
        .collect::<Option<Vec<f32>>>()
        .ok_or("has an invalid weight")
}

/// Maps the grab area, given in panel pixels of the largest display mode, to a crop in layer
/// stack space, which follows the display rotation and any resolution or size override.
pub fn capture_crop(rect: &Rect, geometry: &Geometry) -> Option<Rect> {
    let (panel_w, panel_h) = (geometry.panel_width, geometry.panel_height);
    let (stack_w, stack_h) = (geometry.layer_stack_width, geometry.layer_stack_height);
    if panel_w <= 0
        || panel_h <= 0
        || stack_w <= 0
        || stack_h <= 0
        || rect.left < 0
        || rect.top < 0
        || rect.right <= rect.left
        || rect.bottom <= rect.top
        || rect.right > panel_w
        || rect.bottom > panel_h
        || !(0..=3).contains(&geometry.rotation)
    {
        return None;
    }

    let l = rect.left as f32 / panel_w as f32;
    let t = rect.top as f32 / panel_h as f32;
    let r = rect.right as f32 / panel_w as f32;
    let b = rect.bottom as f32 / panel_h as f32;
    let (left, top, right, bottom) = match geometry.rotation {
        1 => (t, 1.0 - r, b, 1.0 - l),
        2 => (1.0 - r, 1.0 - b, 1.0 - l, 1.0 - t),
        3 => (1.0 - b, l, 1.0 - t, r),
        _ => (l, t, r, b),
    };

    let crop = Rect {
        left: ((left * stack_w as f32).floor() as i32).max(0),
        top: ((top * stack_h as f32).floor() as i32).max(0),
        right: ((right * stack_w as f32).ceil() as i32).min(stack_w),
        bottom: ((bottom * stack_h as f32).ceil() as i32).min(stack_h),
    };
    (!crop.is_empty()).then_some(crop)
}

/// Index into the panel-oriented weights for capture pixel (x, y).
pub fn panel_index(rotation: i32, x: u32, y: u32, panel_w: i32, panel_h: i32) -> Option<usize> {
    let (x, y, w, h) = (
        i64::from(x),
        i64::from(y),
        i64::from(panel_w),
        i64::from(panel_h),
    );
    let (px, py) = match rotation {
        1 => (w - 1 - y, x),
        2 => (w - 1 - x, h - 1 - y),
        3 => (y, h - 1 - x),
        _ => (x, y),
    };
    if px < 0 || py < 0 || px >= w || py >= h {
        return None;
    }
    usize::try_from(py * w + px).ok()
}

// sRGB transfer parameters as in frameworks/native/libs/ui/ColorSpace.cpp.
const SRGB_A: f32 = 1.0 / 1.055;
const SRGB_B: f32 = 0.055 / 1.055;
const SRGB_C: f32 = 1.0 / 12.92;
const SRGB_D: f32 = 0.04045;
const SRGB_G: f32 = 2.4;

fn srgb_eotf(x: f32) -> f32 {
    if x >= SRGB_D {
        (SRGB_A * x + SRGB_B).powf(SRGB_G)
    } else {
        SRGB_C * x
    }
}

fn srgb_oetf(x: f32) -> f32 {
    if x >= SRGB_D * SRGB_C {
        (x.powf(1.0 / SRGB_G) - SRGB_B) / SRGB_A
    } else {
        x / SRGB_C
    }
}

fn srgb_to_linear_lut() -> &'static [f32; 256] {
    static LUT: OnceLock<[f32; 256]> = OnceLock::new();
    LUT.get_or_init(|| std::array::from_fn(|value| srgb_eotf(value as f32 / 255.0)))
}

fn linear_to_srgb(value: f32) -> f32 {
    srgb_oetf(value.clamp(0.0, 1.0)) * 255.0
}

/// Averages an RGBA_8888 capture in linear light, like the CWB path does, and returns sRGB
/// encoded values in 0-255. `stride` is in pixels.
pub fn average_rgb(
    pixels: &[u8],
    width: u32,
    height: u32,
    stride: u32,
    weighting: Option<&Weighting>,
) -> Option<[f32; 3]> {
    if width == 0 || height == 0 || stride < width {
        return None;
    }
    let needed = (height as usize - 1)
        .checked_mul(stride as usize)?
        .checked_add(width as usize)?
        .checked_mul(4)?;
    if pixels.len() < needed {
        return None;
    }
    let lut = srgb_to_linear_lut();
    // Finite f32 weights can still overflow a f32 sum; accumulate in f64.
    let (mut r, mut g, mut b, mut weight_sum) = (0.0f64, 0.0f64, 0.0f64, 0.0f64);
    for y in 0..height {
        for x in 0..width {
            let weight = match weighting {
                Some(w) => *w.weights.get(panel_index(
                    w.rotation,
                    x,
                    y,
                    w.panel_width,
                    w.panel_height,
                )?)?,
                None => 1.0,
            };
            if !weight.is_finite() || weight < 0.0 {
                return None;
            }
            let weight = f64::from(weight);
            let offset = (y as usize * stride as usize + x as usize) * 4;
            let pixel = pixels.get(offset..offset + 3)?;
            r += f64::from(lut[usize::from(pixel[0])]) * weight;
            g += f64::from(lut[usize::from(pixel[1])]) * weight;
            b += f64::from(lut[usize::from(pixel[2])]) * weight;
            weight_sum += weight;
        }
    }
    if weight_sum <= 0.0 {
        return None;
    }
    Some([
        linear_to_srgb((r / weight_sum) as f32),
        linear_to_srgb((g / weight_sum) as f32),
        linear_to_srgb((b / weight_sum) as f32),
    ])
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rect(left: i32, top: i32, right: i32, bottom: i32) -> Rect {
        Rect {
            left,
            top,
            right,
            bottom,
        }
    }

    #[test]
    fn grab_rect() {
        assert_eq!(
            parse_grab_rect("1010 148 1048 190"),
            Some(rect(1010, 148, 1048, 190))
        );
        assert_eq!(parse_grab_rect("  1 2   3 4 extra"), Some(rect(1, 2, 3, 4)));
        assert_eq!(parse_grab_rect(""), None);
        assert_eq!(parse_grab_rect("1 2 3"), None);
        assert_eq!(parse_grab_rect("-1 2 3 4"), None);
        assert_eq!(parse_grab_rect("3 2 3 4"), None);
        assert_eq!(parse_grab_rect("1 4 3 4"), None);
        assert_eq!(parse_grab_rect("a b c d"), None);
    }

    #[test]
    fn weights() {
        assert_eq!(
            parse_weights(r#"{"Weights": [0.5, 1, 0]}"#, 3),
            Ok(vec![0.5, 1.0, 0.0])
        );
        assert!(parse_weights(r#"{"Weights": [1, 1]}"#, 3).is_err());
        assert!(parse_weights(r#"{"Weights": [1, -1, 1]}"#, 3).is_err());
        assert!(parse_weights(r#"{"Weights": [1, "x", 1]}"#, 3).is_err());
        assert!(parse_weights(r#"{"Other": [1, 1, 1]}"#, 3).is_err());
        assert!(parse_weights("not json", 3).is_err());
    }

    #[test]
    fn malformed_geometry_does_not_overflow() {
        assert_eq!(parse_grab_rect("2147483647 0 -2147483648 1"), None);
        assert_eq!(parse_grab_rect("0 2147483647 1 -2147483648"), None);
        let g = Geometry {
            panel_width: 100,
            panel_height: 200,
            layer_stack_width: 100,
            layer_stack_height: 200,
            rotation: 0,
        };
        assert_eq!(capture_crop(&rect(90, 0, 110, 20), &g), None);
        assert_eq!(capture_crop(&rect(1, 0, i32::MIN, 20), &g), None);
        assert!(parse_weights(r#"{"Weights": [1e100]}"#, 1).is_err());
    }

    #[test]
    fn invalid_buffers_and_extreme_weights() {
        let pixels = [255u8; 8];
        assert_eq!(average_rgb(&pixels, 2, 1, 1, None), None);
        assert_eq!(average_rgb(&pixels[..7], 2, 1, 2, None), None);
        assert_eq!(average_rgb(&pixels, 0, 1, 2, None), None);
        for weights in [[f32::NAN, 1.0], [f32::INFINITY, 1.0], [-1.0, 1.0]] {
            let w = Weighting {
                weights: &weights,
                rotation: 0,
                panel_width: 2,
                panel_height: 1,
            };
            assert_eq!(average_rgb(&pixels, 2, 1, 2, Some(&w)), None);
        }
        let w = Weighting {
            weights: &[f32::MAX, f32::MAX],
            rotation: 0,
            panel_width: 2,
            panel_height: 1,
        };
        for value in average_rgb(&pixels, 2, 1, 2, Some(&w)).unwrap() {
            assert!(value.is_finite() && (value - 255.0).abs() < 1e-3);
        }
    }

    #[test]
    fn crop_in_every_rotation() {
        // Exact binary fractions so float rounding cannot move the edges.
        let area = rect(16, 32, 48, 96);
        let geometry = |rotation, stack_w, stack_h| Geometry {
            panel_width: 128,
            panel_height: 256,
            layer_stack_width: stack_w,
            layer_stack_height: stack_h,
            rotation,
        };
        assert_eq!(
            capture_crop(&area, &geometry(0, 128, 256)),
            Some(rect(16, 32, 48, 96))
        );
        assert_eq!(
            capture_crop(&area, &geometry(1, 256, 128)),
            Some(rect(32, 80, 96, 112))
        );
        assert_eq!(
            capture_crop(&area, &geometry(2, 128, 256)),
            Some(rect(80, 160, 112, 224))
        );
        assert_eq!(
            capture_crop(&area, &geometry(3, 256, 128)),
            Some(rect(160, 16, 224, 48))
        );
        // Half resolution override.
        assert_eq!(
            capture_crop(&area, &geometry(0, 64, 128)),
            Some(rect(8, 16, 24, 48))
        );
        assert_eq!(capture_crop(&area, &geometry(0, 0, 256)), None);
    }

    #[test]
    fn panel_indices() {
        // Panel is 2 wide and 3 tall; rotated captures are 3 wide and 2 tall.
        assert_eq!(panel_index(0, 1, 2, 2, 3), Some(5));
        assert_eq!(panel_index(1, 0, 0, 2, 3), Some(1));
        assert_eq!(panel_index(2, 0, 0, 2, 3), Some(5));
        assert_eq!(panel_index(3, 0, 0, 2, 3), Some(4));
        assert_eq!(panel_index(0, 2, 0, 2, 3), None);
        assert_eq!(panel_index(1, 0, 2, 2, 3), None);
    }

    #[test]
    fn weights_follow_all_four_rotations() {
        // Panel 2x3: only its top-right pixel has nonzero weight. The corresponding
        // capture positions are explicit, independent of panel_index().
        for (rotation, width, height, red_index) in
            [(0, 2, 3, 1), (1, 3, 2, 0), (2, 2, 3, 4), (3, 3, 2, 5)]
        {
            let mut pixels = [0u8; 24];
            pixels[red_index * 4] = 255;
            let weighting = Weighting {
                weights: &[0.0, 1.0, 0.0, 0.0, 0.0, 0.0],
                rotation,
                panel_width: 2,
                panel_height: 3,
            };
            let rgb = average_rgb(&pixels, width, height, width, Some(&weighting)).unwrap();
            assert!((rgb[0] - 255.0).abs() < 1e-3);
            assert_eq!((rgb[1], rgb[2]), (0.0, 0.0));
        }
    }

    #[test]
    fn srgb_curves() {
        assert_eq!(srgb_eotf(0.0), 0.0);
        assert!((srgb_eotf(1.0) - 1.0).abs() < 1e-6);
        assert!((srgb_eotf(0.5) - 0.214_041).abs() < 1e-5);
        for value in [0.0f32, 0.01, 0.2, 0.5, 0.9, 1.0] {
            assert!((srgb_oetf(srgb_eotf(value)) - value).abs() < 1e-5);
        }
    }

    #[test]
    fn averaging() {
        // 2x1 capture with a stride of 4 pixels: one red, one black pixel.
        let mut pixels = vec![0u8; 4 * 4];
        pixels[..4].copy_from_slice(&[255, 0, 0, 255]);
        pixels[7] = 255;

        let [r, g, b] = average_rgb(&pixels, 2, 1, 4, None).unwrap();
        assert!((r - srgb_oetf(0.5) * 255.0).abs() < 1e-3);
        assert_eq!((g, b), (0.0, 0.0));

        let only_red = Weighting {
            weights: &[1.0, 0.0],
            rotation: 0,
            panel_width: 2,
            panel_height: 1,
        };
        let [r, _, _] = average_rgb(&pixels, 2, 1, 4, Some(&only_red)).unwrap();
        assert!((r - 255.0).abs() < 1e-3);

        let none = Weighting {
            weights: &[0.0, 0.0],
            rotation: 0,
            panel_width: 2,
            panel_height: 1,
        };
        assert_eq!(average_rgb(&pixels, 2, 1, 4, Some(&none)), None);
        assert_eq!(average_rgb(&pixels[..6], 2, 1, 4, None), None);
    }
}
