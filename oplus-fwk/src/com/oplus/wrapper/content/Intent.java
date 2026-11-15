package com.oplus.wrapper.content;

/* Wrapper over the hidden Intent constants. */
public class Intent {
    public static final String EXTRA_USER_HANDLE = getExtraUserHandle();

    private Intent() {
    }

    private static String getExtraUserHandle() {
        return android.content.Intent.EXTRA_USER_HANDLE;
    }
}
