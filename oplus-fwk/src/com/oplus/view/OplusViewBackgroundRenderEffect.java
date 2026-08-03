package com.oplus.view;

import android.graphics.RenderEffect;
import android.view.View;

/**
 * Stand-in for ColorOS's behind-the-window blur hook.
 *
 * Referenced by Gallery's BackgroundBlurView and by COUI's bottom floating
 * toolbar and MaterialEffectManager -- i.e. the chrome of the photo page -- so it
 * sits directly in the path that was already crashing.
 *
 * Stock applies the effect to what is rendered *behind* the view, which needs the
 * ColorOS surface-side blur that does not exist here; see the parked glass-blur
 * work, where ViewRootManager.setBlurParams() being a no-op drops the whole
 * material. Deliberately a no-op rather than a guess: View.setRenderEffect()
 * would blur the view's own content instead of the backdrop, which is visually
 * wrong -- an unblurred toolbar is the honest degradation, and matches how the
 * rest of this ROM already renders.
 */
public class OplusViewBackgroundRenderEffect {

    private OplusViewBackgroundRenderEffect() {
    }

    public static void setBackgroundRenderEffect(RenderEffect renderEffect, View view) {
        // Intentionally empty -- see class javadoc. Null-tolerant by construction.
    }
}
