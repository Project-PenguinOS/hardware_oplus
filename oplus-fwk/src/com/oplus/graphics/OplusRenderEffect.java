package com.oplus.graphics;

import android.graphics.RenderEffect;

/**
 * Stand-in for ColorOS's gradient blur factory.
 *
 * Gallery builds a gradient blur here and hands it to
 * OplusViewBackgroundRenderEffect.setBackgroundRenderEffect(), which is a no-op
 * in this tree, so the result is never consumed.
 *
 * Returning null rather than approximating with RenderEffect.createBlurEffect():
 * the seven parameters are unlabelled in the dex and which of them are the two
 * blur radii versus tile mode, colour and direction is not something the call
 * site reveals. Passing a wrongly-parameterised effect into the framework is a
 * worse failure than no effect. null is safe here -- the only consumer ignores
 * it, and View.setRenderEffect(null) is itself legal and simply clears any
 * effect.
 */
public class OplusRenderEffect {

    private OplusRenderEffect() {
    }

    public static RenderEffect createGradientBlurEffect(
            float radiusX,
            float radiusY,
            boolean vertical,
            float fraction,
            int startColor,
            int endColor,
            int tileMode) {
        return null;
    }
}
