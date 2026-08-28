package com.oplus.statistics;

import android.content.Context;

import java.util.Map;

// Oplus telemetry upload. Deliberately inert -- the apps that call it must not crash,
// but nothing should be reported off-device from this ROM.
public class OplusTrack {

    public static void init(Context context) {
    }

    public static boolean onCommon(Context context, String appId, String category,
            String event, Map<String, String> data) {
        return false;
    }
}
