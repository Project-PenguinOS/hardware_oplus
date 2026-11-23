/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// org.penguin.sensor.auto_brightness_lux: stock ColorOS auto-brightness lux.

#include <android-base/macros.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "StockLuxPipeline.h"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace subhal {
namespace implementation {
namespace fusionlight {

class StockLuxSensor final {
  public:
    using EmitCallback = std::function<void(int64_t timestamp_ns, float lux)>;

    explicit StockLuxSensor(EmitCallback emit);
    ~StockLuxSensor();

    DISALLOW_COPY_AND_ASSIGN(StockLuxSensor);

    void setEnabled(bool enabled);

    void onMainLux(float lux);
    void onAuxLux(float lux);
    void onProximity(float distance);

  private:
    struct Props {
        std::optional<bool> wifi;
        std::optional<bool> dark_mode;
        bool game_mode = false;
        bool torch = false;
        bool auto_mode = true;
        bool dragging = false;
        int64_t utc_offset_s = 0;
    };

    static Props ReadProps();
    static int NormalizationTime(int64_t utc_offset_s);

    void applyPropsLocked(stocklux::Millis now, const Props& props);
    void applyTimeLocked(stocklux::Millis now);
    void flushOutputLocked(std::vector<std::pair<int64_t, float>>& out);
    void emit(std::vector<std::pair<int64_t, float>>& out);
    void tickLoop();

    EmitCallback emit_;
    std::mutex mutex_;
    std::condition_variable cv_;
    stocklux::Pipeline pipeline_;
    std::vector<std::pair<int64_t, float>> pending_;
    std::optional<Props> props_;
    int last_minute_ = -1;
    bool enabled_ = false;
    bool stopping_ = false;
    bool debug_ = false;
    std::thread thread_;
};

}  // namespace fusionlight
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
