/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "capture_listener.h"
#include <gtest/gtest.h>

TEST(AlsCaptureListener, MissingCallbackTimesOut) {
    AlsCaptureListener listener;
    android::gui::ScreenCaptureResults results;
    EXPECT_FALSE(listener.waitForResults(&results, std::chrono::milliseconds(10)));
}

TEST(AlsCaptureListener, CompletedCaptureDoesNotWaitForAnotherCallback) {
    AlsCaptureListener listener;
    android::gui::ScreenCaptureResults sent, received;
    ASSERT_TRUE(listener.onScreenCaptureCompleted(sent).isOk());
    EXPECT_TRUE(listener.waitForResults(&received, std::chrono::milliseconds(0)));
}

TEST(AlsCaptureListener, LateCallbackIsSafeAfterTimeout) {
    AlsCaptureListener listener;
    android::gui::ScreenCaptureResults sent, received;
    ASSERT_FALSE(listener.waitForResults(&received, std::chrono::milliseconds(0)));
    ASSERT_TRUE(listener.onScreenCaptureCompleted(sent).isOk());
    EXPECT_TRUE(listener.waitForResults(&received, std::chrono::milliseconds(0)));
}
