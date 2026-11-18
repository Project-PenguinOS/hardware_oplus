/*
 * Copyright (C) 2021-2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// libgui has no Rust bindings, so the display queries and the screen capture live here behind a
// C ABI. Everything else (grab area, weights, crop mapping, averaging) is in Rust.

#include "capture.h"
#include "capture_listener.h"

#include <android/gui/SecureLayerMode.h>
#include <gui/AidlUtil.h>
#include <gui/SurfaceComposerClient.h>
#include <log/log.h>
#include <ui/DisplayState.h>
#include <ui/DynamicDisplayInfo.h>
#include <ui/PixelFormat.h>
#include <ui/StaticDisplayInfo.h>

using android::DisplayCaptureArgs;
using android::GraphicBuffer;
using android::IBinder;
using android::NO_ERROR;
using android::PhysicalDisplayId;
using android::Rect;
using android::ScreenshotClient;
using android::sp;
using android::SurfaceComposerClient;
using android::gui::SecureLayerMode;
using android::gui::aidl_utils::toARect;

namespace ui = ::android::ui;

namespace {

// See frameworks/base/services/core/jni/com_android_server_display_DisplayControl.cpp and
// frameworks/base/core/java/android/view/SurfaceControl.java
bool GetPrimaryDisplay(PhysicalDisplayId* id, sp<IBinder>* token) {
    const auto displayIds = SurfaceComposerClient::getPhysicalDisplayIds();
    if (displayIds.empty()) {
        ALOGE("No physical display");
        return false;
    }
    *id = displayIds[0];
    *token = SurfaceComposerClient::getPhysicalDisplayToken(*id);
    if (*token == nullptr) {
        ALOGE("No display token");
        return false;
    }
    return true;
}

}  // namespace

extern "C" bool als_get_display_geometry(AlsDisplayGeometry* out) {
    PhysicalDisplayId displayId;
    sp<IBinder> displayToken;
    if (!GetPrimaryDisplay(&displayId, &displayToken)) {
        return false;
    }

    ui::StaticDisplayInfo staticInfo;
    ui::DynamicDisplayInfo dynamicInfo;
    ui::DisplayState state;
    if (SurfaceComposerClient::getStaticDisplayInfo(displayId.value, &staticInfo) != NO_ERROR ||
        SurfaceComposerClient::getDynamicDisplayInfoFromId(displayId.value, &dynamicInfo) !=
                NO_ERROR ||
        SurfaceComposerClient::getDisplayState(displayToken, &state) != NO_ERROR) {
        return false;
    }

    ui::Size panel;
    for (const auto& mode : dynamicInfo.supportedDisplayModes) {
        if (mode.resolution.width * mode.resolution.height > panel.width * panel.height) {
            panel = mode.resolution;
        }
    }

    out->panel_width = panel.width;
    out->panel_height = panel.height;
    out->layer_stack_width = state.layerStackSpaceRect.width;
    out->layer_stack_height = state.layerStackSpaceRect.height;
    // ui::Rotation addition wraps around.
    out->rotation = static_cast<int32_t>(state.orientation + staticInfo.installOrientation);
    return true;
}

extern "C" bool als_capture(const AlsRect* crop, int32_t width, int32_t height,
                            AlsPixelsCallback callback, void* context) {
    PhysicalDisplayId displayId;
    sp<IBinder> displayToken;
    if (!GetPrimaryDisplay(&displayId, &displayToken)) {
        return false;
    }

    DisplayCaptureArgs displayCaptureArgs;
    displayCaptureArgs.displayToken = displayToken;
    displayCaptureArgs.captureArgs.pixelFormat = ::android::PIXEL_FORMAT_RGBA_8888;
    displayCaptureArgs.captureArgs.sourceCrop =
            toARect(Rect(crop->left, crop->top, crop->right, crop->bottom));
    displayCaptureArgs.width = width;
    displayCaptureArgs.height = height;
    displayCaptureArgs.captureArgs.secureLayerMode = SecureLayerMode::Capture;

    sp<AlsCaptureListener> captureListener = new AlsCaptureListener();
    if (ScreenshotClient::captureDisplay(displayCaptureArgs, captureListener) != NO_ERROR) {
        ALOGE("Capture failed");
        return false;
    }

    android::gui::ScreenCaptureResults captureResults;
    if (!captureListener->waitForResults(&captureResults)) {
        ALOGE("Timed out waiting for capture callback");
        return false;
    }
    if (!captureResults.fenceResult.ok() || captureResults.buffer == nullptr) {
        ALOGE("Fence result error");
        return false;
    }

    if (captureResults.fenceResult.value()->wait(2000) != NO_ERROR) {
        ALOGE("Timed out waiting for capture fence");
        return false;
    }

    uint8_t* pixels = nullptr;
    if (captureResults.buffer->lock(GraphicBuffer::USAGE_SW_READ_OFTEN,
                                    reinterpret_cast<void**>(&pixels)) != NO_ERROR ||
        pixels == nullptr) {
        ALOGE("Unable to map the capture buffer");
        return false;
    }
    callback(context, pixels, captureResults.buffer->getWidth(),
             captureResults.buffer->getHeight(), captureResults.buffer->getStride());
    captureResults.buffer->unlock();
    return true;
}
