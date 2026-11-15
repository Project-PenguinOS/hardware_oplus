package com.oplus.wrapper.app;

/* Wrapper over the hidden ActivityManager accessors. */
public class ActivityManager {
    private ActivityManager() {
    }

    public static int getCurrentUser() {
        return android.app.ActivityManager.getCurrentUser();
    }
}
