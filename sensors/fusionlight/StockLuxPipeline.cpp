/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "StockLuxPipeline.h"

#include <algorithm>
#include <cmath>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace subhal {
namespace implementation {
namespace fusionlight {
namespace stocklux {

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr int kConfidentScore = 5467;
constexpr Millis kHeartbeatPeriod = 3000;
constexpr Millis kTorchOffBlock = 1800;
constexpr Millis kDayNightScreenOnDelay = 5000;
constexpr size_t kInterimListSize = 20;

// LuxEvent aux conversion tables.
constexpr float kMainAreasForAux[] = {4.0f, 10.0f, 30.0f, 60.0f, 95.0f};
constexpr float kLuxThreshForAux[] = {0.0f, 4.0f, 10.0f, 30.0f, 60.0f};
constexpr float kLinearRatioForAux[] = {0.25f, 0.25f, 0.25f, 0.25f, 0.3f};
constexpr float kBoostRatioForAux[] = {16.0f, 16.0f, 32.0f, 64.0f, 128.0f};
constexpr float kBoostFloorForAux[] = {4.0f, 4.0f, 30.0f, 60.0f, 95.0f};

// StabilityProcess tables.
constexpr float kLeftNodes[] = {30.0f, 300.0f, 1080.0f, 500000.0f};
constexpr float kRightNodes[] = {20.0f, 35.0f, 500.0f, 500000.0f};
constexpr float kLeftCoefficient[] = {4.0f, 6.0f, 1.6f, 2.0f};
constexpr float kLeftConstant[] = {0.0f, 2.5f, -135.0f, 0.0f};
constexpr float kRightCoefficient[] = {2.5f, 10.0f, 2.0f, 1.5f};
constexpr float kRightConstant[] = {0.0f, -150.0f, 130.0f, 380.0f};
constexpr float kSmallPercent[] = {0.125f, 0.2f};
constexpr float kSmallThreshold[] = {2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 15.0f, 20.0f, 5000.0f};
constexpr float kSmallPercentLeft[] = {0.2853f, 0.2778f, 0.2593f, 0.25f, 0.2444f,
                                       0.1926f, 0.1778f, 0.125f, 0.2f};
constexpr float kSmallPercentRight[] = {0.3138f, 0.2778f, 0.2593f, 0.25f, 0.2444f,
                                        0.1926f, 0.1778f, 0.125f, 0.2f};
constexpr float kSpecialThreshold[] = {20.0f, 25.0f, 30.0f, 40.0f};
constexpr float kSpecialLeft[] = {0.1066f, 0.0842f, 0.0896f};
constexpr float kSpecialRight[] = {0.1429f, 0.1656f, 0.1603f};

template <size_t N>
size_t FindRegion(float lux, const float (&nodes)[N]) {
    size_t index = 0;
    while (index < N && lux - nodes[index] > 0.0f) {
        ++index;
    }
    return std::min(index, N - 1);
}

double Sum(const LuxList& list) {
    if (list.empty()) {
        return -1.0;
    }
    float sum = 0.0f;
    for (const float value : list) {
        sum += value;
    }
    return sum;
}

double Average(const LuxList& list) {
    return list.empty() ? -1.0 : Sum(list) / list.size();
}

double StandardDeviation(const LuxList& list) {
    if (list.empty()) {
        return -1.0;
    }
    const double average = Average(list);
    double sum = 0.0;
    for (const float value : list) {
        sum += std::pow(static_cast<double>(value) - average, 2.0);
    }
    return std::sqrt(sum / list.size());
}

int NotMonotonousCount(const LuxList& list) {
    if (list.size() < 3) {
        return -1;
    }
    int count = 0;
    int last_score = 0;
    for (size_t i = 1; i < list.size(); ++i) {
        const int score = list[i] > list[i - 1] ? 1 : (list[i] < list[i - 1] ? -1 : 0);
        if (score != 0 && last_score != score) {
            ++count;
            last_score = score;
        }
    }
    return std::max(count - 1, 0);
}

// All values at or above the limit (stock isOutOfMaxLimit).
bool OutOfMaxLimit(const LuxList& list, float limit) {
    if (list.empty()) {
        return false;
    }
    return std::none_of(list.begin(), list.end(),
                        [limit](float v) { return !std::isnan(v) && v < limit; });
}

bool OutOfMinLimit(const LuxList& list, float limit) {
    if (list.empty()) {
        return false;
    }
    return std::none_of(list.begin(), list.end(),
                        [limit](float v) { return !std::isnan(v) && v > limit; });
}

float RoundHalfDown2(float value) {
    if (!std::isfinite(value)) {
        return value;
    }
    const double scaled = static_cast<double>(value) * 100.0;
    double rounded;
    if (scaled >= 0.0) {
        const double floor = std::floor(scaled);
        rounded = scaled - floor > 0.5 ? floor + 1.0 : floor;
    } else {
        const double ceil = std::ceil(scaled);
        rounded = ceil - scaled > 0.5 ? ceil - 1.0 : ceil;
    }
    return static_cast<float>(rounded / 100.0);
}

float Weighting(float first, float last, float weight) {
    return last * weight + (1.0f - weight) * first;
}

// LuxDataProcess.findWeightingValue: exponential weighting from the oldest value.
float Weighting(LuxList list, float weight = 0.5f) {
    while (!list.empty()) {
        float result = list.front();
        for (const float value : list) {
            result = Weighting(result, value, weight);
        }
        if (std::isfinite(result)) {
            return RoundHalfDown2(result);
        }
        list.erase(list.begin());
    }
    return 0.0f;
}

bool FloatEquals(float a, float b) {
    // Java Float.compare semantics: NaN equals NaN.
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

}  // namespace

// --- LimitQueue -----------------------------------------------------------------------------

void LimitQueue::add(Millis now, float value) {
    entries_.emplace_back(now, value);
    while (!entries_.empty() && now - entries_.front().first > time_limit_) {
        entries_.pop_front();
    }
}

LuxList LimitQueue::all() const {
    LuxList list;
    for (const auto& entry : entries_) {
        list.push_back(entry.second);
    }
    return list;
}

LuxList LimitQueue::byPastTime(Millis now, Millis past) const {
    LuxList list;
    const Millis since = now - past;
    if (!entries_.empty() && since > entries_.back().first) {
        return list;
    }
    for (auto it = entries_.rbegin(); it != entries_.rend() && it->first > since; ++it) {
        list.insert(list.begin(), it->second);
    }
    return list;
}

LuxList LimitQueue::byCount(size_t count) const {
    LuxList list;
    const size_t start = entries_.size() > count ? entries_.size() - count : 0;
    for (size_t i = start; i < entries_.size(); ++i) {
        list.push_back(entries_[i].second);
    }
    return list;
}

LuxList LimitQueue::byPastTimeBasedOnCount(Millis now, Millis past, size_t count) const {
    LuxList list;
    const Millis since = now - past;
    const size_t start = entries_.size() > count ? entries_.size() - count : 0;
    for (size_t i = start; i < entries_.size(); ++i) {
        if (entries_[i].first >= since) {
            list.push_back(entries_[i].second);
        }
    }
    return list;
}

Millis LimitQueue::lastDataDuration(Millis now) const {
    return entries_.empty() ? time_limit_ : now - entries_.back().first;
}

// --- Result (OplusLuxResult) ------------------------------------------------------------------

enum class Reason {
    kInit,
    kLarge,
    kAuxLarge,
    kStableForce,
    kRecover,
    kInterim,
    kDayNight,
    kDarkest,
    kSync,
    kReset
};

struct Pipeline::Result {
    State state;
    Scene scene = Scene::kNormal;
    bool scene_change = true;
    float last_final = kNaN;
    float last_auto = kNaN;
    bool post_monitoring = false;
    float accelerate = 1.0f;
    int ambient_direction = 0;
    LuxList queue;
    float weight = kNaN;
    float automatic = kNaN;
    float final_lux = kNaN;
    std::vector<Reason> reasons;

