package com.oplus.animation;

import android.content.Context;
import android.os.Bundle;
import android.view.View;

/**
 * Stand-in for ColorOS's seamless cross-activity view handoff.
 *
 * Only COUIFloatingButtonSeamlessImpl reaches this, so it is off the photo-page
 * path -- included because the class is referenced and would otherwise be the
 * next NoClassDefFoundError the moment a floating action button animates.
 *
 * Handing a view to the system compositor to continue across a transition needs
 * ColorOS window-manager support that is absent here, so this reports false =
 * "seamless unavailable" and the caller keeps its ordinary animation. The
 * callback is deliberately not invoked: false means the handoff never started,
 * so signalling completion would be a lie.
 *
 * AnimationCallback is left empty on purpose -- nothing in Gallery calls through
 * the interface, and an implementing class having more methods than the
 * interface declares is fine, whereas guessing method names that implementers
 * must match is not.
 */
public class OplusViewSeamless {

    public interface AnimationCallback {
    }

    private OplusViewSeamless() {
    }

    public static boolean setSeamlessView(
            View view, Context context, Bundle bundle, AnimationCallback callback) {
        return false;
    }
}
