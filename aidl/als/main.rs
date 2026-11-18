/*
 * Copyright (C) 2021-2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

//! Screen area capture for the ALS correction: reports the average color of the screen above
//! the light sensor.

// The test build has no caller for main() and the service type.
#![cfg_attr(test, allow(dead_code))]

mod area;

use area::{average_rgb, capture_crop, parse_grab_rect, parse_weights, Geometry, Rect, Weighting};
use binder::{BinderFeatures, Interface, Status};
use log::{error, info, warn, LevelFilter};
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;
use vendor_lineage_oplus_als::aidl::vendor::lineage::oplus_als::{
    AreaRgbCaptureResult::AreaRgbCaptureResult,
    IAreaCapture::{BnAreaCapture, IAreaCapture},
};

const LOG_TAG: &str = "vendor.lineage.oplus_als.service";
const GRAB_RECT_PROP: &str = "vendor.sensors.als_correction.grabrect";
const WEIGHTS_PATH: &str = "/odm/etc/display/cwb_weightspos.json";

// capture.h
#[repr(C)]
struct AlsRect {
    left: i32,
    top: i32,
    right: i32,
    bottom: i32,
}

#[repr(C)]
#[derive(Default)]
struct AlsDisplayGeometry {
    panel_width: i32,
    panel_height: i32,
    layer_stack_width: i32,
    layer_stack_height: i32,
    rotation: i32,
}

type AlsPixelsCallback = unsafe extern "C" fn(*mut c_void, *const u8, u32, u32, u32);

extern "C" {
    fn als_get_display_geometry(out: *mut AlsDisplayGeometry) -> bool;
    fn als_capture(
        crop: *const AlsRect,
        width: i32,
        height: i32,
        callback: AlsPixelsCallback,
        context: *mut c_void,
    ) -> bool;
}

fn display_geometry() -> Option<Geometry> {
    let mut g = AlsDisplayGeometry::default();
    // SAFETY: `g` is a valid, writable AlsDisplayGeometry.
    if !unsafe { als_get_display_geometry(&mut g) } {
        return None;
    }
    Some(Geometry {
        panel_width: g.panel_width,
        panel_height: g.panel_height,
        layer_stack_width: g.layer_stack_width,
        layer_stack_height: g.layer_stack_height,
        rotation: g.rotation,
    })
}

struct CaptureJob<'a> {
    width: i32,
    height: i32,
    weighting: Option<Weighting<'a>>,
    result: Option<[f32; 3]>,
}

unsafe extern "C" fn on_pixels(
    context: *mut c_void,
    pixels: *const u8,
    width: u32,
    height: u32,
    stride: u32,
) {
    // SAFETY: `context` is the CaptureJob that capture() passes to als_capture, which only
    // calls back before returning.
    let job = unsafe { &mut *context.cast::<CaptureJob>() };
    let len = (stride as usize)
        .checked_mul(height as usize)
        .and_then(|n| n.checked_mul(4));
    let (Some(len), false) = (
        len,
        pixels.is_null() || stride < width || len.is_some_and(|n| n > isize::MAX as usize),
    ) else {
        return;
    };
    // SAFETY: the shim passes the locked RGBA_8888 buffer, which holds stride * height pixels
    // and stays mapped until this callback returns.
    let pixels = unsafe { std::slice::from_raw_parts(pixels, len) };

    // Weights apply only if the capture came back at the requested one pixel per panel pixel.
    let weighting = job
        .weighting
        .as_ref()
        .filter(|_| i64::from(width) == i64::from(job.width))
        .filter(|_| i64::from(height) == i64::from(job.height));
    // Never unwind into C++.
    job.result = catch_unwind(AssertUnwindSafe(|| {
        average_rgb(pixels, width, height, stride, weighting)
    }))
    .ok()
    .flatten();
}

fn capture(crop: &Rect, mut job: CaptureJob) -> Option<[f32; 3]> {
    let crop = AlsRect {
        left: crop.left,
        top: crop.top,
        right: crop.right,
        bottom: crop.bottom,
    };
    // SAFETY: `crop` and `job` outlive the call, and on_pixels matches AlsPixelsCallback.
    let captured = unsafe {
        als_capture(
            &crop,
            job.width,
            job.height,
            on_pixels,
            (&mut job as *mut CaptureJob).cast::<c_void>(),
        )
    };
    if captured {
        job.result
    } else {
        None
    }
}

fn load_weights(rect: &Rect) -> Vec<f32> {
    let Ok(json) = std::fs::read_to_string(WEIGHTS_PATH) else {
        return Vec::new();
    };
    let count = (rect.width() as usize) * (rect.height() as usize);
    match parse_weights(&json, count) {
        Ok(weights) => {
            info!("Loaded {} grab area weights", weights.len());
            weights
        }
        Err(e) => {
            warn!("{WEIGHTS_PATH} {e}; using unweighted sampling");
            Vec::new()
        }
    }
}

struct AreaCapture {
    capture_lock: Mutex<()>,
    rect: Option<Rect>,
    // Per-pixel weights of the grab area in panel orientation, row major.
    weights: Vec<f32>,
}

impl AreaCapture {
    fn new() -> Self {
        let value = rustutils::android::system_properties::read(GRAB_RECT_PROP)
            .ok()
            .flatten();
        let Some(rect) = value.as_deref().and_then(parse_grab_rect) else {
            error!("No valid screenshot grab area config");
            return Self {
                capture_lock: Mutex::new(()),
                rect: None,
                weights: Vec::new(),
            };
        };
        info!(
            "Screenshot grab area: {} {} {} {}",
            rect.left, rect.top, rect.right, rect.bottom
        );
        Self {
            capture_lock: Mutex::new(()),
            rect: Some(rect),
            weights: load_weights(&rect),
        }
    }
}

fn failed() -> Status {
    Status::new_service_specific_error(-1, None)
}

impl Interface for AreaCapture {}

#[allow(non_snake_case)]
impl IAreaCapture for AreaCapture {
    fn getAreaBrightness(&self) -> binder::Result<AreaRgbCaptureResult> {
        // Never occupy another Binder thread waiting behind an in-flight capture:
        // screenshot completion must be able to enter this same process.
        let _capture = self.capture_lock.try_lock().map_err(|_| failed())?;
        let Some(rect) = self.rect else {
            return Err(failed());
        };
        let Some((geometry, crop)) =
            display_geometry().and_then(|g| Some((g, capture_crop(&rect, &g)?)))
        else {
            error!("Unable to map the grab area to the display");
            return Err(failed());
        };

        // Weighted captures are scaled back to one pixel per panel pixel, in layer stack
        // orientation.
        let transposed = matches!(geometry.rotation, 1 | 3);
        let (width, height) = match (self.weights.is_empty(), transposed) {
            (true, _) => (crop.width(), crop.height()),
            (false, false) => (rect.width(), rect.height()),
            (false, true) => (rect.height(), rect.width()),
        };
        let weighting = (!self.weights.is_empty()).then(|| Weighting {
            weights: &self.weights,
            rotation: geometry.rotation,
            panel_width: rect.width(),
            panel_height: rect.height(),
        });

        let [r, g, b] = capture(
            &crop,
            CaptureJob {
                width,
                height,
                weighting,
                result: None,
            },
        )
        .ok_or_else(failed)?;
        Ok(AreaRgbCaptureResult { r, g, b })
    }
}

fn main() {
    logger::init(
        logger::Config::default()
            .with_tag_on_device(LOG_TAG)
            .with_max_level(LevelFilter::Info),
    );

    // Capture completion uses the same Binder pool. Leave callback capacity when the
    // sensor HAL and diagnostic clients request samples concurrently. The shim also
    // bounds callback/fence waits so saturation cannot hang the service permanently.
    binder::ProcessState::set_thread_pool_max_thread_count(4);
    binder::ProcessState::start_thread_pool();

    let service = BnAreaCapture::new_binder(AreaCapture::new(), BinderFeatures::default());
    let instance = format!(
        "{}/default",
        <AreaCapture as IAreaCapture>::get_descriptor()
    );
    binder::add_service(&instance, service.as_binder()).expect("Failed to register service");

    binder::ProcessState::join_thread_pool();
}

#[cfg(test)]
mod callback_tests {
    use super::*;

    #[test]
    fn callback_validates_layout_and_returns_rgb() {
        let mut job = CaptureJob {
            width: 2,
            height: 1,
            weighting: None,
            result: None,
        };
        let context = (&mut job as *mut CaptureJob).cast::<c_void>();
        let pixels = [255u8; 8];
        // SAFETY: context and pixels stay valid for each synchronous call.
        unsafe { on_pixels(context, pixels.as_ptr(), 2, 1, 1) };
        assert!(job.result.is_none());
        unsafe { on_pixels(context, std::ptr::null(), 2, 1, 2) };
        assert!(job.result.is_none());
        unsafe { on_pixels(context, pixels.as_ptr(), 2, 1, 2) };
        for value in job.result.unwrap() {
            assert!((value - 255.0).abs() < 1e-3);
        }
    }
}
