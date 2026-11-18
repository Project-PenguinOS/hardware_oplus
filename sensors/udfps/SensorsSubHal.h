/*
 * SPDX-FileCopyrightText: 2022-2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <V2_1/SubHal.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace subhal {
namespace implementation {

using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::RateLevel;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SharedMemInfo;
using ::android::hardware::sensors::V2_1::Event;
using ::android::hardware::sensors::V2_1::SensorInfo;
using ::android::hardware::sensors::V2_1::implementation::IHalProxyCallback;
using ::android::hardware::sensors::V2_1::implementation::ISensorsSubHal;

// The HIDL side of the UDFPS sensor; the sensor itself (udfps.rs) runs in Rust.
class SensorsSubHal : public ISensorsSubHal {
  public:
    SensorsSubHal();

    Return<void> getSensorsList_2_1(ISensors::getSensorsList_2_1_cb _hidl_cb) override;
    Return<Result> setOperationMode(OperationMode mode) override;
    Return<Result> activate(int32_t sensorHandle, bool enabled) override;
    Return<Result> batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                         int64_t maxReportLatencyNs) override;
    Return<Result> flush(int32_t sensorHandle) override;
    Return<Result> injectSensorData_2_1(const Event& event) override;
    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                       ISensors::registerDirectChannel_cb _hidl_cb) override;
    Return<Result> unregisterDirectChannel(int32_t channelHandle) override;
    Return<void> configDirectReport(int32_t sensorHandle, int32_t channelHandle, RateLevel rate,
                                    ISensors::configDirectReport_cb _hidl_cb) override;
    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override;
    const std::string getName() override { return "UdfpsSubHal"; }
    Return<Result> initialize(const sp<IHalProxyCallback>& halProxyCallback) override;

  private:
    static void onEvent(void* context, int32_t x, int32_t y);

    SensorInfo mSensorInfo;
    sp<IHalProxyCallback> mCallback;
    bool mStarted = false;
};

}  // namespace implementation
}  // namespace subhal
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
