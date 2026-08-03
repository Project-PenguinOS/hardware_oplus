package com.oplus.util;

import android.content.Context;

/**
 * Stub for ColorOS's platform capability tiering.
 *
 * Gallery reaches this through
 * com.oplus.gallery.foundation.util.cpu.PerformanceLevelUtils, but only once the
 * ColorOS API level reports >= 26. While OplusBuild.getOplusOSVERSION() was
 * pinned at 24 the whole branch was dead, so the class was never needed; raising
 * the level so AIUnit's minColorApi checks pass takes Gallery down that path and
 * it crashed with NoClassDefFoundError on launch.
 *
 * Gallery maps the returned int to a PerformanceLevel enum as
 * 3 = HIGH, 2 = MIDDLE, 1 = LOWER, anything else = UNDEF. Reporting HIGH is the
 * honest answer for the devices this tree builds for (SM8750 / 12-16 GB), and it
 * keeps the richer render paths enabled rather than silently degrading effects.
 */
public class OplusPlatformLevelUtils {
    public static final int LEVEL_UNDEF = 0;
    public static final int LEVEL_LOWER = 1;
    public static final int LEVEL_MIDDLE = 2;
    public static final int LEVEL_HIGH = 3;

    private static OplusPlatformLevelUtils sInstance = null;

    private OplusPlatformLevelUtils() {
    }

    public static synchronized OplusPlatformLevelUtils getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new OplusPlatformLevelUtils();
        }
        return sInstance;
    }

    public int getPlatformLevel(int type) {
        return LEVEL_HIGH;
    }

    public int getPlatformAnimationLevel() {
        return LEVEL_HIGH;
    }

    public int getPlatformGaussianLevel() {
        return LEVEL_HIGH;
    }
}
