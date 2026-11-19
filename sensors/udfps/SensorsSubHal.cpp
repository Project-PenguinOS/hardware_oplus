/*
 * SPDX-FileCopyrightText: 2022-2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SensorsSubHal.h"

#include <aidl/vendor/oplus/hardware/touch/IOplusTouch.h>
#include <android/binder_manager.h>
#include <android/hardware/sensors/2.1/types.h>
#include <log/log.h>
#include <utils/SystemClock.h>

#include <cstdlib>
#include <string>

using ::android::hardware::sensors::V2_1::implementation::ISensorsSubHal;

extern "C" {
bool udfps_sensor_start(void (*callback)(void* context, int32_t x, int32_t y), void* context);
void udfps_sensor_activate(bool enabled);
void udfps_sensor_set_injection_mode(bool injection);
bool tap_sensor_available();
bool tap_sensor_start(void (*callback)(void* context), void* context);
void tap_sensor_activate(bool enabled);
void tap_sensor_set_injection_mode(bool injection);
}

namespace {

using aidl::vendor::oplus::hardware::touch::IOplusTouch;

// vendor.oplus.hardware.touch gesture bits, as the power HAL uses for double tap: node 21 holds
// one bit per oplus gesture id, and SINGLE_TAP is gesture 16.
constexpr int kDefaultTpIcId = 0;
constexpr int kGestureEnableNode = 1;
constexpr int kGestureIndepNode = 21;
constexpr int kSingleTapGesture = 1 << 16;

// Arms the firmware single tap gesture while the sensor is active, keeping the other gestures.
void setSingleTapGesture(bool enabled) {
    const std::string instance = std::string() + IOplusTouch::descriptor + "/default";
    std::shared_ptr<IOplusTouch> touch = IOplusTouch::fromBinder(
            ndk::SpAIBinder(AServiceManager_checkService(instance.c_str())));
    if (!touch) {
        ALOGE("%s is not available", instance.c_str());
        return;
    }
    std::string state;
    int gestures = 0;
    if (touch->touchReadNodeFile(kDefaultTpIcId, kGestureIndepNode, &state).isOk()) {
        gestures = static_cast<int>(strtol(state.c_str(), nullptr, 16));
    }
    const int wanted = enabled ? (gestures | kSingleTapGesture) : (gestures & ~kSingleTapGesture);
    if (wanted == gestures) return;
    int ret = 0;
    if (enabled) touch->touchWriteNodeFile(kDefaultTpIcId, kGestureEnableNode, "1", &ret);
    touch->touchWriteNodeFile(kDefaultTpIcId, kGestureIndepNode, std::to_string(wanted), &ret);
    ALOGI("single tap gesture %s, gestures %d", enabled ? "on" : "off", wanted);
}

}  // anonymous namespace

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace subhal {
namespace implementation {

using ::android::hardware::Void;
using ::android::hardware::sensors::V1_0::SensorFlagBits;
using ::android::hardware::sensors::V2_0::implementation::ScopedWakelock;
using ::android::hardware::sensors::V2_1::SensorType;

static constexpr int32_t kUdfpsHandle = 1;
static constexpr int32_t kTapHandle = 2;

SensorsSubHal::SensorsSubHal() {
    mSensorInfo.sensorHandle = kUdfpsHandle;
    mSensorInfo.name = "UDFPS Sensor";
    mSensorInfo.vendor = "The LineageOS Project";
    mSensorInfo.version = 1;
    mSensorInfo.type =
            static_cast<SensorType>(static_cast<int32_t>(SensorType::DEVICE_PRIVATE_BASE) + 1);
    mSensorInfo.typeAsString = "org.lineageos.sensor.udfps";
    mSensorInfo.maxRange = 2048.0f;
    mSensorInfo.resolution = 1.0f;
    mSensorInfo.power = 0;
    mSensorInfo.minDelay = -1;
    mSensorInfo.maxDelay = 0;
    mSensorInfo.fifoReservedEventCount = 0;
    mSensorInfo.fifoMaxEventCount = 0;
    mSensorInfo.requiredPermission = "";
    mSensorInfo.flags = static_cast<uint32_t>(SensorFlagBits::ONE_SHOT_MODE) |
                        static_cast<uint32_t>(SensorFlagBits::WAKE_UP);

    // Doze uses this as its tap sensor (config_dozeTapSensorType).
    mTapAvailable = tap_sensor_available();
    mTapSensorInfo = mSensorInfo;
    mTapSensorInfo.sensorHandle = kTapHandle;
    mTapSensorInfo.name = "Single Tap Sensor";
    mTapSensorInfo.vendor = "The PenguinOS Project";
    mTapSensorInfo.type =
            static_cast<SensorType>(static_cast<int32_t>(SensorType::DEVICE_PRIVATE_BASE) + 2);
    mTapSensorInfo.typeAsString = "org.penguin.sensor.single_tap";
    mTapSensorInfo.maxRange = 1.0f;
}

void SensorsSubHal::postEvent(const SensorInfo& sensor, float x, float y) {
    Event event;
    event.sensorHandle = sensor.sensorHandle;
    event.sensorType = sensor.type;
    event.timestamp = ::android::elapsedRealtimeNano();
    event.u.data[0] = x;
    event.u.data[1] = y;
    ScopedWakelock wakelock = mCallback->createScopedWakelock(true);
    mCallback->postEvents({event}, std::move(wakelock));
}

void SensorsSubHal::onTap(void* context) {
    // No position: the firmware gesture only reports that a tap happened.
    static_cast<SensorsSubHal*>(context)->postEvent(
            static_cast<SensorsSubHal*>(context)->mTapSensorInfo, -1, -1);
}

void SensorsSubHal::onEvent(void* context, int32_t x, int32_t y) {
    auto* self = static_cast<SensorsSubHal*>(context);
    self->postEvent(self->mSensorInfo, x, y);
}

Return<void> SensorsSubHal::getSensorsList_2_1(ISensors::getSensorsList_2_1_cb _hidl_cb) {
    // HalProxy lists sensors before initialize(), so this cannot depend on the sensor thread.
    if (mTapAvailable) {
        _hidl_cb({mSensorInfo, mTapSensorInfo});
    } else {
        _hidl_cb({mSensorInfo});
    }
    return Void();
}

Return<Result> SensorsSubHal::setOperationMode(OperationMode mode) {
    udfps_sensor_set_injection_mode(mode == OperationMode::DATA_INJECTION);
    tap_sensor_set_injection_mode(mode == OperationMode::DATA_INJECTION);
    return Result::OK;
}

Return<Result> SensorsSubHal::activate(int32_t sensorHandle, bool enabled) {
    if (sensorHandle == kTapHandle && mTapAvailable) {
        if (!mTapStarted) return Result::INVALID_OPERATION;
        setSingleTapGesture(enabled);
        tap_sensor_activate(enabled);
        return Result::OK;
    }
    if (sensorHandle != kUdfpsHandle) return Result::BAD_VALUE;
    udfps_sensor_activate(enabled);
    return Result::OK;
}

Return<Result> SensorsSubHal::batch(int32_t sensorHandle, int64_t /* samplingPeriodNs */,
                                    int64_t /* maxReportLatencyNs */) {
    // One-shot: there is no rate to set.
    return sensorHandle == kUdfpsHandle || (sensorHandle == kTapHandle && mTapAvailable)
                   ? Result::OK
                   : Result::BAD_VALUE;
}

