/*
 * Copyright (C) 2026 The PenguinOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.pen.sdk;

oneway interface IPenSdkCallback {
    void onConnectionChanged(boolean connected);

    void onDoubleTap();

    void onBatteryChanged(int level, boolean charging);
}
