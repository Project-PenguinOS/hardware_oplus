/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// Port of the stock ColorOS auto-brightness ambience pipeline (oplus-services
// com.android.server.display.feature.ambience): it turns main/aux light sensor
// readings and phone state into the lux the brightness curve is driven with.
// Times are milliseconds on one monotonic clock supplied by the caller.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace subhal {
namespace implementation {
namespace fusionlight {
namespace stocklux {

using Millis = int64_t;
using LuxList = std::vector<float>;

// OplusLimitQueue with a time limit: entries older than the limit are dropped on add.
class LimitQueue {
  public:
    explicit LimitQueue(Millis time_limit) : time_limit_(time_limit) {}

    void add(Millis now, float value);
    void clear() { entries_.clear(); }
    bool empty() const { return entries_.empty(); }
    float last() const { return entries_.back().second; }
    LuxList all() const;
    // Entries newer than now - past, oldest first; empty if even the newest is older.
    LuxList byPastTime(Millis now, Millis past) const;
    LuxList byCount(size_t count) const;
    // The last `count` entries that are not older than now - past.
    LuxList byPastTimeBasedOnCount(Millis now, Millis past, size_t count) const;
    // Time since the newest entry, or the time limit when empty.
    Millis lastDataDuration(Millis now) const;

  private:
    Millis time_limit_;
    std::deque<std::pair<Millis, float>> entries_;
};

struct SceneConfig {
    Millis up_trend_time = 800;
    Millis down_trend_time = 1200;
    float one_time_filter_weight = 1.0f;
    float shake_main_up_weight = 1.0f;
    float shake_main_down_weight = 1.0f;
    float shake_aux_up_weight = 1.0f;
    float shake_aux_down_weight = 1.0f;
    bool accelerate_switch = true;
    float accelerate_up_weight = 1.5f;
    int accelerate_up_offset = 1;
    float accelerate_down_weight = 1.08f;
    int accelerate_down_offset = 3;
    float accelerate_min = 1.0f;
    float accelerate_max = 3.333f;
    int day_night_begin = 73000;
    int day_night_end = 210000;
    Millis day_night_duration = 45000;
    float day_night_lux_threshold = 1.5f;
    float interim_lux_threshold = 1.0f;
    Millis interim_duration = 12000;
    Millis darkness_duration = 120000;
    float dual_light_min_lux_range = 2.0f;
};

enum class Scene { kNormal, kErratic, kStable };

// Stock ComfortableBrightnessConfig feature flags, defaults as on dodge.
struct Features {
    bool interim_mode = true;
    bool day_night_mode = true;
    bool darkest_mode = true;
    bool proximity = true;
    bool weak_light_special_shake_range = true;
    bool erratic_scene_lux_filter = true;
    bool all_scene_lux_small_change = false;
    bool erratic_large_range_reduce_not_supported = false;
    bool dark_env_smooth = true;
    bool small_debounce_expand = false;
    // STABLE_RIGHT_MIN_LUX_UNDER_SCREEN_LIGHT for high precision under-screen sensors.
    float stable_right_min_lux = 7.01f;
    float stable_small_right_min_lux = 4.2f;
};

// Phone state inputs that do not come from the light sensors.
struct Environment {
    std::optional<bool> wifi_connected;
    std::optional<bool> dark_mode;
    std::optional<int> normalization_time;  // HHMMSS local time
    bool proximity_near = false;
    bool proximity_known = false;
    bool game_mode = false;
    bool torch_on = false;
};

class Pipeline {
  public:
    using Output = std::function<void(float lux)>;

    Pipeline(Features features, Output output);

    // Screen and auto-brightness state. Events are only processed while both are on.
    void setActive(Millis now, bool screen_on, bool auto_mode);

    void onMainLux(Millis now, float lux);
    void onAuxLux(Millis now, float lux);
    void onProximity(Millis now, bool near);
    void onWifi(Millis now, bool connected);
    void onDarkMode(Millis now, bool dark);
    void onTime(Millis now, int normalization_time);
    void onTorch(Millis now, bool on);
    void setGameMode(bool on) { env_.game_mode = on; }
    void setDragging(bool dragging) { dragging_ = dragging; }

    // Runs the 3 s state heartbeat; call at least every few hundred milliseconds.
    void tick(Millis now);