Return<Result> SensorsSubHal::flush(int32_t /* sensorHandle */) {
    // One-shot sensors have no flush.
    return Result::BAD_VALUE;
}

Return<Result> SensorsSubHal::injectSensorData_2_1(const Event& event) {
    if (event.sensorHandle != kUdfpsHandle &&
        !(event.sensorHandle == kTapHandle && mTapAvailable)) {
        return Result::BAD_VALUE;
    }
    // ADDITIONAL_INFO pushes environment data, which this sensor does not use.
    return event.sensorType == SensorType::ADDITIONAL_INFO ? Result::OK
                                                            : Result::INVALID_OPERATION;
}

Return<void> SensorsSubHal::registerDirectChannel(const SharedMemInfo& /* mem */,
                                                  ISensors::registerDirectChannel_cb _hidl_cb) {
    _hidl_cb(Result::INVALID_OPERATION, -1 /* channelHandle */);
    return Void();
}

Return<Result> SensorsSubHal::unregisterDirectChannel(int32_t /* channelHandle */) {
    return Result::INVALID_OPERATION;
}

Return<void> SensorsSubHal::configDirectReport(int32_t /* sensorHandle */,
                                               int32_t /* channelHandle */, RateLevel /* rate */,
                                               ISensors::configDirectReport_cb _hidl_cb) {
    _hidl_cb(Result::INVALID_OPERATION, 0 /* reportToken */);
    return Void();
}

Return<void> SensorsSubHal::debug(const hidl_handle& fd, const hidl_vec<hidl_string>& /* args */) {
    if (fd.getNativeHandle() == nullptr || fd->numFds < 1) {
        ALOGE("%s: missing fd for writing", __FUNCTION__);
        return Void();
    }
    dprintf(fd->data[0], "Available sensors:\nName: %s\nMin delay: %d\nFlags: %u\nRunning: %d\n\n",
            mSensorInfo.name.c_str(), mSensorInfo.minDelay, mSensorInfo.flags, mStarted);
    if (mTapAvailable) {
        dprintf(fd->data[0], "Name: %s\nFlags: %u\nRunning: %d\n\n",
                mTapSensorInfo.name.c_str(), mTapSensorInfo.flags, mTapStarted);
    }
    return Void();
}

Return<Result> SensorsSubHal::initialize(const sp<IHalProxyCallback>& halProxyCallback) {
    mCallback = halProxyCallback;
    mStarted = udfps_sensor_start(&SensorsSubHal::onEvent, this);
    udfps_sensor_set_injection_mode(false);
    if (mTapAvailable) {
        mTapStarted = tap_sensor_start(&SensorsSubHal::onTap, this);
        tap_sensor_set_injection_mode(false);
        // Nothing is listening at start; don't leave a stale gesture bit from a previous run.
        setSingleTapGesture(false);
    }
    return Result::OK;
}

}  // namespace implementation
}  // namespace subhal
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android

ISensorsSubHal* sensorsHalGetSubHal_2_1(uint32_t* version) {
    static ::android::hardware::sensors::V2_1::subhal::implementation::SensorsSubHal subHal;
    *version = SUB_HAL_2_1_VERSION;
    return &subHal;
}
