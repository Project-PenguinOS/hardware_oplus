/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <android-base/logging.h>
#include <android/binder_auto_utils.h>
#include <android/binder_manager.h>
#include <Eigen/Core>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace subhal {
namespace implementation {
namespace fusionlight {

constexpr int kChannelCount = 4;
constexpr int kIrBandCount = 3;
constexpr int kLeakageTermCount = 35;
constexpr int kRatioTermCount = 20;

using Channels = Eigen::Matrix<double, kChannelCount, 1>;
using ChannelLimits = Eigen::Matrix<int, kChannelCount, 1>;
using Polynomial = Eigen::Vector4d;
using LeakageModel = Eigen::Matrix<double, kLeakageTermCount, 1>;
using RatioModel = Eigen::Matrix<double, kRatioTermCount, 1>;
using GreyScale = Eigen::Vector3d;
// Rows are sensor channels, columns are the R/G/B/W screen views.
using ViewCalibration = Eigen::Matrix4d;

struct ValueRange {
    double min = 0.0;
    double max = 0.0;
};

struct CwbConfig {
    int32_t crop_left = 0;
    int32_t crop_top = 0;
    int32_t crop_right = 0;
    int32_t crop_bottom = 0;
    int32_t reference_width = 0;
    int32_t reference_height = 0;
    bool screenshot_weighted = false;
    std::chrono::milliseconds screenshot_period{0};
    std::vector<int32_t> weights;
};

// Pre-V2.1 model for one FusionLightBrightnesses segment.
struct ViewSegment {
    std::vector<ValueRange> ir_brightness;
    std::array<std::vector<Channels>, kIrBandCount> lux_coefficients;
    // Indexed by sensor channel, then by R/G/B/W screen view.
    std::array<Channels, kChannelCount> golden;
    std::array<GreyScale, kChannelCount> grey_scale;
    std::array<std::array<Polynomial, kChannelCount>, kChannelCount> leakage;
};

struct FusionConfig {
    int32_t sensor_module_id = 0;
    int32_t ir_ratio_formula = 0;
    double low_light_accuracy = 0.0;

    bool fusion_rgb_supported = false;
    bool cwb_supported = false;
    bool black_frame_supported = false;
    bool screenshot_v2_1_supported = false;
    bool screen_off_lux_supported = false;
    bool channel_count_policy_supported = false;
    bool fod_linearity_segment_supported = false;

    CwbConfig cwb;

    std::vector<ValueRange> linearity_brightness;
    std::vector<ValueRange> ir_brightness;
    std::vector<ValueRange> ir_thresholds;
    std::vector<std::array<Polynomial, kChannelCount>> linearity;
    std::vector<std::array<LeakageModel, kChannelCount>> leakage;
    std::vector<std::array<RatioModel, kChannelCount>> leakage_ratio;
    std::vector<Channels> leakage_golden;

    std::array<std::vector<Channels>, kIrBandCount> lux_coefficients;
    std::array<std::vector<Channels>, kIrBandCount> channel_count_coefficients;
    std::array<std::vector<Channels>, kIrBandCount> screen_off_coefficients;
    std::vector<ChannelLimits> channel_thresholds;
    std::vector<double> c_zero_thresholds;

    std::vector<int32_t> segment_brightness;
    std::vector<ViewSegment> segments;
    std::vector<std::array<Polynomial, kChannelCount>> fod_linearity;
    Channels fod_linearity_constant = Channels::Zero();
};

std::optional<FusionConfig> LoadConfig(const std::string& sensor_name);
std::vector<Channels> LoadCalibration(const FusionConfig& config);
std::vector<ViewCalibration> LoadViewCalibration(const FusionConfig& config);

template <typename Interface>
std::shared_ptr<Interface> GetService() {
    const std::string instance = std::string(Interface::descriptor) + "/default";
    ndk::SpAIBinder binder(AServiceManager_checkService(instance.c_str()));
    return binder.get() == nullptr ? nullptr : Interface::fromBinder(binder);
}

// For one-shot calls to services that may be lazy (oneshot and stopped after use). Only
// waitForService makes servicemanager start them; checkService and getService just poll.
// waitForService never gives up, so it runs on a helper thread that is left behind if the
// service has not come up within kServiceStartTimeout.
constexpr std::chrono::seconds kServiceStartTimeout{5};

template <typename Interface>
std::shared_ptr<Interface> GetDeclaredService() {
    const std::string instance = std::string(Interface::descriptor) + "/default";
    if (!AServiceManager_isDeclared(instance.c_str())) {
        return nullptr;
    }
    auto result = std::make_shared<std::promise<ndk::SpAIBinder>>();
    auto binder_future = result->get_future();
    std::thread([instance, result] {
        result->set_value(ndk::SpAIBinder(AServiceManager_waitForService(instance.c_str())));
    }).detach();
    if (binder_future.wait_for(kServiceStartTimeout) != std::future_status::ready) {
        LOG(ERROR) << instance << " did not start within " << kServiceStartTimeout.count() << " s";
        return nullptr;
    }
    ndk::SpAIBinder binder = binder_future.get();
    return binder.get() == nullptr ? nullptr : Interface::fromBinder(binder);
}

}  // namespace fusionlight
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
