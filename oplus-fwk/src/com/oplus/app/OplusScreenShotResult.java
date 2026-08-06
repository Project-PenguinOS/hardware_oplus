/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stub for stock com.oplus.app.OplusScreenShotResult (oplus-framework.jar).
 *
 * mColorSpaceNamed is not a ColorSpace.Named ordinal despite the name -- callers
 * feed it straight to ColorSpace.getFromDataSpace(int), so it holds an
 * android.hardware.DataSpace value.
 */
package com.oplus.app;

import android.hardware.HardwareBuffer;

public class OplusScreenShotResult {

    public HardwareBuffer mHardwareBuffer;

    /** android.hardware.DataSpace value describing mHardwareBuffer. */
    public int mColorSpaceNamed;

    public OplusScreenShotResult() {}
}
