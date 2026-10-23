/*
 * Copyright (C) 2026 The PenguinOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.pen.sdk;

import org.lineageos.pen.sdk.IPenSdkCallback;

/**
 * Bound with action org.lineageos.pen.SDK on package org.lineageos.pen.
 * Requires org.lineageos.pen.permission.PEN_SDK.
 */
interface IPenSdk {
    int getVersion();

    boolean isConnected();

    /** Battery percentage, or -1 when unknown. */
    int getBatteryLevel();

    boolean isCharging();

    /** Whether the connected pen accepts vibration commands. */
    boolean supportsVibration();

    void registerCallback(IPenSdkCallback callback);

    void unregisterCallback(IPenSdkCallback callback);

    /**
     * Writing feedback texture: type 0 pencil, 1 eraser, 2 ballpoint, 3 fountain pen, 4 marker.
     * Level 0 turns writing feedback off, 1..3 sets its strength. Kept across reconnects.
     */
    oneway void setVibration(int type, int level);

    /**
     * Start (stylus down) or stop (stylus up/cancel) writing feedback for one stroke.
     * Feedback stops automatically if the process owning token dies.
     */
    oneway void setWritingFeedback(IBinder token, boolean active);

    /** One short confirmation buzz. */
    oneway void vibrateOnce();
}
