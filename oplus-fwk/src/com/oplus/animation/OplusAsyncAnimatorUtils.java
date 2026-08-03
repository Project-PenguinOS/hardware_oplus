package com.oplus.animation;

import android.graphics.Outline;
import android.graphics.Rect;
import android.graphics.RenderNode;
import android.view.View;
import android.view.ViewOutlineProvider;

/**
 * Software stand-in for ColorOS's asynchronous property setters.
 *
 * On stock these hand a property change to RenderThread so it lands without a
 * UI-thread frame. Gallery's photo open/close transition drives its scale,
 * translation, alpha and outline through here, so with the class missing the
 * transition died with NoClassDefFoundError out of
 * PhotoClipBoundTransitionView.setViewScaleAsync.
 *
 * Every call site discards the boolean -- invoke-static straight to return-void,
 * no move-result -- so there is no caller-side fallback to rely on. Returning
 * false without doing anything would silently leave the view parked at its old
 * scale. These therefore *perform* the change synchronously and report true:
 * correct whether or not a caller inspects the result, just costing a normal UI
 * frame instead of an RT-side update.
 */
public class OplusAsyncAnimatorUtils {

    private OplusAsyncAnimatorUtils() {
    }

    public static boolean setAlpha(View view, float alpha) {
        if (view == null) {
            return false;
        }
        view.setAlpha(alpha);
        return true;
    }

    public static boolean setAlpha(RenderNode renderNode, float alpha) {
        if (renderNode == null) {
            return false;
        }
        renderNode.setAlpha(alpha);
        return true;
    }

    public static boolean setScaleX(View view, float scaleX) {
        if (view == null) {
            return false;
        }
        view.setScaleX(scaleX);
        return true;
    }

    public static boolean setScaleY(View view, float scaleY) {
        if (view == null) {
            return false;
        }
        view.setScaleY(scaleY);
        return true;
    }

    public static boolean setTranslationX(View view, float translationX) {
        if (view == null) {
            return false;
        }
        view.setTranslationX(translationX);
        return true;
    }

    public static boolean setTranslationY(View view, float translationY) {
        if (view == null) {
            return false;
        }
        view.setTranslationY(translationY);
        return true;
    }

    /**
     * Round-rect outline used by the photo transition to round the corners of
     * the growing/shrinking image. Applied through a ViewOutlineProvider, which
     * is the public equivalent of the RenderNode outline stock sets directly.
     * clipToOutline is deliberately left alone -- the caller owns that.
     */
    public static boolean setOutlineRoundRect(View view, Rect bounds, float radius, float alpha) {
        if (view == null || bounds == null) {
            return false;
        }

        final Rect outlineBounds = new Rect(bounds);
        final float outlineRadius = radius;
        final float outlineAlpha = alpha;

        view.setOutlineProvider(new ViewOutlineProvider() {
            @Override
            public void getOutline(View v, Outline outline) {
                outline.setRoundRect(outlineBounds, outlineRadius);
                outline.setAlpha(outlineAlpha);
            }
        });
        view.invalidateOutline();
        return true;
    }
}
