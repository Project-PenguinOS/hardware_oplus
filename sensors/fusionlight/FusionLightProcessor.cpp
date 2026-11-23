/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "FusionLightProcessor.h"

#ifdef USES_ALS_CAPTURE
#include "AlsSampler.h"
#else
#include "CwbSampler.h"
#endif

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <vector>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace subhal {
namespace implementation {
namespace fusionlight {

using namespace std::chrono_literals;

namespace {

#ifdef FUSIONLIGHT_FRAME_SYNC
constexpr bool kFrameSync = true;
#else
constexpr bool kFrameSync = false;
#endif

constexpr size_t kMaxScreenSamples = 5;
constexpr int64_t kSampleGracePeriods = 2;
constexpr int64_t kCaptureStallPeriods = 8;

constexpr size_t kMaxPendingEvents = 39;
constexpr auto kMaxPendingAge = 2s;
constexpr auto kNoMatchTimeout = 1s;
constexpr auto kDefaultIntegrationWindow = 8333300ns;

constexpr auto kScreenshotInfoPath = "/dev/ssc_screenshot_info";
constexpr int32_t kScreenshotInfoTypeSurfaceFlinger = 1;
constexpr int32_t kMaxScreenshotIndex = 65533;

struct PendingEvent {
    Event event;
    Channels raw_channels = Channels::Zero();
    int32_t brightness = 0;
    bool dc_mode = false;
    std::chrono::nanoseconds window = kDefaultIntegrationWindow;
};

int FindRange(const std::vector<ValueRange>& ranges, double value) {
    for (int level = 0; level < ranges.size(); ++level) {
        if (value >= ranges[level].min && value < ranges[level].max) {
            return level;
        }
    }
    // Some profile tables use adjacent inclusive integer ranges (for example,
    // 1..419 followed by 420..728). Recover their upper endpoint only when no
    // half-open range matched.
    for (int level = 0; level < ranges.size(); ++level) {
        if (value >= ranges[level].min && value <= ranges[level].max) {
            return level;
        }
    }
    return -1;
}

// The view model matches brightness above each lower bound, except for the
// first IR brightness range. Unmatched brightness falls back to the first range.
int FindViewRange(const std::vector<ValueRange>& ranges, double value, bool include_first_min) {
    for (int level = 0; level < ranges.size(); ++level) {
        const bool above_min = value > ranges[level].min ||
                               (include_first_min && level == 0 && value == ranges[level].min);
        if (above_min && value <= ranges[level].max) {
            return level;
        }
    }
    return 0;
}

// FusionLightBrightnesses lists the upper brightness of the default, medium and
// low brightness segments.
int FindViewSegment(const FusionConfig& config, int32_t brightness) {
    if (config.segments.size() != kIrBandCount) {
        return 0;
    }
    if (brightness <= config.segment_brightness[2]) {
        return 2;
    }
    return brightness <= config.segment_brightness[1] ? 1 : 0;
}

// V2.1 selects the first IR brightness range whose upper bound covers the
// brightness, ignoring lower bounds. Unmatched brightness falls back to the
// first range.
int FindUpperRange(const std::vector<ValueRange>& ranges, double value) {
    for (int level = 0; level < ranges.size(); ++level) {
        if (value <= ranges[level].max) {
            return level;
        }
    }
    return 0;
}

// Stock compares IR ratios and lux in fixed point with 1e-7 resolution.
int64_t ToFixedPoint(double value) {
    constexpr double kLimit = 9.2e18;
    return static_cast<int64_t>(std::clamp(value * 1e7, -kLimit, kLimit));
}

// The LIR band has no lower bound and the super HIR band no upper bound. Ratios
// outside every band fall back to LIR.
int FindIrBand(const std::vector<ValueRange>& thresholds, double ratio) {
    const int64_t value = ToFixedPoint(ratio);
    if (value <= ToFixedPoint(thresholds[0].max)) {
        return 0;
    }
    if (value > ToFixedPoint(thresholds[1].min) && value <= ToFixedPoint(thresholds[1].max)) {
        return 1;
    }
    return value > ToFixedPoint(thresholds[2].min) ? 2 : 0;
}

int FindChannelPolicy(const FusionConfig& config, const Channels& channels) {
    if (!config.channel_count_policy_supported) {
        return -1;
    }
    for (int level = 0; level < config.channel_thresholds.size(); ++level) {
        bool matches = true;
        for (int channel = 0; channel < kChannelCount; ++channel) {
            const int max = config.channel_thresholds[level][channel];
            matches &= max == -1 || channels[channel] < max;
        }
        if (matches) {
            return level;
        }
    }
    return -1;
}

int FindCZeroLevel(const FusionConfig& config, double clear) {
    int level = 0;
    for (int candidate = 0; candidate < config.c_zero_thresholds.size(); ++candidate) {
        if (clear >= config.c_zero_thresholds[candidate]) {
            level = candidate;
        }
    }
    return level;
}

bool HasAbsoluteIrRatio(int32_t sensor_module_id) {
    constexpr int32_t kModules[] = {4, 5, 6, 8, 11};
    return std::find(std::begin(kModules), std::end(kModules), sensor_module_id) !=
           std::end(kModules);
}

double IrRatio(const FusionConfig& config, const Channels& channels) {
    const bool has_clear = ToFixedPoint(channels[3]) > 0;
    if (config.ir_ratio_formula == 2) {
        if (!has_clear) {
            return 1.0;
        }
        return std::max(1.0 - (channels[0] * 0.4379 + channels[1] * 0.243 + channels[2] * 0.2435) /
                                        channels[3],
                        0.0);
    }
    if (!has_clear) {
        return 0.0;
    }
    double ratio = (channels[0] + channels[1] + channels[2] - channels[3]) / channels[3] * 0.5;
    // Stock folds negative ratios back for these sensor modules.
    if (HasAbsoluteIrRatio(config.sensor_module_id)) {
        ratio = std::abs(ratio);
    }
    return std::max(ratio, 0.0);
}

// Black-frame profiles, and V2.1 profiles without a leakage model, report lux
// from the raw channels without screen captures.
bool UsesScreenCapture(const FusionConfig& config) {
    return !config.black_frame_supported &&
           (!config.screenshot_v2_1_supported || !config.leakage.empty());
}

double EvaluatePolynomial(const Polynomial& polynomial, double x) {
    return ((polynomial[0] * x + polynomial[1]) * x + polynomial[2]) * x + polynomial[3];
}

std::optional<double> CalculateLux(const FusionConfig& config, const Channels& channels,
                                   int32_t brightness, bool screen_off) {
    const int band = FindIrBand(config.ir_thresholds, IrRatio(config, channels));

    const std::vector<Channels>* table;
    int level;
    if (screen_off) {
        if (!config.screen_off_lux_supported) {
            return std::nullopt;
        }
        table = &config.screen_off_coefficients[band];
        level = FindCZeroLevel(config, channels[3]);
    } else {
        level = FindChannelPolicy(config, channels);
        if (level >= 0) {
            table = &config.channel_count_coefficients[band];
        } else if (config.screenshot_v2_1_supported) {
            table = &config.lux_coefficients[band];
            level = FindUpperRange(config.ir_brightness, brightness);
        } else {
            const auto& segment = config.segments[FindViewSegment(config, brightness)];
            table = &segment.lux_coefficients[band];
            level = FindViewRange(segment.ir_brightness, brightness, true);
        }
    }

    if (level < 0 || level >= table->size()) {
        return std::nullopt;
    }
    double lux = channels.dot((*table)[level]);
    if (!std::isfinite(lux)) {
        return std::nullopt;
    }
    if (ToFixedPoint(lux) < ToFixedPoint(config.low_light_accuracy)) {
        lux = 0.0;
    }
    return std::max(lux, 0.0);
}

LeakageModel LeakageTerms(double linearity, double r, double g, double b) {
    const double l2 = linearity * linearity;
    const double r2 = r * r;
    const double g2 = g * g;
    const double b2 = b * b;
    LeakageModel terms;
    terms << 1.0, linearity, r, g, b, l2, linearity * r, linearity * g, linearity * b, r2, r * g,
            r * b, g2, g * b, b2, l2 * linearity, l2 * r, l2 * g, l2 * b, linearity * r2,
            linearity * r * g, linearity * r * b, linearity * g2, linearity * g * b, linearity * b2,
            r2 * r, r2 * g, r2 * b, r * g2, r * g * b, r * b2, g2 * g, g2 * b, g * b2, b2 * b;
    return terms;
}

RatioModel RatioTerms(double r, double g, double b) {
    const double r2 = r * r;
    const double g2 = g * g;
    const double b2 = b * b;
    RatioModel terms;
    terms << 1.0, r, g, b, r2, r * g, r * b, g2, g * b, b2, r2 * r, r2 * g, r2 * b, r * g2,
            r * g * b, r * b2, g2 * g, g2 * b, g * b2, b2 * b;
    return terms;
}

std::optional<double> CalculateCorrectedLux(const FusionConfig& config,
                                            const std::vector<Channels>& calibration,
                                            const PendingEvent& pending_event,
                                            const CwbSample& sample) {
    const Channels& raw_channels = pending_event.raw_channels;
    const int32_t brightness = pending_event.brightness;
    const int level = FindRange(config.linearity_brightness, brightness);
    if (level < 0 || level >= calibration.size()) {
        LOG(WARNING) << "FusionLight calibration invalid level ts_ns="
                     << pending_event.event.timestamp << " brightness=" << brightness
                     << " level=" << level;
        return std::nullopt;
    }

    const auto ratio_basis = RatioTerms(sample.r, sample.g, sample.b);
    Channels compensated;
    for (int channel = 0; channel < kChannelCount; ++channel) {
        const auto& polynomial = config.linearity[level][channel];
        const double x = brightness;
        const double linearity = EvaluatePolynomial(polynomial, x);
        const auto leakage_basis = LeakageTerms(linearity, sample.r, sample.g, sample.b);
        const double parameter = config.leakage[level][channel].dot(leakage_basis);
        const double ratio = config.leakage_ratio[level][channel].dot(ratio_basis);
        const double leakage = std::max(parameter * ratio * calibration[level][channel], 0.0);
        compensated[channel] = std::max(raw_channels[channel] - leakage, 0.0);
    }
    return CalculateLux(config, compensated, brightness, false);
}

Channels CalculateViewLinearity(const FusionConfig& config, int32_t brightness, bool dc_mode) {
    if (brightness == 0) {
        return Channels::Zero();
    }
    if (dc_mode && !config.fod_linearity_segment_supported) {
        return config.fod_linearity_constant;
    }
    const auto& functions = dc_mode ? config.fod_linearity : config.linearity;
    int level = FindViewRange(config.linearity_brightness, brightness, false);
    if (level >= functions.size()) {
        level = 0;
    }
    Channels linearity;
    for (int channel = 0; channel < kChannelCount; ++channel) {
        linearity[channel] = EvaluatePolynomial(functions[level][channel], brightness);
    }
    return linearity;
}

std::optional<double> CalculateViewLux(const FusionConfig& config,
                                       const std::vector<ViewCalibration>& calibration,
                                       const PendingEvent& pending_event, const CwbSample& sample) {
    const int32_t brightness = pending_event.brightness;
    const int segment_index = FindViewSegment(config, brightness);
    if (segment_index >= calibration.size()) {
        return std::nullopt;
    }
    const auto& segment = config.segments[segment_index];
    const Eigen::Vector3d screen(sample.r, sample.g, sample.b);
    const Channels linearity = CalculateViewLinearity(config, brightness, pending_event.dc_mode);

    Channels compensated;
    for (int channel = 0; channel < kChannelCount; ++channel) {
        // R, G and B views add their leakage; the white view, driven by the
        // channel's grey level, removes the overlap between them.
        double leakage = 0.0;
        for (int view = 0; view < kChannelCount; ++view) {
            const bool white = view == kChannelCount - 1;
            const double x = white ? segment.grey_scale[channel].dot(screen) : screen[view];
            const double term = EvaluatePolynomial(segment.leakage[channel][view], x) *
                                calibration[segment_index](channel, view);
            leakage += white ? -term : term;
        }
        leakage = std::max(leakage * linearity[channel], 0.0);
        compensated[channel] = std::max(pending_event.raw_channels[channel] - leakage, 0.0);
    }
    return CalculateLux(config, compensated, brightness, false);
}

std::optional<CwbSample> SelectScreenSample(std::chrono::nanoseconds event_time,
                                            const std::deque<CwbSample>& samples) {
    for (auto sample = samples.rbegin(); sample != samples.rend(); ++sample) {
        if (sample->frame_end <= event_time) {
            return *sample;
        }
    }
    return std::nullopt;
}

Event MakeFusionLightEvent(const Event& event, int32_t fusion_light_handle, float lux) {
    Event result = event;
    result.sensorHandle = fusion_light_handle;
    result.sensorType = SensorType::LIGHT;
    result.u.scalar = lux;
    return result;
}

bool InRange(int64_t value, int64_t min, int64_t max) {
    return value >= min && value <= max;
}

// Stock ScreenShotResult::matchRatio.
double MatchRatio(const ScreenshotMatchConfig& config, const PendingEvent& pending,
                  const CwbSample& sample) {
    auto window = pending.window;
    auto delay = config.delay;
    const auto duration = sample.frame_end - sample.frame_start;
    if (InRange(window.count(), 10000001, 11999999)) {
        delay = config.delay_90;
    } else if (InRange(window.count(), 15000001, 16999999)) {
        if (config.delay_60.has_value() && InRange(duration.count(), 15000001, 16999999)) {
            delay = *config.delay_60;
            window = config.sample_period_for_60;
        } else {
            window = kDefaultIntegrationWindow;
        }
    }
    if (window <= 0ns || duration <= 0ns) {
        return 0.0;
    }
    const auto event_start = std::chrono::nanoseconds(pending.event.timestamp);
    const auto overlap = std::min(event_start + window, sample.frame_end + delay) -
                         std::max(event_start, sample.frame_start + delay);
    return overlap <= 0ns ? 0.0
                          : std::min(static_cast<double>(overlap.count()) / window.count(), 1.0);
}

int64_t ToFixedPoint4(double value) {
    return static_cast<int64_t>(value * 1e4);
}

int64_t RoundFixedPoint4(double value) {
    return static_cast<int64_t>(std::round(value * 1e4));
}

// Stock luxEventFilterLocked.
bool AcceptLux(std::optional<double> last_lux, double lux,
               const std::vector<const PendingEvent*>& candidates, size_t target) {
    if (!last_lux.has_value()) {
        return true;
    }
    const float previous = std::fabs(static_cast<float>(*last_lux)) < 0.001f
                                   ? 1.0f
                                   : static_cast<float>(*last_lux);
    float limit;
    const int64_t previous_fixed = ToFixedPoint4(previous);
    if (InRange(previous_fixed, 10001, 340000 - 1)) {
        limit = std::pow(previous - 0.9f, -0.128f) * 2.0385f;
    } else {
        limit = previous_fixed < 10001 ? 5.0f : 1.3f;
    }
    const int64_t rate = ToFixedPoint4(static_cast<float>(lux) / previous);
    if (rate < ToFixedPoint4(limit) && rate > ToFixedPoint4(2.0f - limit)) {
        return true;
    }

    if (candidates.size() < 2) {
        return false;
    }
    const size_t last = target > 0 ? target - 1 : 0;
    const size_t next = target + 1 < candidates.size() ? target + 1 : target;
    const int64_t target_clear = RoundFixedPoint4(candidates[target]->raw_channels[3]);
    const int64_t high = target_clear * 2;
    const int64_t low = target_clear / 2;
    for (const size_t index : {last, next}) {
        const int64_t clear = RoundFixedPoint4(candidates[index]->raw_channels[3]);
        if (clear > high || clear < low) {
            return false;
        }
    }
    return true;
}

}  // namespace

struct FusionLightProcessor::SharedState {
    explicit SharedState(EmitCallback callback) : emit(std::move(callback)) {}

    bool startDispatchLocked() {
        if (dispatching || delivery.empty()) {
            return false;
        }
        dispatching = true;
        return true;
    }

    void enqueueDeliveryLocked(Event event) { delivery.push_back(std::move(event)); }

    void resetMatchingLocked() {
        waiting_for_sample.reset();
        pending.clear();
        match_sample.reset();
        last_reported_lux.reset();
        last_report_time = 0ns;
    }

    void flushPendingLocked() {
        if (cwb_failed && !pending.empty()) {
            enqueueDeliveryLocked(std::move(pending.back().event));
        }
        pending.clear();
    }

    void appendPendingLocked(PendingEvent pending_event) {
        pending.push_back(std::move(pending_event));
        const auto newest = std::chrono::nanoseconds(pending.back().event.timestamp);
        while (pending.size() > kMaxPendingEvents ||
               newest - std::chrono::nanoseconds(pending.front().event.timestamp) >
                       kMaxPendingAge) {
            pending.pop_front();
        }
    }

    void checkCwbStallLocked(std::chrono::nanoseconds event_time) {
        const auto last_capture =
                screen_samples.empty() ? minimum_frame_start : screen_samples.back().frame_end;
        if (!cwb_failed && last_capture > 0ns && event_time > last_capture &&
            event_time - last_capture > config.cwb.screenshot_period * kCaptureStallPeriods) {
            LOG(WARNING) << "FusionLight CWB stalled since_ns=" << last_capture.count()
                         << " event_ns=" << event_time.count();
            cwb_failed = true;
            if (kFrameSync) {
                flushPendingLocked();
            } else {
                finishWaitingLocked();
            }
        }
    }

    void calibrateWithSampleLocked(PendingEvent& pending_event, const CwbSample& sample) {
        const auto lux =
                config.screenshot_v2_1_supported
                        ? CalculateCorrectedLux(config, calibration, pending_event, sample)
                        : CalculateViewLux(config, view_calibration, pending_event, sample);
        if (lux.has_value()) {
            pending_event.event.u.scalar = *lux;
        } else {
            LOG(WARNING) << "FusionLight RAW ts_ns=" << pending_event.event.timestamp
                         << " brightness=" << pending_event.brightness
                         << " reason=invalid_calibration lux=" << pending_event.event.u.scalar;
        }
    }

    void finishWaitingLocked() {
        if (!waiting_for_sample.has_value()) {
            return;
        }
        if (cwb_failed) {
            enqueueDeliveryLocked(std::move(waiting_for_sample->event));
        }
        waiting_for_sample.reset();
    }

    bool updateCalibrationLocked(PendingEvent& pending_event) {
        const auto event_time = std::chrono::nanoseconds(pending_event.event.timestamp);
        const auto sample = SelectScreenSample(event_time, screen_samples);
        if (!sample.has_value()) {
            return false;
        }
        const auto age = event_time - sample->frame_end;
        if (cwb_failed && age > config.cwb.screenshot_period * kSampleGracePeriods) {
            return false;
        }
        calibrateWithSampleLocked(pending_event, *sample);
        return true;
    }

    void finishWaitingWithSampleLocked() {
        if (!waiting_for_sample.has_value() || screen_samples.empty()) {
            return;
        }
        if (updateCalibrationLocked(*waiting_for_sample)) {
            enqueueDeliveryLocked(std::move(waiting_for_sample->event));
            waiting_for_sample.reset();
            return;
        }
        const auto& sample = screen_samples.back();
        const auto event_time = std::chrono::nanoseconds(waiting_for_sample->event.timestamp);
        // A pending event may use a later capture only within two sample periods.
        if (sample.frame_end >= event_time &&
            sample.frame_end - event_time <= config.cwb.screenshot_period * kSampleGracePeriods) {
            calibrateWithSampleLocked(*waiting_for_sample, sample);
            enqueueDeliveryLocked(std::move(waiting_for_sample->event));
            waiting_for_sample.reset();
            return;
        }
        finishWaitingLocked();
    }

    void calibrateWithRecentSampleLocked(PendingEvent& pending_event) {
        const auto event_time = std::chrono::nanoseconds(pending_event.event.timestamp);
        const auto sample = SelectScreenSample(event_time, screen_samples);
        if (sample.has_value() &&
            event_time - sample->frame_end <= config.cwb.screenshot_period * kSampleGracePeriods) {
            calibrateWithSampleLocked(pending_event, *sample);
        }
    }

    void writeScreenshotInfoLocked(const CwbSample& sample) {
        if (screenshot_info_failed || sample.frame_end <= sample.frame_start) {
            return;
        }
        if (!screenshot_info_fd.ok()) {
            screenshot_info_fd.reset(TEMP_FAILURE_RETRY(
                    open(kScreenshotInfoPath, O_WRONLY | O_CLOEXEC)));
            if (!screenshot_info_fd.ok()) {
                PLOG(WARNING) << "Unable to open " << kScreenshotInfoPath;
                screenshot_info_failed = true;
                return;
            }
        }
        screenshot_index = screenshot_index > kMaxScreenshotIndex ? 1 : screenshot_index + 1;
        const auto info = android::base::StringPrintf(
                "%lld,%lld,%d,%d", static_cast<long long>(sample.frame_start.count()),
                static_cast<long long>(sample.frame_end.count()), screenshot_index,
                kScreenshotInfoTypeSurfaceFlinger);
        if (TEMP_FAILURE_RETRY(write(screenshot_info_fd.get(), info.c_str(), info.size())) < 0) {
            PLOG(WARNING) << "Unable to write " << kScreenshotInfoPath;
            screenshot_info_fd.reset();
        }
    }

    // Stock handleEventForRegionSampling.
    void tryMatchLocked(bool force_complete = false) {
        if (!match_sample.has_value() || pending.empty()) {
            return;
        }
        const CwbSample& sample = *match_sample;
        const auto& match = config.screenshot_match;
        const int64_t threshold = ToFixedPoint4(match.ratio_threshold);
        std::vector<const PendingEvent*> candidates;
        std::vector<int64_t> ratios;
        const PendingEvent* best = nullptr;
        double best_ratio = 0.0;
        for (const auto& pending_event : pending) {
            const double ratio = MatchRatio(match, pending_event, sample);
            if (ToFixedPoint4(ratio) >= threshold) {
                candidates.push_back(&pending_event);
                ratios.push_back(ToFixedPoint4(ratio));
            }
            if (ratio > best_ratio) {
                best = &pending_event;
                best_ratio = ratio;
            }
        }

        std::optional<size_t> peak;
        for (size_t index = 0; index + 1 < candidates.size(); ++index) {
            if (ratios[index + 1] < ratios[index]) {
                peak = index;
                break;
            }
        }
        const auto latest_delay = std::max({match.delay, match.delay_90,
                                            match.delay_60.value_or(match.delay)});
        const bool complete = force_complete ||
                              std::chrono::nanoseconds(pending.back().event.timestamp) >
                                      sample.frame_end + latest_delay;
        if (!peak.has_value() && !complete) {
            return;
        }
        if (debug) {
            std::string list;
            for (const auto& pending_event : pending) {
                list += android::base::StringPrintf(
                        " %lld:%.2f", static_cast<long long>(pending_event.event.timestamp / 1000000),
                        MatchRatio(match, pending_event, sample));
            }
            LOG(INFO) << "FusionLight capture ["
                      << sample.frame_start.count() / 1000000 << ","
                      << sample.frame_end.count() / 1000000 << "] candidates "
                      << candidates.size() << (peak.has_value() ? " peak" : " complete")
                      << " pending" << list;
        }

        size_t target = 0;
        bool fallback = false;
        if (!candidates.empty()) {
            target = peak.value_or(candidates.size() - 1);
        } else {
            const auto newest = std::chrono::nanoseconds(pending.back().event.timestamp);
            if (newest - last_report_time < kNoMatchTimeout) {
                LOG_IF(INFO, debug) << "FusionLight no match, best ratio " << best_ratio;
                match_sample.reset();
                return;
            }
            if (best == nullptr) {
                const auto capture_mid = (sample.frame_start + sample.frame_end) / 2 + match.delay;
                auto distance = [&](const PendingEvent& event) {
                    const auto mid = std::chrono::nanoseconds(event.event.timestamp) +
                                     event.window / 2;
                    return mid > capture_mid ? mid - capture_mid : capture_mid - mid;
                };
                for (const auto& pending_event : pending) {
                    if (best == nullptr || distance(pending_event) < distance(*best)) {
                        best = &pending_event;
                    }
                }
            }
            candidates.push_back(best);
            fallback = true;
        }

        PendingEvent result = *candidates[target];
        calibrateWithSampleLocked(result, sample);
        match_sample.reset();
        const double lux = result.event.u.scalar;
        const int64_t target_time = result.event.timestamp;
        if (fallback || AcceptLux(last_reported_lux, lux, candidates, target)) {
            LOG_IF(INFO, debug) << "FusionLight report lux " << lux
                                << (fallback ? " (fallback, best ratio " : " (ratio ")
                                << (fallback ? best_ratio : ratios[target] / 1e4) << ")";
            last_reported_lux = lux;
            last_report_time = std::chrono::nanoseconds(target_time);
            enqueueDeliveryLocked(std::move(result.event));
        } else {
            LOG_IF(INFO, debug) << "FusionLight filtered lux " << lux << " after "
                                << *last_reported_lux;
        }
        while (!pending.empty() && pending.front().event.timestamp <= target_time) {
            pending.pop_front();
        }
    }

    void dispatch() {
        for (;;) {
            Event event;
            EmitCallback callback;
            {
                std::lock_guard lock(mutex);
                if (delivery.empty()) {
                    dispatching = false;
                    return;
                }
                event = std::move(delivery.front());
                delivery.pop_front();
                callback = emit;
            }
            if (callback) {
                callback(std::move(event));
            }
        }
    }

    std::mutex mutex;
    EmitCallback emit;
    FusionConfig config;
    std::vector<Channels> calibration;
    std::vector<ViewCalibration> view_calibration;
    std::deque<PendingEvent> pending;
    std::optional<PendingEvent> waiting_for_sample;
    std::deque<Event> delivery;
    std::deque<CwbSample> screen_samples;
    std::optional<CwbSample> match_sample;
    std::optional<double> last_reported_lux;
    std::chrono::nanoseconds last_report_time{0};
    std::chrono::nanoseconds minimum_frame_start{0};
    android::base::unique_fd screenshot_info_fd;
    int32_t screenshot_index = 0;
    int32_t brightness = -1;
    bool active = false;
    bool available = false;
    bool cwb_failed = false;
    bool dispatching = false;
    bool screenshot_info_failed = false;
    bool debug = false;
};

bool FusionLightProcessor::initialize(const std::string& sensor_name) {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    profile_sensor_name_ = sensor_name;
    return initializeLocked();
}

bool FusionLightProcessor::initializeLocked() {
    if (state_->available) {
        return true;
    }

    if (!android::base::GetBoolProperty("ro.vendor.oplus.sensor.high_pwm_rgb", false)) {
        LOG(INFO) << "High-PWM RGB property is disabled; FusionLight will pass raw lux";
        return false;
    }

    auto config = profile_sensor_name_.empty() ? std::optional<FusionConfig>{}
                                               : LoadConfig(profile_sensor_name_);
    if (!config.has_value() || !config->cwb_supported || !config->fusion_rgb_supported) {
        LOG(WARNING) << "FusionLight is disabled or unavailable; passing raw lux";
        return false;
    }

    std::vector<Channels> calibration;
    std::vector<ViewCalibration> view_calibration;
    if (!UsesScreenCapture(*config)) {
        LOG(INFO) << "FusionLight profile does not use screen captures";
    } else if (config->screenshot_v2_1_supported) {
        calibration = LoadCalibration(*config, kFrameSync);
    } else {
        view_calibration = LoadViewCalibration(*config);
    }
    if (!sampler_) {
        auto callback = [this](std::optional<CwbSample> sample) { handleSample(std::move(sample)); };
#ifdef USES_ALS_CAPTURE
        sampler_ = std::make_unique<AlsSampler>(std::move(callback));
#else
        sampler_ = std::make_unique<CwbSampler>(std::move(callback));
#endif
    }
    sampler_->setConfig(config->cwb);
    {
        std::lock_guard lock(state_->mutex);
        state_->config = std::move(*config);
        state_->calibration = std::move(calibration);
        state_->view_calibration = std::move(view_calibration);
        state_->debug =
                android::base::GetBoolProperty("persist.vendor.sensors.fusionlight.debug", false);
        state_->available = true;
    }
    return true;
}

void FusionLightProcessor::setEnabled(bool enabled) {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    if (enabled) {
        initializeLocked();
        {
            std::lock_guard lock(state_->mutex);
            state_->active = true;
            state_->screen_samples.clear();
            state_->resetMatchingLocked();
            state_->delivery.clear();
            state_->minimum_frame_start = 0ns;
            state_->brightness = -1;
            state_->cwb_failed = false;
        }
        return;
    }

    {
        std::lock_guard lock(state_->mutex);
        state_->active = false;
    }
    if (sampler_) {
        sampler_->stop();
    }
    {
        std::lock_guard lock(state_->mutex);
        state_->screen_samples.clear();
        state_->resetMatchingLocked();
        state_->delivery.clear();
        state_->minimum_frame_start = 0ns;
        state_->brightness = -1;
        state_->cwb_failed = false;
    }
}

void FusionLightProcessor::enqueueReady(Event event) {
    bool should_dispatch = false;
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->active) {
            return;
        }
        if (!kFrameSync) {
            state_->finishWaitingLocked();
        }
        state_->enqueueDeliveryLocked(std::move(event));
        should_dispatch = state_->startDispatchLocked();
    }
    if (should_dispatch) {
        state_->dispatch();
    }
}

void FusionLightProcessor::processScreenOff(Event event, const Channels& raw_channels,
                                            int32_t brightness) {
    bool should_dispatch = false;
    {
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        sampler_->stop();
        {
            std::lock_guard lock(state_->mutex);
            if (!state_->active) {
                return;
            }
            const auto lux = CalculateLux(state_->config, raw_channels, brightness, true);
            if (lux.has_value()) {
                event.u.scalar = *lux;
            }
            state_->resetMatchingLocked();
            state_->enqueueDeliveryLocked(std::move(event));
            should_dispatch = state_->startDispatchLocked();
            state_->screen_samples.clear();
            state_->minimum_frame_start = 0ns;
            state_->brightness = brightness;
            state_->cwb_failed = false;
        }
    }
    if (should_dispatch) {
        state_->dispatch();
    }
}

void FusionLightProcessor::process(const Event& event, int32_t fusion_light_handle) {
    const auto event_time = std::chrono::nanoseconds(event.timestamp);
    Event fusion_light_event = MakeFusionLightEvent(event, fusion_light_handle, event.u.data[0]);

    Channels raw_channels;
    for (int channel = 0; channel < kChannelCount; ++channel) {
        raw_channels[channel] = event.u.data[channel + 4];
        if (!std::isfinite(raw_channels[channel])) {
            enqueueReady(std::move(fusion_light_event));
            return;
        }
    }
    int32_t brightness = event.u.data[3];
    // Stock treats brightness below 2 as screen off.
    if (brightness < 2) {
        brightness = 0;
    }
    // Bit 1 of the event flags reports DC dimming.
    const bool dc_mode = static_cast<int32_t>(event.u.data[2]) & 0x2;
    const float window_ms = event.u.data[1];
    const auto window = std::isfinite(window_ms) && window_ms > 0.0f
                                ? std::chrono::nanoseconds(std::llround(window_ms * 1e6))
                                : std::chrono::nanoseconds(kDefaultIntegrationWindow);

    bool available;
    bool screen_capture = false;
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->active) {
            return;
        }
        available = state_->available;
        if (available && brightness > 0) {
            screen_capture = UsesScreenCapture(state_->config);
            const auto lux =
                    screen_capture ? std::nullopt
                                   : CalculateLux(state_->config, raw_channels, brightness, false);
            if (lux.has_value()) {
                fusion_light_event.u.scalar = *lux;
            }
        }
    }
    if (!available || (brightness > 0 && !screen_capture)) {
        enqueueReady(std::move(fusion_light_event));
        return;
    }
    if (brightness <= 0) {
        processScreenOff(std::move(fusion_light_event), raw_channels, brightness);
        return;
    }

    bool brightness_changed;
    bool start_sampler;
    {
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        {
            std::lock_guard lock(state_->mutex);
            if (!state_->active) {
                return;
            }
            brightness_changed = brightness != state_->brightness;
            start_sampler = state_->brightness <= 0;
            if (start_sampler) {
                state_->minimum_frame_start = event_time;
                state_->cwb_failed = false;
            }
            state_->checkCwbStallLocked(event_time);
            if (brightness_changed) {
                if (!kFrameSync) {
                    state_->finishWaitingLocked();
                }
                state_->brightness = brightness;
            }
        }
        if (start_sampler) {
            sampler_->start();
        }
        if (brightness_changed) {
            sampler_->requestSample();
        }
    }

    bool should_dispatch = false;
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->active) {
            return;
        }
        PendingEvent pending_event{std::move(fusion_light_event), raw_channels, brightness,
                                   dc_mode, window};
        if (!kFrameSync) {
            state_->finishWaitingLocked();
            if (state_->updateCalibrationLocked(pending_event)) {
                state_->enqueueDeliveryLocked(std::move(pending_event.event));
            } else if (state_->cwb_failed) {
                state_->enqueueDeliveryLocked(std::move(pending_event.event));
            } else {
                state_->waiting_for_sample = std::move(pending_event);
            }
        } else if (state_->cwb_failed) {
            state_->calibrateWithRecentSampleLocked(pending_event);
            state_->enqueueDeliveryLocked(std::move(pending_event.event));
        } else {
            state_->appendPendingLocked(std::move(pending_event));
            state_->tryMatchLocked();
        }
        should_dispatch = state_->startDispatchLocked();
    }
    if (should_dispatch) {
        state_->dispatch();
    }
}

