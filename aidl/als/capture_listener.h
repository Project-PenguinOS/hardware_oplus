/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <android/gui/BnScreenCaptureListener.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

// Capture completion is asynchronous. Never leave an incoming ALS call occupying a
// Binder thread forever if SurfaceFlinger or the callback pool cannot make progress.
class AlsCaptureListener : public android::gui::BnScreenCaptureListener {
  public:
    android::binder::Status onScreenCaptureCompleted(
            const android::gui::ScreenCaptureResults& results) override {
        std::lock_guard lock(mMutex);
        if (!mResults) mResults = results;
        mReady.notify_one();
        return android::binder::Status::ok();
    }

    bool waitForResults(android::gui::ScreenCaptureResults* results,
                        std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock lock(mMutex);
        if (!mReady.wait_for(lock, timeout, [this] { return mResults.has_value(); })) {
            return false;
        }
        *results = *mResults;
        return true;
    }

  private:
    std::mutex mMutex;
    std::condition_variable mReady;
    std::optional<android::gui::ScreenCaptureResults> mResults;
};
