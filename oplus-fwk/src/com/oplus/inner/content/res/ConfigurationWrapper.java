package com.oplus.inner.content.res;

import android.content.res.Configuration;

// COUIThemeOverlay (getCOUITheme/isRejectTheme) reads the ColorOS theme state through
// this accessor. On stock it casts Configuration to OplusBaseConfiguration; AOSP has no
// such subclass, so every value reads as unset and the setters have nowhere to write.
// That is the correct answer here -- it makes COUI fall through to its default theme
// instead of throwing ClassNotFoundException and abandoning the overlay entirely.
//
// Do NOT add the com.color.inner counterpart: stock does not define it either, and COUI
// probes that legacy name first precisely to detect the older platform. Resolving it
// would push COUI down a code path this ROM cannot satisfy.
public class ConfigurationWrapper {

    public static int getAccessibleChanged(Configuration configuration) {
        return 0;
    }

    public static String getCustomThemePath(Configuration configuration) {
        return null;
    }

    public static int getFlipFont(Configuration configuration) {
        return 0;
    }

    public static String getIconPackName(Configuration configuration) {
        return null;
    }

    public static long getMaterialColor(Configuration configuration) {
        return 0L;
    }

    public static int getThemeChanged(Configuration configuration) {
        return 0;
    }

    public static long getThemeChangedFlags(Configuration configuration) {
        return 0L;
    }

    public static long getUxIconConfig(Configuration configuration) {
        return 0L;
    }

    public static boolean isDarkModeIconOpen(Configuration configuration) {
        return ((getUxIconConfig(configuration) >> 61) & 1L) == 1L;
    }

    public static void setAccessibleChanged(Configuration configuration, int accessibleChanged) {
    }

    public static void setCustomThemePath(Configuration configuration, String customThemePath) {
    }

    public static void setDarkModeStyleArgs(Configuration configuration, float saturation,
            float contrast, float brightness) {
    }

    public static void setFlipFont(Configuration configuration, int flipFont) {
    }

    public static void setIconPackName(Configuration configuration, String iconPackName) {
    }

    public static void setMaterialColor(Configuration configuration, long materialColor) {
    }

    public static void setThemeChanged(Configuration configuration, int themeChanged) {
    }

    public static void setThemeChangedFlags(Configuration configuration, long themeChangedFlags) {
    }

    public static void setUxIconConfig(Configuration configuration, long uxIconConfig) {
    }

    public static void toggleDarkModeIconConfig(Configuration configuration) {
    }
}
