/*
 * SPDX-FileCopyrightText: 2022-2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SensorsSubHal.h"

#include <android/hardware/sensors/2.1/types.h>
#include <log/log.h>
#include <utils/SystemClock.h>

using ::android::hardware::sensors::V2_1::implementation::ISensorsSubHal;

extern "C" {
bool udfps_sensor_start(void (*callback)(void* context, int32_t x, int32_t y), void* context);
void udfps_sensor_activate(bool enabled);
void udfps_sensor_set_injection_mode(bool injection);
}

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
}

void SensorsSubHal::onEvent(void* context, int32_t x, int32_t y) {
    auto* self = static_cast<SensorsSubHal*>(context);
    Event event;
    event.sensorHandle = self->mSensorInfo.sensorHandle;
    event.sensorType = self->mSensorInfo.type;
    event.timestamp = ::android::elapsedRealtimeNano();
    event.u.data[0] = x;
    event.u.data[1] = y;
    ScopedWakelock wakelock = self->mCallback->createScopedWakelock(true);
    self->mCallback->postEvents({event}, std::move(wakelock));
}

Return<void> SensorsSubHal::getSensorsList_2_1(ISensors::getSensorsList_2_1_cb _hidl_cb) {
    // HalProxy lists sensors before initialize(), so this cannot depend on the sensor thread.
    _hidl_cb({mSensorInfo});
    return Void();
}

Return<Result> SensorsSubHal::setOperationMode(OperationMode mode) {
    udfps_sensor_set_injection_mode(mode == OperationMode::DATA_INJECTION);
    return Result::OK;
}

Return<Result> SensorsSubHal::activate(int32_t sensorHandle, bool enabled) {
    if (sensorHandle != kUdfpsHandle) return Result::BAD_VALUE;
    udfps_sensor_activate(enabled);
    return Result::OK;
}

Return<Result> SensorsSubHal::batch(int32_t sensorHandle, int64_t /* samplingPeriodNs */,
                                    int64_t /* maxReportLatencyNs */) {
    // One-shot: there is no rate to set.
    return sensorHandle == kUdfpsHandle ? Result::OK : Result::BAD_VALUE;
}

Return<Result> SensorsSubHal::flush(int32_t /* sensorHandle */) {
    // One-shot sensors have no flush.
    return Result::BAD_VALUE;
}

Return<Result> SensorsSubHal::injectSensorData_2_1(const Event& event) {
    if (event.sensorHandle != kUdfpsHandle) return Result::BAD_VALUE;
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
    return Void();
}

Return<Result> SensorsSubHal::initialize(const sp<IHalProxyCallback>& halProxyCallback) {
    mCallback = halProxyCallback;
    mStarted = udfps_sensor_start(&SensorsSubHal::onEvent, this);
    udfps_sensor_set_injection_mode(false);
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
