package com.oplus.graphics;

import android.graphics.Paint;
import android.graphics.RectF;

/* loaded from: classes.dex */
public interface IOplusCanvas {
    default void drawSmoothRoundRect(float left, float top, float right, float bottom, float rx, float ry, Paint paint, float weight) {
    }

    default void drawSmoothRoundRect(float left, float top, float right, float bottom, float rx, float ry, Paint paint) {
    }

    default void drawSmoothRoundRect(RectF rect, float rx, float ry, Paint paint, float weight) {
    }

    default void drawSmoothRoundRect(RectF rect, float rx, float ry, Paint paint) {
    }
}