    bool has(Reason reason) const {
        return std::find(reasons.begin(), reasons.end(), reason) != reasons.end();
    }
    bool init() const { return has(Reason::kInit); }
    bool reset() const { return has(Reason::kReset); }
    float realtime() const { return state.hasLux() ? state.lux->lux : kNaN; }
    bool accelerating() const { return accelerate > 1.0f; }
    bool queueOk() const { return !queue.empty(); }
    bool dataOk() const {
        return !std::isnan(weight) && weight >= 0.0f && !FloatEquals(last_auto, weight);
    }
    bool stabilityOk() const { return !std::isnan(automatic) && automatic >= 0.0f; }
    void setAutomatic(float lux) {
        if (!std::isnan(lux)) {
            automatic = lux;
            final_lux = lux;
        }
    }
    bool needUpdate() const {
        return !std::isnan(final_lux) && (!FloatEquals(final_lux, last_final) || init() || reset());
    }
    bool needSync() {
        bool sync = false;
        if (std::isnan(final_lux)) {
            sync = !(std::isnan(last_final) || std::isnan(last_auto) || last_final == last_auto);
            final_lux = std::isnan(weight) ? last_auto : weight;
            reasons.push_back(Reason::kSync);
        }
        return sync;
    }
    bool dangerLux(float lux) const {
        return stabilityOk() ? automatic <= lux : last_auto <= lux;
    }
};

// --- Pipeline -----------------------------------------------------------------------------------

Pipeline::Pipeline(Features features, Output output)
    : features_(features), output_(std::move(output)) {
    SceneConfig& normal = scene_configs_[static_cast<int>(Scene::kNormal)];
    if (features_.dark_env_smooth) {
        normal.darkness_duration = 420000;
    }

    SceneConfig& stable = scene_configs_[static_cast<int>(Scene::kStable)];
    stable.up_trend_time = 800;
    stable.down_trend_time = 800;
    stable.one_time_filter_weight = 1.0f;
    stable.shake_main_up_weight = stable.shake_main_down_weight = 0.85f;
    stable.shake_aux_up_weight = stable.shake_aux_down_weight = 0.85f;
    stable.dual_light_min_lux_range = 1.5f;
    stable.accelerate_up_weight = 1.5f;
    stable.accelerate_up_offset = 1;
    stable.accelerate_down_weight = 1.08f;
    stable.accelerate_down_offset = 2;
    stable.day_night_duration = 10000;
    stable.day_night_lux_threshold = 1.5f;
    stable.interim_duration = 5000;
    stable.darkness_duration = features_.dark_env_smooth ? 420000 : 120000;

    SceneConfig& erratic = scene_configs_[static_cast<int>(Scene::kErratic)];
    erratic.up_trend_time = 1500;
    erratic.down_trend_time = 2000;
    erratic.one_time_filter_weight = 0.3f;
    erratic.shake_main_up_weight = erratic.shake_main_down_weight = 1.1f;
    erratic.shake_aux_up_weight = erratic.shake_aux_down_weight = 1.1f;
    erratic.dual_light_min_lux_range = 3.0f;
    erratic.accelerate_switch = true;
    erratic.accelerate_up_weight = 1.2f;
    erratic.accelerate_up_offset = 2;
    erratic.accelerate_down_weight = 1.08f;
    erratic.accelerate_down_offset = 4;
    erratic.day_night_duration = 60000;
    erratic.day_night_lux_threshold = 2.0f;
    erratic.interim_duration = 45000;
    erratic.interim_lux_threshold = 1.5f;
    erratic.darkness_duration = 9999000;
}

const SceneConfig& Pipeline::config(Scene scene) const {
    return scene_configs_[static_cast<int>(scene)];
}

void Pipeline::setActive(Millis now, bool screen_on, bool auto_mode) {
    if (auto_mode != auto_mode_) {
        auto_mode_ = auto_mode;
        change_to_auto_ = auto_mode;
        if (auto_mode) {
            force_update_ = true;
        } else {
            // LuxPostProcessStrategy resetAll(mode switch)
            interim_scene_.reset();
            day_night_scene_.reset();
            day_night_time_ = -1;
            darkest_scene_.reset();
            darkest_time_ = -1;
            realtime_.clear();
            lux_source_.clear();
            weight_.clear();
            target_.clear();
            state_ = State();
        }
        scene_ = Scene::kNormal;
    }
    if (screen_on != screen_on_) {
        screen_on_ = screen_on;
        refresh_score_ = true;
        scene_ = Scene::kNormal;
        if (screen_on) {
            screen_on_time_ = now;
            last_result_.reset();
            if (last_lux_.has_value()) {
                state_.lux = last_lux_;
                sendState(now);
            }
        } else {
            last_main_.reset();
            last_aux_.reset();
            last_lux_.reset();
            // LuxPostProcessStrategy.resetAll(power off)
            interim_scene_.reset();
            if (day_night_effect_) {
                day_night_scene_.reset();
                day_night_time_ = -1;
            }
            if (!darkest_effect_) {
                darkest_scene_.reset();
                darkest_time_ = -1;
            }
        }
    }
}

// --- Phone state manager --------------------------------------------------------------------

bool Pipeline::completeData() const {
    // Motion and in/out-door are filled with defaults on dodge (features off).
    return state_.hasLux() && env_.dark_mode.has_value() && env_.wifi_connected.has_value() &&
           env_.normalization_time.has_value() && (env_.proximity_known || !features_.proximity);
}

void Pipeline::auxEvent(Millis now) {
    ++state_.aux_changes;
    if (state_.needUpdateState()) {
        sendState(now);
    }
}

void Pipeline::onProximity(Millis now, bool near) {
    env_.proximity_near = near;
    env_.proximity_known = true;
    auxEvent(now);
}

void Pipeline::onWifi(Millis now, bool connected) {
    env_.wifi_connected = connected;
    auxEvent(now);
}

void Pipeline::onDarkMode(Millis now, bool dark) {
    env_.dark_mode = dark;
    auxEvent(now);
}

void Pipeline::onTime(Millis now, int normalization_time) {
    env_.normalization_time = normalization_time;
    auxEvent(now);
}

void Pipeline::onTorch(Millis now, bool on) {
    if (env_.torch_on && !on) {
        torch_off_time_ = now;
    }
    env_.torch_on = on;
}

void Pipeline::tick(Millis now) {
    if (heartbeat_at_ >= 0 && now >= heartbeat_at_) {
        heartbeat_at_ = -1;
        if (state_.auxChanged()) {
            sendState(now);
        }
    }
}

float Pipeline::auxLux(float main_lux, float aux_lux) const {
    constexpr size_t kAreas = std::size(kMainAreasForAux);
    if (main_lux > kMainAreasForAux[kAreas - 1]) {
        return aux_lux > 200.0f ? 0.65f * aux_lux : aux_lux;
    }
    if (aux_lux <= main_lux) {
        return aux_lux;
    }
    size_t index = 0;
    while (index < kAreas && main_lux > kMainAreasForAux[index]) {
        ++index;
    }
    if (index == kAreas) {
        return aux_lux;
    }
    const float threshold = kLuxThreshForAux[index];
    if (aux_lux <= threshold) {
        return aux_lux;
    }
    const float linear = kLinearRatioForAux[index] * (aux_lux - threshold);
    const float exponent =
            kBoostRatioForAux[index] * std::exp(aux_lux / 4000.0f) + kBoostFloorForAux[index];
    return std::min(linear, exponent) + main_lux;
}

std::optional<Pipeline::LuxSample> Pipeline::assessMain(const LuxSample& sample) {
    LuxSample result = sample;
    const bool use_last =
            env_.proximity_near && (!last_main_.has_value() || sample.lux < last_main_->lux);
    if (use_last) {
        if (!last_main_.has_value()) {
            return std::nullopt;
        }
        result = *last_main_;
    }
    last_main_ = result;
    if (last_aux_.has_value()) {
        if (result.lux < last_aux_->lux) {
            return std::nullopt;
        }
        if (result.lux == last_aux_->lux && result.lux > 0.0f) {
            return last_aux_;
        }
    }
    return result;
}

std::optional<Pipeline::LuxSample> Pipeline::assessAux(Millis now, const LuxSample& sample) {
    const bool blocked =
            env_.torch_on || (torch_off_time_ > 0 && now - torch_off_time_ < kTorchOffBlock);
    if (blocked) {
        return last_lux_;
    }
    last_aux_ = sample;
    if (last_main_.has_value()) {
        if (sample.lux < last_main_->lux) {
            return std::nullopt;
        }
        return sample;
    }
    if (!last_lux_.has_value()) {
        return std::nullopt;
    }
    return sample;
}

void Pipeline::luxEvent(Millis now, const LuxSample& sample) {
    if (!auto_mode_ || dragging_) {
        return;
    }
    const auto result = sample.main ? assessMain(sample) : assessAux(now, sample);
    if (!result.has_value()) {
        return;
    }
    last_lux_ = result;
    if (screen_on_) {
        state_.lux = result;
        if (state_.needUpdateState()) {
            sendState(now);
        }
    }
}

void Pipeline::onMainLux(Millis now, float lux) {
    original_main_lux_ = lux;
    luxEvent(now, LuxSample{lux, lux, true});
}

void Pipeline::onAuxLux(Millis now, float lux) {
    luxEvent(now, LuxSample{auxLux(original_main_lux_, lux), lux, false});
}

void Pipeline::sendState(Millis now) {
    heartbeat_at_ = -1;
    if (!auto_mode_) {
        return;
    }
    const State state = state_;
    matchScene(now, state);
    if (state.hasLux()) {
        realtime_.add(now, state.lux->lux);
        lux_source_.add(now, state.lux->main ? 0.0f : 1.0f);
        onAmbienceChanged(now, state);
    }
    // callbackEnd
    state_.lux.reset();
    state_.aux_changes = 0;
    heartbeat_at_ = now + kHeartbeatPeriod;
}

// --- Scene adapter ---------------------------------------------------------------------------

int Pipeline::erraticScore(Millis now, const State& state) const {
    const bool same = scene_ == Scene::kErratic;
    int score = -1;
    const LuxList targets = target_.byPastTime(now, 30000);
    const int changes = NotMonotonousCount(targets);
    if (same && state.onlyLuxChanged()) {
        score = kConfidentScore;
    }
    if (changes > 0 && Average(targets) < 600000.0) {
        if (targets.size() >= 4) {
            if (changes >= 2) {
                score = kConfidentScore;
            }
        } else if (same || changes >= 1) {
            const double sd = StandardDeviation(weight_.byPastTime(now, 5000));
            if (sd >= 0.0 && !std::isnan(large_.low)) {
                const double shake = StandardDeviation({large_.low, large_.high});
                if (2.0 * sd > shake) {
                    score = same ? kConfidentScore : 5;
                }
            }
        }
        if (score < kConfidentScore) {
            int extra = 0;
            // Motion defaults to motionless and in/out-door to uncertain on dodge.
            if (env_.wifi_connected.has_value() && !*env_.wifi_connected) {
                extra += 2;
            }
            if (env_.normalization_time.has_value()) {
                const SceneConfig& erratic = config(Scene::kErratic);
                const int time = *env_.normalization_time;
                if (!(time >= erratic.day_night_begin && time <= erratic.day_night_end)) {
                    ++extra;
                }
            }
            if (env_.dark_mode.value_or(false)) {
                ++extra;
            }
            score += extra;
        }
    }
    return static_cast<int>(std::lround(score * (same ? 1.1f : 1.0f)));
}

int Pipeline::stableScore(Millis now, const State&) const {
    const bool same = scene_ == Scene::kStable;
    int score = -1;
    if (target_.byPastTime(now, 20000).empty()) {
        const double sd = StandardDeviation(weight_.byPastTime(now, 10000));
        if (sd <= 0.0) {
            score = kConfidentScore;
        } else if (!std::isnan(large_.low)) {
            const double shake = StandardDeviation({large_.low, large_.high});
            if (10.0 * sd < shake) {
                score = kConfidentScore;
            } else if (6.6666665f * sd < shake) {
                score = same ? kConfidentScore : 3;
            }
        }
        if (score < kConfidentScore) {
            int extra = 3;  // motionless
            if (env_.wifi_connected.value_or(false)) {
                extra += 4;
            }
            if (env_.dark_mode.value_or(false)) {
                ++extra;
            }
            score += extra;
        }
    }
    return static_cast<int>(std::lround(score * (same ? 1.1f : 1.0f)));
}

void Pipeline::matchScene(Millis now, const State& state) {
    if (state.onlyLuxChanged() && !refresh_score_) {
        return;
    }
    const int erratic = erraticScore(now, state);
    const int stable = stableScore(now, state);
    const int normal = 9;
    const int best = std::max({erratic, stable, normal});
    if (best == normal) {
        scene_ = Scene::kNormal;
    } else if (best == erratic) {
        scene_ = Scene::kErratic;
    } else {
        scene_ = Scene::kStable;
    }
    refresh_score_ = false;
}

void Pipeline::updateLuxChange(Millis now, float lux, bool target_change) {
    if (!std::isfinite(lux)) {
        return;
    }
    if (target_change) {
        weight_.add(now, lux);
        target_.add(now, lux);
    } else if (!weight_.empty() && weight_.lastDataDuration(now) >= 1000) {
        weight_.add(now, lux);
    }
    refresh_score_ = target_change || outside(lux, small_);
}

// --- Stability process -------------------------------------------------------------------------

bool Pipeline::outside(float lux, const Range& range) {
    return std::isnan(range.low) || std::isnan(range.high) || lux <= range.low ||
           lux >= range.high;
}

static float Blend(float target, float raw, float scale) {
    return std::max((1.0f - scale) * target + raw * scale, 0.0f);
}

static float LeftFromRegion(float lux) {
    const size_t index = FindRegion(lux, kLeftNodes);
    return lux / kLeftCoefficient[index] + kLeftConstant[index];
}

static float RightFromRegion(float lux) {
    const size_t index = FindRegion(lux, kRightNodes);
    return std::min(kRightCoefficient[index] * lux + kRightConstant[index], 2000.0f + lux);
}

bool Pipeline::weakLightLeftInvalid(float lux) const {
    // OplusLimitQueue reads its window against the clock at call time.
    return features_.weak_light_special_shake_range && lux <= 2.0f &&
           Sum(weight_.byPastTime(current_time_, 10000)) > 0.0;
}

float Pipeline::largeLeft(float lux, Scene scene) const {
    const float left = Blend(lux, LeftFromRegion(lux), config(scene).shake_main_down_weight);
    return weakLightLeftInvalid(lux) ? -1.0f : left;
}

float Pipeline::largeRight(float lux, Scene scene) const {
    const float coefficient =
            features_.erratic_large_range_reduce_not_supported ? 1.0f : 0.5f;
    return std::max(Blend(lux, RightFromRegion(lux), config(scene).shake_main_up_weight),
                    features_.stable_right_min_lux *
                            (scene == Scene::kErratic ? coefficient : 1.0f));
}

float Pipeline::largeLeftSpecial(float lux, Scene scene) const {
    const float left = Blend(lux, LeftFromRegion(lux), config(scene).shake_aux_down_weight);
    return weakLightLeftInvalid(lux) ? -1.0f : left;
}

float Pipeline::largeRightSpecial(float lux, Scene scene) const {
    return std::max(Blend(lux, RightFromRegion(lux), config(scene).shake_aux_up_weight),
                    config(scene).dual_light_min_lux_range);
}

static float SmallChange(float lux, float large_width, bool left, bool expand) {
    float coefficient;
    if (expand) {
        size_t index = 0;
        while (index < std::size(kSmallThreshold) && lux > kSmallThreshold[index]) {
            ++index;
        }
        coefficient = left ? kSmallPercentLeft[index] : kSmallPercentRight[index];
    } else {
        coefficient = kSmallPercent[lux > 5000.0f ? 1 : 0];
    }
    const float delta = large_width * coefficient;
    return left ? lux - delta : lux + delta;
}

static float SpecialThreshold(float lux, float large_width, float fallback, bool left) {
    const float* constants = left ? kSpecialLeft : kSpecialRight;
    for (size_t i = 0; i < std::size(kSpecialLeft); ++i) {
        if (lux > kSpecialThreshold[i] && lux <= kSpecialThreshold[i + 1]) {
            const float term = constants[i] * large_width;
            return left ? lux - term : lux + term;
        }
    }
    return fallback;
}

float Pipeline::smallLeft(const Range& large, float lux) const {
    const float width = large.high - large.low;
    float value = SpecialThreshold(
            lux, width, SmallChange(lux, width, true, features_.small_debounce_expand), true);
    if (features_.erratic_large_range_reduce_not_supported) {
        value = 0.6f * large.low + 0.4f * value;
    }
    return std::max(value, large.low);
}

float Pipeline::smallRight(const Range& large, float lux) const {
    const float width = large.high - large.low;
    float value = SpecialThreshold(
            lux, width, SmallChange(lux, width, false, features_.small_debounce_expand), false);
    if (features_.erratic_large_range_reduce_not_supported) {
        value = 0.6f * large.high + 0.4f * value;
    }
    return std::min(std::max(features_.stable_small_right_min_lux, value), large.high);
}

void Pipeline::updateRangeStrategy(const Result& result) {
    float lux = result.last_auto;
    if (std::isnan(lux)) {
        lux = result.last_final;
    }
    const bool lux_change = !FloatEquals(lux, current_automatic_lux_);
    const bool scene_change = range_scene_.has_value() && *range_scene_ != result.scene;
    if (!lux_change && !scene_change && range_scene_.has_value()) {
        return;
    }
    current_automatic_lux_ = std::isnan(lux) ? result.realtime() : lux;
    range_scene_ = result.scene;
    const float base = current_automatic_lux_;
    large_ = Range{largeLeft(base, result.scene), largeRight(base, result.scene)};
    large_aux_ = Range{largeLeftSpecial(base, result.scene), largeRightSpecial(base, result.scene)};
    small_ = Range{smallLeft(large_, base), smallRight(large_, base)};
}

float Pipeline::accelerateScale(float verify_lux) const {
    const SceneConfig& scene = config(range_scene_.value_or(scene_));
    if (std::isnan(verify_lux) || !scene.accelerate_switch || std::isnan(large_.low) ||
        verify_lux == std::clamp(verify_lux, large_.low, large_.high)) {
        return 1.0f;
    }
    const Scene current = range_scene_.value_or(scene_);
    const bool up = verify_lux - current_automatic_lux_ > 0.0f;
    int count = 0;
    float lux = current_automatic_lux_;
    while (count < 10 && !(lux < 1.0f && !up)) {
        const float limit = up ? largeRight(lux, current) : largeLeft(lux, current);
        const bool over = up ? verify_lux > limit : verify_lux < limit;
        if (!over) {
            break;
        }
        lux = limit;
        ++count;
    }
    const float weight = up ? scene.accelerate_up_weight : scene.accelerate_down_weight;
    const int offset = up ? scene.accelerate_up_offset : scene.accelerate_down_offset;
    const float scale = std::pow(weight, std::max(static_cast<float>(count - offset), 0.0f));
    return std::clamp(scale, scene.accelerate_min, scene.accelerate_max);
}

// --- Ambient adapter ---------------------------------------------------------------------------

void Pipeline::luxQueueProcess(Millis now, Result& result) {
    float scale = 1.0f;
    if (config(result.scene).accelerate_switch) {
        scale = accelerateScale(Weighting(realtime_.byCount(2), 0.8f));
    }
    result.accelerate = scale;
    if (result.reset()) {
        result.queue = {realtime_.empty() ? 0.0f : realtime_.last()};
        result.ambient_direction = direction_;
        return;
    }

    const SceneConfig& scene = config(result.scene);
    const Millis window = static_cast<Millis>(
            (direction_ == 1 ? scene.up_trend_time : scene.down_trend_time) / scale);
    const LuxList recent = realtime_.byPastTime(now, window);
    LuxList trend;
    if (!recent.empty()) {
        direction_ = 0;
        const float device_lux = std::max(std::isnan(result.last_final) ? 0.0f
                                                                         : result.last_final,
                                          0.0f);
        float average = kNaN;
        bool vigilant = false;
        bool ok = true;
        int noise = 0;
        for (auto it = recent.rbegin(); it != recent.rend(); ++it) {
            const float lux = *it;
            const int compare = lux > device_lux ? 1 : (lux < device_lux ? -1 : 0);
            vigilant = vigilant ||
                       (!result.scene_change ? !outside(lux, large_) : !outside(lux, small_));
            if (std::isnan(lux) || (direction_ == 0 && !result.post_monitoring && compare == 0)) {
                ok = false;
                break;
            }
            if (direction_ != 0 && direction_ != compare) {
                if (noise++ > 0) {
                    ok = false;
                    break;
                }
                continue;
            }
            trend.insert(trend.begin(), lux);
            direction_ = compare;
            if (!std::isnan(average) && vigilant) {
                bool unstable = false;
                if (direction_ == 1) {
                    unstable = lux * 2.0f < average &&
                               std::fabs(lux - device_lux) * 2.0f <
                                       std::fabs(average - device_lux);
                } else if (direction_ == -1) {
                    unstable = average * 2.0f < lux &&
                               std::fabs(lux - device_lux) * 2.0f >
                                       std::fabs(average - device_lux);
                }
                if (unstable) {
                    ok = false;
                    break;
                }
            }
            average = static_cast<float>(Average(trend));
        }
        if (!ok) {
            trend.clear();
        }
    }
    if (!trend.empty()) {
        result.queue = trend;
        result.ambient_direction = direction_;
    }
}

void Pipeline::luxDataProcess(Result& result) {
    if (!result.queueOk()) {
        return;
    }
    const float weight = Weighting(result.queue);
    const bool proximity_lock = completeData() && env_.proximity_near &&
                                weight <= result.last_final;
    if (!proximity_lock) {
        result.weight = weight;
    }
}

void Pipeline::stabilityProcess(Millis now, Result& result) {
    if (!result.dataOk() && !result.reset()) {
        return;
    }
    const float weight = result.weight;
    float calculate = weight;
    if (features_.erratic_scene_lux_filter && result.scene == Scene::kErratic &&
        !result.accelerating()) {
        const Millis target_duration = target_.lastDataDuration(now);
        const Millis check = std::min(now - screen_on_time_, target_duration);
        LuxList history = weight_.byPastTimeBasedOnCount(now, check, 3);
        history.push_back(weight);
        calculate = Weighting(history, config(result.scene).one_time_filter_weight);
    }

    float lux = kNaN;
    if (outside(calculate, large_)) {
        result.reasons.push_back(Reason::kLarge);
        lux = calculate;
    }
    if (std::isnan(lux)) {
        const size_t count = result.queue.size();
        const double aux_score = Sum(lux_source_.byCount(count));
        if (aux_score != -1.0 && aux_score >= count * 0.5 && outside(calculate, large_aux_)) {
            result.reasons.push_back(Reason::kAuxLarge);
            lux = calculate;
        }
    }
    if (std::isnan(lux)) {
        if (outside(calculate, small_)) {
            if (result.reset()) {
                lux = weight;
            } else {
                // The screen animation state is not visible from the HAL; stock only uses it to
                // pick RecoverChange, otherwise this is StableForceChange.
                const bool stable_scene = result.scene == Scene::kStable ||
                                          features_.all_scene_lux_small_change;
                if (stable_scene) {
                    LuxList updates = weight_.byPastTime(now, 4000);
                    if (!updates.empty()) {
                        updates.push_back(calculate);
                        if (OutOfMaxLimit(updates, small_.high) ||
                            OutOfMinLimit(updates, small_.low)) {
                            result.reasons.push_back(Reason::kStableForce);
                            lux = static_cast<float>(Average(updates));
                        }
                    }
                }
            }
        } else if (result.reset()) {
            lux = (std::isnan(weight) || weight < 0.0f) ? result.last_auto : weight;
        }
    }
    if (!std::isnan(lux)) {
        result.setAutomatic(lux);
    }
}

float Pipeline::checkLux(const Result& result) const {
    if (!std::isnan(result.final_lux)) {
        return result.final_lux;
    }
    if (!std::isnan(result.last_auto)) {
        return result.last_auto;
    }
    return result.realtime();
}

// InterimMode.evaluateInterimCondition / evaluateInterimFallback share this tail
// (resolveInterimFromDeviation).
float Pipeline::interimFromDeviation(Millis now, Result& result, Scene scene, float threshold,
                                     float check) {
    LuxList list;
    const Millis since = now - config(scene).interim_duration;
    if (!interim_list_.empty() && interim_list_.back().first > since) {
        for (const auto& [time, lux] : interim_list_) {
            if (time > since) {
                list.push_back(lux);
            }
        }
    }
    if (list.empty() && !interim_list_.empty()) {
        list.push_back(interim_list_.back().second);
    }
    // isForceRespondInterim needs outdoor, which is uncertain on dodge.
    if (StandardDeviation(list) > 0.009999999776482582) {
        interim_effect_ = true;
        result.post_monitoring = true;
        interim_scene_ = scene;
        return std::max(check, threshold);
    }
    interim_effect_ = false;
    result.setAutomatic(result.weight);
    return kNaN;
}

float Pipeline::interimLux(Millis now, Result& result) {
    if (features_.interim_mode && completeData()) {
        if (result.needUpdate() && !std::isnan(result.weight)) {
            interim_list_.emplace_back(now, result.weight);
        } else if (interim_list_.empty()) {
            interim_list_.emplace_back(now, result.last_final);
        }
        while (interim_list_.size() > kInterimListSize) {
            interim_list_.pop_front();
        }
        if (!interim_scene_.has_value() && !result.needUpdate()) {
            return kNaN;
        }
        // evaluateInterimCondition
        float lux = kNaN;
        if (result.dangerLux(2.0f)) {
            const Scene scene = interim_scene_.value_or(result.scene);
            const float threshold = config(scene).interim_lux_threshold;
            const float check = checkLux(result);
            interim_scene_.reset();
            if (check <= threshold && !result.init() && !change_to_auto_) {
                lux = interimFromDeviation(now, result, scene, threshold, check);
            }
        }
        if (!std::isnan(lux)) {
            return lux;
        }
        change_to_auto_ = false;
    }
    // evaluateInterimFallback
    if (!interim_scene_.has_value() && !result.needUpdate()) {
        return kNaN;
    }
    if (!result.dangerLux(2.0f)) {
        change_to_auto_ = false;
        interim_effect_ = false;
        return kNaN;
    }
    const Scene scene = interim_scene_.value_or(result.scene);
    const float threshold = config(scene).interim_lux_threshold;
    const float check = checkLux(result);
    interim_scene_.reset();
    if (check > threshold || result.init() || change_to_auto_) {
        change_to_auto_ = false;
        interim_effect_ = false;
        return kNaN;
    }
    const float lux = interimFromDeviation(now, result, scene, threshold, check);
    if (std::isnan(lux)) {
        change_to_auto_ = false;
    }
    return lux;
}

float Pipeline::dayNightLux(Millis now, Result& result) {
    if (!features_.day_night_mode || !completeData()) {
        day_night_effect_ = false;
        day_night_scene_.reset();
        day_night_time_ = -1;
        return kNaN;
    }
    if (!day_night_scene_.has_value() && !result.needUpdate()) {
        return kNaN;
    }
    const Scene scene = day_night_scene_.value_or(result.scene);
    const SceneConfig& scene_config = config(scene);
    const int time = *env_.normalization_time;
    const float check = checkLux(result);
    day_night_scene_.reset();

    const bool bright = time >= scene_config.day_night_begin && time <= scene_config.day_night_end;
    const bool switched = bright != bright_mode_;
    bool respond = false;
    if (!(now - screen_on_time_ < kDayNightScreenOnDelay)) {
        if (check <= scene_config.day_night_lux_threshold) {
            if (bright) {
                respond = !switched && !change_to_auto_;
            } else if (result.init()) {
                bright_mode_ = bright;
            } else {
                respond = switched && !change_to_auto_;
            }
        } else {
            bright_mode_ = bright;
        }
    }
    if (!respond) {
        day_night_effect_ = false;
        day_night_scene_.reset();
        day_night_time_ = -1;
        return kNaN;
    }
    if (day_night_time_ == -1) {
        day_night_time_ = now;
    }
    if (now - day_night_time_ <= scene_config.day_night_duration) {
        if (result.scene != Scene::kStable) {
            day_night_time_ = -1;
        }
        day_night_effect_ = true;
        result.post_monitoring = true;
        day_night_scene_ = scene;
        return std::max(scene_config.day_night_lux_threshold, check);
    }
    day_night_effect_ = false;
    return check;
}

float Pipeline::darkestLux(Millis now, Result& result) {
    if (!features_.darkest_mode || !completeData()) {
        darkest_effect_ = false;
        darkest_scene_.reset();
        darkest_time_ = -1;
        return kNaN;
    }
    const float average = static_cast<float>(Average(realtime_.all()));
    const bool no_scene = !darkest_scene_.has_value() && !result.needUpdate() && average >= 1.0f;
    if (no_scene || env_.game_mode || env_.torch_on) {
        darkest_effect_ = false;
        darkest_time_ = -1;
        return kNaN;
    }
    const bool low_average = average != -1.0f && average < 1.0f;
    const bool low = result.dangerLux(0.0f) || low_average;
    const Scene scene = darkest_scene_.value_or(result.scene);
    if (!low) {
        darkest_effect_ = false;
        darkest_time_ = -1;
        return kNaN;
    }
    const float check = checkLux(result);
    darkest_scene_ = scene;
    const float current = result.needUpdate() ? result.final_lux : result.last_final;
    if (current <= 0.0f && (result.realtime() <= 0.0f || low_average)) {
        if (darkest_time_ == -1) {
            darkest_time_ = now;
        }
        if (now - darkest_time_ >= config(scene).darkness_duration) {
            darkest_effect_ = true;
            return std::min(-1.0f, check);
        }
        darkest_effect_ = false;
        result.post_monitoring = true;
        return kNaN;
    }
    darkest_effect_ = false;
    darkest_scene_.reset();
    darkest_time_ = -1;
    return kNaN;
}

void Pipeline::luxPostProcess(Millis now, Result& result) {
    if (!last_result_ && !result.init()) {
        return;
    }
    result.post_monitoring = false;
    // Motion mode is off on dodge.
    const std::pair<float, Reason> modes[] = {
            {dayNightLux(now, result), Reason::kDayNight},
            {interimLux(now, result), Reason::kInterim},
            {darkestLux(now, result), Reason::kDarkest},
    };
    for (const auto& [lux, reason] : modes) {
        if (!std::isnan(lux)) {
            result.final_lux = lux;
            result.reasons.push_back(reason);
        }
    }
}

void Pipeline::onAmbienceChanged(Millis now, const State& state) {
    current_time_ = now;
    auto result = std::make_shared<Result>();
    result->state = state;
    result->scene = scene_;
    if (last_result_) {
        const Result& last = *last_result_;
        result->last_final = last.needUpdate() ? last.final_lux : last.last_final;
        result->last_auto = last.stabilityOk() ? last.automatic : last.last_auto;
        result->scene_change = last.scene != scene_;
        result->post_monitoring = last.post_monitoring;
    } else if (state.hasLux()) {
        result->setAutomatic(state.lux->original_lux);
        result->reasons.push_back(Reason::kInit);
    }
    if (force_update_) {
        result->reasons.push_back(Reason::kReset);
    }

    if (!result->init()) {
        updateRangeStrategy(*result);
        luxQueueProcess(now, *result);
        luxDataProcess(*result);
        stabilityProcess(now, *result);
    }
    luxPostProcess(now, *result);

    const bool update = result->needUpdate();
    if (update || result->needSync()) {
        last_result_ = result;
        force_update_ = false;
        updateLuxChange(now, result->stabilityOk() ? result->automatic : result->last_auto, true);
        last_output_ = result->final_lux;
        if (output_) {
            output_(result->final_lux);
        }
    } else if (result->dataOk()) {
        updateLuxChange(now, result->weight, false);
    }
}

}  // namespace stocklux
}  // namespace fusionlight
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
