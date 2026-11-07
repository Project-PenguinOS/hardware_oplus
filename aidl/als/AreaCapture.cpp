/*
 * Copyright (C) 2021-2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "AreaCapture.h"

#include <android-base/properties.h>
#include <android/gui/SecureLayerMode.h>
#include <json/reader.h>
#include <gui/AidlUtil.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/SyncScreenCaptureListener.h>
#include <ui/ColorSpace.h>
#include <ui/DisplayState.h>
#include <ui/DynamicDisplayInfo.h>
#include <ui/PixelFormat.h>
#include <ui/StaticDisplayInfo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

using android::ColorSpace;
using android::DisplayCaptureArgs;
using android::GraphicBuffer;
using android::IBinder;
using android::NO_ERROR;
using android::PhysicalDisplayId;
using android::Rect;
using android::ScreenshotClient;
using android::sp;
using android::SurfaceComposerClient;
using android::SyncScreenCaptureListener;
using android::base::GetProperty;
using android::gui::ScreenCaptureResults;
using android::gui::SecureLayerMode;
using android::gui::aidl_utils::toARect;
using android::ui::PixelFormat;

namespace ui = ::android::ui;

namespace aidl {
namespace vendor {
namespace lineage {
namespace oplus_als {

namespace {

constexpr char kWeightsPath[] = "/odm/etc/display/cwb_weightspos.json";

const std::array<float, 256>& GetSrgbToLinearLut() {
    static const std::array<float, 256> lut = [] {
        std::array<float, 256> values;
        const auto decode = ColorSpace::sRGB().getEOTF();
        for (size_t value = 0; value < values.size(); ++value) {
            values[value] = decode(value / 255.0f);
        }
        return values;
    }();
    return lut;
}

float LinearToSrgb(float value) {
    return ColorSpace::sRGB().getOETF()(std::clamp(value, 0.0f, 1.0f)) * 255.0f;
}

// Stock CWB weights for the grab area, used only when they cover exactly that many pixels.
std::vector<float> LoadWeights(const Rect& rect) {
    std::ifstream stream(kWeightsPath);
    if (!stream.is_open()) {
        return {};
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string error;
    const auto count = static_cast<Json::ArrayIndex>(rect.getWidth() * rect.getHeight());
    if (!Json::parseFromStream(builder, stream, &root, &error) || !root["Weights"].isArray() ||
        root["Weights"].size() != count) {
        ALOGW("%s does not match the grab area; using unweighted sampling", kWeightsPath);
        return {};
    }

    std::vector<float> weights;
    weights.reserve(count);
    for (const auto& value : root["Weights"]) {
        if (!value.isNumeric() || value.asDouble() < 0) {
            ALOGW("Invalid weight in %s; using unweighted sampling", kWeightsPath);
            return {};
        }
        weights.push_back(value.asFloat());
    }
    ALOGI("Loaded %zu grab area weights", weights.size());
    return weights;
}

}  // namespace

AreaCapture::AreaCapture() {
    int left = 0, top = 0, right = 0, bottom = 0;
    std::istringstream is(GetProperty("vendor.sensors.als_correction.grabrect", ""));

    is >> left >> top >> right >> bottom;
    if (is.fail() || left < 0 || top < 0 || right <= left || bottom <= top) {
        ALOGE("No valid screenshot grab area config");
        return;
    }

    ALOGI("Screenshot grab area: %d %d %d %d", left, top, right, bottom);
    m_screenshot_rect = Rect(left, top, right, bottom);
    m_weights = LoadWeights(m_screenshot_rect);
}

// The grab area is in panel pixels of the largest display mode. captureDisplay crops in layer
// stack space, which follows the display rotation and any resolution or size override.
std::optional<AreaCapture::CaptureArea> AreaCapture::getCaptureArea(
        PhysicalDisplayId displayId, const sp<IBinder>& displayToken) const {
    ui::StaticDisplayInfo staticInfo;
    ui::DynamicDisplayInfo dynamicInfo;
    ui::DisplayState state;
    if (SurfaceComposerClient::getStaticDisplayInfo(displayId.value, &staticInfo) != NO_ERROR ||
        SurfaceComposerClient::getDynamicDisplayInfoFromId(displayId.value, &dynamicInfo) !=
                NO_ERROR ||
        SurfaceComposerClient::getDisplayState(displayToken, &state) != NO_ERROR) {
        return std::nullopt;
    }

    ui::Size panel;
    for (const auto& mode : dynamicInfo.supportedDisplayModes) {
        if (mode.resolution.width * mode.resolution.height > panel.width * panel.height) {
            panel = mode.resolution;
        }
    }
    const auto& layerStack = state.layerStackSpaceRect;
    if (panel.width <= 0 || panel.height <= 0 || layerStack.width <= 0 ||
        layerStack.height <= 0) {
        return std::nullopt;
    }

    const float l = static_cast<float>(m_screenshot_rect.left) / panel.width;
    const float t = static_cast<float>(m_screenshot_rect.top) / panel.height;
    const float r = static_cast<float>(m_screenshot_rect.right) / panel.width;
    const float b = static_cast<float>(m_screenshot_rect.bottom) / panel.height;
    const auto rotation = state.orientation + staticInfo.installOrientation;
    float left, top, right, bottom;
    switch (rotation) {
        case ui::ROTATION_90:
            left = t, top = 1.0f - r, right = b, bottom = 1.0f - l;
            break;
        case ui::ROTATION_180:
            left = 1.0f - r, top = 1.0f - b, right = 1.0f - l, bottom = 1.0f - t;
            break;
        case ui::ROTATION_270:
            left = 1.0f - b, top = l, right = 1.0f - t, bottom = r;
            break;
        default:
            left = l, top = t, right = r, bottom = b;
            break;
    }

    const Rect crop(std::max(0, static_cast<int32_t>(std::floor(left * layerStack.width))),
                    std::max(0, static_cast<int32_t>(std::floor(top * layerStack.height))),
                    std::min(layerStack.width,
                             static_cast<int32_t>(std::ceil(right * layerStack.width))),
                    std::min(layerStack.height,
                             static_cast<int32_t>(std::ceil(bottom * layerStack.height))));
    if (crop.isEmpty()) {
        return std::nullopt;
    }
    return CaptureArea{crop, rotation};
}

ndk::ScopedAStatus AreaCapture::getAreaBrightness(AreaRgbCaptureResult* _aidl_return) {
    if (m_screenshot_rect.isEmpty()) {
        return ndk::ScopedAStatus::fromServiceSpecificError(-1);
    }

    // See frameworks/base/services/core/jni/com_android_server_display_DisplayControl.cpp and
    // frameworks/base/core/java/android/view/SurfaceControl.java
    const auto displayIds = SurfaceComposerClient::getPhysicalDisplayIds();
    if (displayIds.empty()) {
        ALOGE("No physical display");
        return ndk::ScopedAStatus::fromServiceSpecificError(-1);
    }
    const auto displayToken = SurfaceComposerClient::getPhysicalDisplayToken(displayIds[0]);
    if (displayToken == nullptr) {
        ALOGE("No display token");
        return ndk::ScopedAStatus::fromServiceSpecificError(-1);
    }
    const auto area = getCaptureArea(displayIds[0], displayToken);
    if (!area.has_value()) {
        ALOGE("Unable to map the grab area to the display");
        return ndk::ScopedAStatus::fromServiceSpecificError(-1);
    }

    // Weighted captures are scaled back to one pixel per panel pixel, in layer stack orientation.
    const int32_t panelWidth = m_screenshot_rect.getWidth();
    const int32_t panelHeight = m_screenshot_rect.getHeight();
    const bool transposed = area->rotation == ui::ROTATION_90 ||
                            area->rotation == ui::ROTATION_270;
    const int32_t captureWidth =
            m_weights.empty() ? area->crop.getWidth() : (transposed ? panelHeight : panelWidth);
    const int32_t captureHeight =
            m_weights.empty() ? area->crop.getHeight() : (transposed ? panelWidth : panelHeight);

    DisplayCaptureArgs displayCaptureArgs;
    displayCaptureArgs.displayToken = displayToken;
    displayCaptureArgs.captureArgs.pixelFormat = ::android::PIXEL_FORMAT_RGBA_8888;
    displayCaptureArgs.captureArgs.sourceCrop = toARect(area->crop);
    displayCaptureArgs.width = captureWidth;
    displayCaptureArgs.height = captureHeight;
    displayCaptureArgs.captureArgs.secureLayerMode = SecureLayerMode::Capture;

    sp<SyncScreenCaptureListener> captureListener = new SyncScreenCaptureListener();
    if (ScreenshotClient::captureDisplay(displayCaptureArgs, captureListener) != NO_ERROR) {
        ALOGE("Capture failed");
        return ndk::ScopedAStatus::fromServiceSpecificError(-1);
    }

    auto captureResults = captureListener->waitForResults();
    if (!captureResults.fenceResult.ok() || captureResults.buffer == nullptr) {
        ALOGE("Fence result error");
        return ndk::ScopedAStatus::fromServiceSpecificError(-1);
    }

    uint8_t* out = nullptr;
    if (captureResults.buffer->lock(GraphicBuffer::USAGE_SW_READ_OFTEN,
                                    reinterpret_cast<void**>(&out)) != NO_ERROR ||
        out == nullptr) {
        ALOGE("Unable to map the capture buffer");
        return ndk::ScopedAStatus::fromServiceSpecificError(-1);
    }

    const auto resultWidth = captureResults.buffer->getWidth();
    const auto resultHeight = captureResults.buffer->getHeight();
    const auto stride = captureResults.buffer->getStride();

    const bool weighted = !m_weights.empty() && resultWidth == static_cast<uint32_t>(captureWidth) &&
                          resultHeight == static_cast<uint32_t>(captureHeight);

    // The capture is sRGB encoded; average in linear light like the CWB path does.
    const auto& lut = GetSrgbToLinearLut();
    float rsum = 0.0f, gsum = 0.0f, bsum = 0.0f, weightSum = 0.0f;
    for (uint32_t y = 0; y < resultHeight; y++) {
        for (uint32_t x = 0; x < resultWidth; x++) {
            float weight = 1.0f;
            if (weighted) {
                int32_t panelX, panelY;
                switch (area->rotation) {
                    case ui::ROTATION_90:
                        panelX = panelWidth - 1 - y, panelY = x;
                        break;
                    case ui::ROTATION_180:
                        panelX = panelWidth - 1 - x, panelY = panelHeight - 1 - y;
                        break;
                    case ui::ROTATION_270:
                        panelX = y, panelY = panelHeight - 1 - x;
                        break;
                    default:
                        panelX = x, panelY = y;
                        break;
                }
                weight = m_weights[panelY * panelWidth + panelX];
            }
            const uint8_t* pixel = out + (y * stride + x) * 4;
            rsum += lut[pixel[0]] * weight;
            gsum += lut[pixel[1]] * weight;
            bsum += lut[pixel[2]] * weight;
            weightSum += weight;
        }
    }
    captureResults.buffer->unlock();

    if (weightSum <= 0.0f) {
        return ndk::ScopedAStatus::fromServiceSpecificError(-1);
    }
    _aidl_return->r = LinearToSrgb(rsum / weightSum);
    _aidl_return->g = LinearToSrgb(gsum / weightSum);
    _aidl_return->b = LinearToSrgb(bsum / weightSum);

    return ndk::ScopedAStatus::ok();
}

}  // namespace oplus_als
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
