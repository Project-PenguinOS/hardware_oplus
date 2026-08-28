package com.oplus.flexiblewindow;

import android.app.Activity;

public class FlexibleWindowManager {

    private static final FlexibleWindowManager INSTANCE = new FlexibleWindowManager();

    public static FlexibleWindowManager getInstance() {
        return INSTANCE;
    }

    // 0 is the "not in a flexible window" state; this ROM has no flexible-window mode.
    public int getFlexibleWindowState(Activity activity) {
        return 0;
    }
}