    Scene scene() const { return scene_; }
    std::optional<float> lastLux() const { return last_output_; }

  private:
    struct LuxSample {
        float lux = 0.0f;           // converted lux
        float original_lux = 0.0f;  // sensor value
        bool main = true;
    };

    struct State {
        std::optional<LuxSample> lux;
        int aux_changes = 0;
        bool hasLux() const { return lux.has_value(); }
        bool needUpdateState() const { return hasLux() || aux_changes >= 2; }
        bool auxChanged() const { return aux_changes > 0; }
        bool onlyLuxChanged() const { return !auxChanged() && hasLux(); }
    };

    struct Range {
        float low = std::numeric_limits<float>::quiet_NaN();
        float high = std::numeric_limits<float>::quiet_NaN();
    };

    struct Result;

    // Phone state manager
    void auxEvent(Millis now);
    void sendState(Millis now);
    bool completeData() const;
    std::optional<LuxSample> assessMain(const LuxSample& sample);
    std::optional<LuxSample> assessAux(Millis now, const LuxSample& sample);
    void luxEvent(Millis now, const LuxSample& sample);
    float auxLux(float main_lux, float aux_lux) const;

    // Scene adapter
    void matchScene(Millis now, const State& state);
    int erraticScore(Millis now, const State& state) const;
    int stableScore(Millis now, const State& state) const;
    const SceneConfig& config(Scene scene) const;
    void updateLuxChange(Millis now, float lux, bool target_change);

    // Ambient adapter
    void onAmbienceChanged(Millis now, const State& state);
    void luxQueueProcess(Millis now, Result& result);
    void luxDataProcess(Result& result);
    void stabilityProcess(Millis now, Result& result);
    void luxPostProcess(Millis now, Result& result);

    // Stability process
    void updateRangeStrategy(const Result& result);
    float largeLeft(float lux, Scene scene) const;
    float largeRight(float lux, Scene scene) const;
    float largeLeftSpecial(float lux, Scene scene) const;
    float largeRightSpecial(float lux, Scene scene) const;
    float smallLeft(const Range& large, float lux) const;
    float smallRight(const Range& large, float lux) const;
    bool weakLightLeftInvalid(float lux) const;
    float accelerateScale(float verify_lux) const;
    static bool outside(float lux, const Range& range);

    // Post processing
    float checkLux(const Result& result) const;
    float interimLux(Millis now, Result& result);
    float interimFromDeviation(Millis now, Result& result, Scene scene, float threshold,
                               float check);
    float dayNightLux(Millis now, Result& result);
    float darkestLux(Millis now, Result& result);

    Features features_;
    Output output_;
    std::array<SceneConfig, 3> scene_configs_;

    bool screen_on_ = false;
    bool auto_mode_ = false;
    bool dragging_ = false;
    Millis screen_on_time_ = 0;
    Millis heartbeat_at_ = -1;
    bool change_to_auto_ = false;
    bool force_update_ = true;

    Environment env_;
    State state_;
    std::optional<LuxSample> last_main_;
    std::optional<LuxSample> last_aux_;
    std::optional<LuxSample> last_lux_;
    float original_main_lux_ = -1.0f;
    Millis torch_off_time_ = -1;

    Scene scene_ = Scene::kNormal;
    bool refresh_score_ = true;
    LimitQueue realtime_{5000};
    LimitQueue lux_source_{30000};
    LimitQueue weight_{60000};
    LimitQueue target_{30000};

    int direction_ = 0;

    float current_automatic_lux_ = -1.0f;
    std::optional<Scene> range_scene_;
    Range large_;
    Range large_aux_;
    Range small_;

    std::shared_ptr<Result> last_result_;
    std::optional<float> last_output_;

    // Interim mode
    std::deque<std::pair<Millis, float>> interim_list_;
    std::optional<Scene> interim_scene_;
    bool interim_effect_ = false;
    // Day/night mode
    std::optional<Scene> day_night_scene_;
    Millis day_night_time_ = -1;
    bool bright_mode_ = false;
    bool day_night_effect_ = false;
    // Darkest mode
    std::optional<Scene> darkest_scene_;
    Millis darkest_time_ = -1;
    bool darkest_effect_ = false;

    // Clock of the result being processed (queues read their windows against it).
    Millis current_time_ = 0;
};

}  // namespace stocklux
}  // namespace fusionlight
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
