/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package android.hardware.display;

import android.os.Bundle;
import android.os.SystemProperties;

/*
 * Stock reads the panel bit depth from the displaypanelfeature HAL in system_server.
 * Gallery only enables its 10-bit/HDR window (and with it the ProXDR badge) when the
 * primary panel reports 10 bits, so publish the depth from a device property instead.
 */
public class OplusDisplayManager {
    private static final String KEY_PRIMARY = "primary_screen_multi_bits";
    private static final String KEY_SECONDARY = "secondary_screen_multi_bits";

    private static final OplusDisplayManager sInstance = new OplusDisplayManager();

    public OplusDisplayManager() {}

    public static OplusDisplayManager getInstance() {
        return sInstance;
    }

    public Bundle getScreenMultiBitsSupport() {
        int bits = SystemProperties.getInt("persist.sys.feature.screen_multi_bits", -1);
        if (bits <= 0) {
            return null;
        }
        Bundle info = new Bundle();
        info.putInt(KEY_PRIMARY, bits);
        info.putInt(KEY_SECONDARY, 0);
        return info;
    }

    public Bundle getScreenProductionTime() {
        return null;
    }

    public void setFakeLux(int displayId, boolean on, int type, int fakeLux) {}

    public void setSpecBrightness(int gear, String reason, int rate) {}
}
