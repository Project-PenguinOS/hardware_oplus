package com.oplus.view.material;

import android.view.View;

/*
 * Stub for the ColorOS RenderNode material effects used by COUI (corner
 * smoothing, edge strokes, caustic shadows). AOSP's RenderNode has no such
 * effects; every setter reports "not applied" so COUI keeps its plain
 * drawable path.
 */
public class OplusMaterialUtil {
    private OplusMaterialUtil() {
    }

    public static boolean setCornerParams(View view, OplusMaterialCornerParams params) {
        return false;
    }

    public static boolean setEdgeParams(View view, OplusMaterialEdgeParams params) {
        return false;
    }

    public static boolean setShadowParams(View view, OplusMaterialShadowParams params) {
        return false;
    }

    public static boolean setOutlineCausticShadowColor(View view, int color) {
        return false;
    }

    public static boolean setOutlineCausticShadowLayout(View view, int left, int top,
            int right, int bottom) {
        return false;
    }

    public static boolean setOutlineCausticShadowParams(View view, float a, float b, float c,
            float d, float e, float f) {
        return false;
    }
}
