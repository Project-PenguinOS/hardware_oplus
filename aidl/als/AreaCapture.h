/*
 * Copyright (C) 2021-2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/lineage/oplus_als/BnAreaCapture.h>
#include <ui/DisplayId.h>
#include <ui/Rect.h>
#include <ui/Rotation.h>

#include <optional>
#include <vector>

namespace aidl {
namespace vendor {
namespace lineage {
namespace oplus_als {

class AreaCapture : public BnAreaCapture {
  public:
    AreaCapture();
    ndk::ScopedAStatus getAreaBrightness(AreaRgbCaptureResult* _aidl_return) override;

  private:
    struct CaptureArea {
        ::android::Rect crop;
        ::android::ui::Rotation rotation;
    };

    std::optional<CaptureArea> getCaptureArea(
            ::android::PhysicalDisplayId displayId,
            const ::android::sp<::android::IBinder>& displayToken) const;

    ::android::Rect m_screenshot_rect;
    // Per-pixel weights of the grab area in panel orientation, row major.
    std::vector<float> m_weights;
};

}  // namespace oplus_als
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
