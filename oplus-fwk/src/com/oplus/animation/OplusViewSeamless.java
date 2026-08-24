package com.oplus.animation;

import android.content.Context;
import android.os.Bundle;
import android.view.View;

/**
 * Stand-in for ColorOS's seamless cross-activity view handoff.
 *
 * COUIFloatingButtonSeamlessImpl (ConsumerIRApp) subclasses AnimationCallback
 * and invokes its constructor. It must be a class, not an interface, or ART
 * throws IncompatibleClassChangeError on first FAB touch.
 *
 * Handing a view to the system compositor to continue across a transition needs
 * ColorOS window-manager support that is absent here, so this reports false =
 * "seamless unavailable" and the caller keeps its ordinary animation. The
 * callback is deliberately not invoked: false means the handoff never started,
 * so signalling completion would be a lie.
 */
public class OplusViewSeamless {

    public static class AnimationCallback {
    }

    private OplusViewSeamless() {
    }

    public static boolean setSeamlessView(
            View view, Context context, Bundle bundle, AnimationCallback callback) {
        return false;
    }
}
