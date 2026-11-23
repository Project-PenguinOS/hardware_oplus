/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "StockLuxSensor.h"

#include <time.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include <android-base/logging.h>
#include <android-base/properties.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace subhal {
namespace implementation {
namespace fusionlight {
namespace {

using namespace std::chrono_literals;
using stocklux::Millis;

// Published by OplusBrightnessBridge.
constexpr auto kPropWifi = "sys.penguin.brightness.wifi";
constexpr auto kPropDarkMode = "sys.penguin.brightness.dark_mode";
constexpr auto kPropGameMode = "sys.penguin.brightness.game_mode";
constexpr auto kPropTorch = "sys.penguin.brightness.torch";
constexpr auto kPropAutoMode = "sys.penguin.brightness.auto";
constexpr auto kPropDragging = "sys.penguin.brightness.dragging";
constexpr auto kPropUtcOffset = "sys.penguin.brightness.utc_offset";
constexpr auto kPropDebug = "persist.vendor.sensors.stocklux.debug";

constexpr auto kTickPeriod = 250ms;

int64_t BoottimeNs() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

Millis NowMs() {
    return BoottimeNs() / 1000000LL;
}

// Stock AllSceneLuxSmallChange.
constexpr auto kPropAllScene = "persist.vendor.sensors.stocklux.all_scene";

stocklux::Features MakeFeatures() {
    stocklux::Features features;
    features.all_scene_lux_small_change = android::base::GetBoolProperty(kPropAllScene, true);
    return features;
}

std::optional<bool> ReadOptionalBool(const char* name) {
    const std::string value = android::base::GetProperty(name, "");
    if (value == "1" || value == "true") return true;
    if (value == "0" || value == "false") return false;
    return std::nullopt;
}

}  // namespace

StockLuxSensor::StockLuxSensor(EmitCallback emit)
    : emit_(std::move(emit)),
      pipeline_(MakeFeatures(), [this](float lux) {
          if (std::isnan(lux)) {
              return;
          }
          // Darkest mode reports -1.
          pending_.emplace_back(BoottimeNs(), std::max(lux, 0.0f));
      }) {
    thread_ = std::thread(&StockLuxSensor::tickLoop, this);
}

StockLuxSensor::~StockLuxSensor() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

StockLuxSensor::Props StockLuxSensor::ReadProps() {
    Props props;
    props.wifi = ReadOptionalBool(kPropWifi);
    props.dark_mode = ReadOptionalBool(kPropDarkMode);
    props.game_mode = ReadOptionalBool(kPropGameMode).value_or(false);
    props.torch = ReadOptionalBool(kPropTorch).value_or(false);
    props.auto_mode = ReadOptionalBool(kPropAutoMode).value_or(true);
    props.dragging = ReadOptionalBool(kPropDragging).value_or(false);
    const std::string offset = android::base::GetProperty(kPropUtcOffset, "");
    if (offset.empty()) {
        props.utc_offset_s = INT64_MIN;
    } else {
        props.utc_offset_s = android::base::GetIntProperty<int64_t>(kPropUtcOffset, 0,
                                                                    -16 * 3600, 16 * 3600);
    }
    return props;
}

// HHMMSS local time.
int StockLuxSensor::NormalizationTime(int64_t utc_offset_s) {
    const time_t now = time(nullptr);
    tm local{};
    if (utc_offset_s == INT64_MIN) {
        localtime_r(&now, &local);
    } else {
        const time_t shifted = now + static_cast<time_t>(utc_offset_s);
        gmtime_r(&shifted, &local);
    }
    return local.tm_hour * 10000 + local.tm_min * 100 + local.tm_sec;
}

void StockLuxSensor::applyPropsLocked(Millis now, const Props& props) {
    const bool first = !props_.has_value();
    const Props old = first ? Props() : *props_;
    if (props.wifi.has_value() && (first || props.wifi != old.wifi)) {
        pipeline_.onWifi(now, *props.wifi);
    }
    if (props.dark_mode.has_value() && (first || props.dark_mode != old.dark_mode)) {
        pipeline_.onDarkMode(now, *props.dark_mode);
    }
    if (first || props.torch != old.torch) {
        pipeline_.onTorch(now, props.torch);
    }
    pipeline_.setGameMode(props.game_mode);
    pipeline_.setDragging(props.dragging);
    if (!first && props.utc_offset_s != old.utc_offset_s) {
        last_minute_ = -1;
    }
    if (debug_ && !first &&
        (props.wifi != old.wifi || props.dark_mode != old.dark_mode ||
         props.game_mode != old.game_mode || props.torch != old.torch ||
         props.dragging != old.dragging)) {
        LOG(INFO) << "StockLux state wifi=" << props.wifi.value_or(false)
                  << " dark=" << props.dark_mode.value_or(false) << " game=" << props.game_mode
                  << " torch=" << props.torch << " drag=" << props.dragging;
    }
    props_ = props;
}

void StockLuxSensor::applyTimeLocked(Millis now) {
    const int hhmmss = NormalizationTime(props_.has_value() ? props_->utc_offset_s : INT64_MIN);
    const int minute = hhmmss / 100;
    if (minute != last_minute_) {
        last_minute_ = minute;
        LOG_IF(INFO, debug_) << "StockLux in " << now << " time " << hhmmss;
        pipeline_.onTime(now, hhmmss);
    }
}

void StockLuxSensor::flushOutputLocked(std::vector<std::pair<int64_t, float>>& out) {
    out.swap(pending_);
    pending_.clear();
    if (debug_) {
        for (const auto& [timestamp, lux] : out) {
            LOG(INFO) << "StockLux report " << timestamp / 1000000 << " " << lux << " scene "
                      << static_cast<int>(pipeline_.scene());
        }
    }
}

void StockLuxSensor::emit(std::vector<std::pair<int64_t, float>>& out) {
    for (const auto& [timestamp, lux] : out) {
        emit_(timestamp, lux);
    }
}

void StockLuxSensor::setEnabled(bool enabled) {
    const Props props = ReadProps();
    std::vector<std::pair<int64_t, float>> out;
    {
        std::lock_guard lock(mutex_);
        if (enabled == enabled_) {
            return;
        }
        enabled_ = enabled;
        const Millis now = NowMs();
        if (enabled) {
            debug_ = android::base::GetBoolProperty(kPropDebug, false);
            props_.reset();
            last_minute_ = -1;
            pipeline_.setActive(now, true, true);
            applyPropsLocked(now, props);
            applyTimeLocked(now);
        } else if (!props.auto_mode) {
            pipeline_.setActive(now, true, false);
        } else {
            pipeline_.setActive(now, false, true);
        }
        LOG(INFO) << "StockLux " << (enabled ? "enabled" : "disabled")
                  << " auto=" << props.auto_mode;
        flushOutputLocked(out);
    }
    cv_.notify_all();
    emit(out);
}

void StockLuxSensor::onMainLux(float lux) {
    std::vector<std::pair<int64_t, float>> out;
    {
        std::lock_guard lock(mutex_);
        if (!enabled_ || !std::isfinite(lux)) {
            return;
        }
        const Millis now = NowMs();
        LOG_IF(INFO, debug_) << "StockLux in " << now << " main " << lux;
        pipeline_.onMainLux(now, lux);
        flushOutputLocked(out);
    }
    emit(out);
}

void StockLuxSensor::onAuxLux(float lux) {
    std::vector<std::pair<int64_t, float>> out;
    {
        std::lock_guard lock(mutex_);
        if (!enabled_ || !std::isfinite(lux)) {
            return;
        }
        const Millis now = NowMs();
        LOG_IF(INFO, debug_) << "StockLux in " << now << " aux " << lux;
        pipeline_.onAuxLux(now, lux);
        flushOutputLocked(out);
    }
    emit(out);
}

void StockLuxSensor::onProximity(float distance) {
    std::vector<std::pair<int64_t, float>> out;
    {
        std::lock_guard lock(mutex_);
        if (!enabled_) {
            return;
        }
        const Millis now = NowMs();
        LOG_IF(INFO, debug_) << "StockLux in " << now << " prox " << distance;
        pipeline_.onProximity(now, distance == 0.0f);
        flushOutputLocked(out);
    }
    emit(out);
}

void StockLuxSensor::tickLoop() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        if (!enabled_) {
            cv_.wait(lock, [this] { return stopping_ || enabled_; });
            continue;
        }
        cv_.wait_for(lock, kTickPeriod, [this] { return stopping_ || !enabled_; });
        if (stopping_ || !enabled_) {
            continue;
        }
        lock.unlock();
        const Props props = ReadProps();
        lock.lock();
        if (!enabled_) {
            continue;
        }
        const Millis now = NowMs();
        applyPropsLocked(now, props);
        applyTimeLocked(now);
        pipeline_.tick(now);
        std::vector<std::pair<int64_t, float>> out;
        flushOutputLocked(out);
        if (!out.empty()) {
            lock.unlock();
            emit(out);
            lock.lock();
        }
    }
}

}  // namespace fusionlight
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