void FusionLightProcessor::completeFlush(Event event) {
    bool should_dispatch = false;
    {
        std::lock_guard lock(state_->mutex);
        if (!kFrameSync) {
            state_->finishWaitingLocked();
        }
        state_->enqueueDeliveryLocked(std::move(event));
        should_dispatch = state_->startDispatchLocked();
    }
    if (should_dispatch) {
        state_->dispatch();
    }
}

FusionLightProcessor::FusionLightProcessor(EmitCallback emit_callback)
    : state_(std::make_shared<SharedState>(std::move(emit_callback))) {}

void FusionLightProcessor::handleSample(std::optional<CwbSample> sample) {
    const auto shared_state = std::weak_ptr<SharedState>(state_).lock();
    if (shared_state == nullptr) {
        return;
    }

    bool should_dispatch = false;
    {
        std::lock_guard lock(shared_state->mutex);
        if (!shared_state->active) {
            return;
        }
        if (!sample.has_value() || sample->frame_start <= 0ns ||
            sample->frame_end <= sample->frame_start) {
            shared_state->cwb_failed = true;
            if (kFrameSync) {
                shared_state->flushPendingLocked();
            } else {
                shared_state->finishWaitingLocked();
            }
        } else if (sample->frame_start >= shared_state->minimum_frame_start) {
            shared_state->cwb_failed = false;
            shared_state->screen_samples.push_back(*sample);
            while (shared_state->screen_samples.size() > kMaxScreenSamples) {
                shared_state->screen_samples.pop_front();
            }
            if (kFrameSync) {
                shared_state->writeScreenshotInfoLocked(*sample);
                shared_state->tryMatchLocked(/*force_complete=*/true);
                shared_state->match_sample = *sample;
                shared_state->tryMatchLocked();
            } else {
                shared_state->finishWaitingWithSampleLocked();
            }
        }
        should_dispatch = shared_state->startDispatchLocked();
    }
    if (should_dispatch) {
        shared_state->dispatch();
    }
}

FusionLightProcessor::~FusionLightProcessor() {
    setEnabled(false);
    std::lock_guard lock(state_->mutex);
    state_->emit = nullptr;
}

}  // namespace fusionlight
}  // namespace implementation
}  // namespace subhal
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
