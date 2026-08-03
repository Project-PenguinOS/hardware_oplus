package com.oplus.dynamicframerate;

import android.animation.ValueAnimator;

/**
 * Stand-in for ColorOS's dynamic-refresh-rate velocity hint.
 *
 * Stock samples an animator to estimate how fast content is moving and asks the
 * display for a matching refresh rate. Referenced from COUIBottomSheetDialog and
 * Gallery's own animation helper, so it is reachable from the AI editor panels.
 *
 * There is no dynamic-frame-rate service to feed here, so the velocity is
 * reported as 0 -- "no hint" -- and the panel simply animates at whatever rate
 * the display is already running. This only forfeits a refresh-rate
 * optimisation; nothing reads it for correctness.
 */
public class AnimationVelocityCalculator {

    public AnimationVelocityCalculator(ValueAnimator animator) {
    }

    public float calculator(int type, ValueAnimator animator) {
        return 0f;
    }
}
