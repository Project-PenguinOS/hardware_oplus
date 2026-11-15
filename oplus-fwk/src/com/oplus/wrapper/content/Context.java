package com.oplus.wrapper.content;

/* Wrapper over the hidden Context constants. */
public class Context {
    public static final int BIND_FOREGROUND_SERVICE = getBindForegroundService();

    private Context() {
    }

    private static int getBindForegroundService() {
        return android.content.Context.BIND_FOREGROUND_SERVICE;
    }
}
