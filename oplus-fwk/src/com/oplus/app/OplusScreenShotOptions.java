/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stub for stock com.oplus.app.OplusScreenShotOptions (oplus-framework.jar).
 *
 * Plain field holder -- every caller we have writes the fields directly and
 * hands the object to OplusWindowManager.getScreenshot(). Gallery uses exactly
 * mLayer + mFullDisplay + mSourceCrop, and so does the doodle-engine Toolkit,
 * so there is nothing else to model here.
 */
package com.oplus.app;

import android.graphics.Rect;
import android.view.SurfaceControl;

public class OplusScreenShotOptions {

    /** The single layer to capture. Ignored when mFullDisplay is true. */
    public SurfaceControl mLayer;

    /** Capture the whole display instead of mLayer. */
    public boolean mFullDisplay;

    /** Region of the source to capture, in layer/display pixels. */
    public Rect mSourceCrop;

    public OplusScreenShotOptions() {}
}
