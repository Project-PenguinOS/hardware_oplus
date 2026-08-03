package com.oplus.view;

import android.animation.Animator;
import android.animation.ValueAnimator;
import android.util.Log;
import android.view.View;
import android.view.animation.AnimationUtils;
import android.view.animation.LinearInterpolator;

import java.util.Collections;
import java.util.Map;
import java.util.WeakHashMap;

/**
 * Software stand-in for ColorOS's RenderThread animation bridge.
 *
 * Gallery's photo page runs its open/close transition through this class. It was
 * absent from oplus-fwk entirely, so opening a picture died at layout time with
 * NoClassDefFoundError out of PhotoContainerSection -> PhotoPageTransitionManager
 * -> RenderAnimator.
 *
 * On stock these hand the animation to RenderThread, which steps it independently
 * of the UI thread. There is no such bridge here, so the animations are driven
 * from the UI thread instead: visually the same, just not isolated from main
 * thread jank. All four methods are invoke-static in Gallery.
 *
 * The four entry points were read straight off the call sites:
 *
 *   createRenderValueAnimator - the caller builds a ValueAnimator, attaches its
 *       update listener, passes it in and returns whatever comes back. Handing
 *       the same animator back is therefore the correct pass-through; returning
 *       null would propagate a null Animator to the transition manager.
 *
 *   createRtAnimator - wraps an IRtAnimationTarget (always COUIRtAnimationImpl,
 *       COUI's fork of AndroidX DynamicAnimation). Returning an inert animator
 *       would leave the spring never stepped and the view parked at its start
 *       value, so instead a ValueAnimator is used purely as a frame ticker that
 *       calls doFrame() until the spring reports it has settled.
 *
 *   animateToFinalPosition - stock addresses the target through the animator it
 *       created, so the association is kept here in a weak map and forwarded.
 *
 *   getFrameNumber - a RenderThread frame counter with no meaning off
 *       RenderThread; the caller only uses it as an alternative to a value it
 *       already holds, so 0 is a safe answer.
 */
public class OplusRenderNodeAnimator {
    private static final String TAG = "OplusRenderNodeAnimator";

    /**
     * Upper bound on how long the frame ticker runs. A spring normally reports
     * settled long before this; the bound only stops a mis-stepped animation
     * from ticking forever.
     */
    private static final long MAX_SPRING_DURATION_MS = 5000L;

    /**
     * Animator -> target, so animateToFinalPosition() can reach the spring it
     * belongs to. Weak keys: the entry dies with the animator.
     */
    private static final Map<Animator, IRtAnimationTarget> sTargets =
            Collections.synchronizedMap(new WeakHashMap<Animator, IRtAnimationTarget>());

    private OplusRenderNodeAnimator() {
    }

    public static Animator createRenderValueAnimator(Animator animator, View view) {
        return animator;
    }

    public static Animator createRtAnimator(final IRtAnimationTarget target, View view) {
        final ValueAnimator animator = ValueAnimator.ofFloat(0f, 1f);
        animator.setDuration(MAX_SPRING_DURATION_MS);
        animator.setInterpolator(new LinearInterpolator());

        if (target != null) {
            animator.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
                @Override
                public void onAnimationUpdate(ValueAnimator source) {
                    boolean finished;
                    try {
                        // Millisecond frame times, matching the AndroidX
                        // DynamicAnimation contract COUISpringAnimation forked.
                        finished = target.doFrame(AnimationUtils.currentAnimationTimeMillis());
                    } catch (Throwable t) {
                        // Do not take Gallery down over a transition: end the
                        // animation so the view lands on its final value. Logged
                        // rather than swallowed so it stays diagnosable.
                        Log.w(TAG, "doFrame failed, ending animation", t);
                        finished = true;
                    }
                    if (finished) {
                        source.end();
                    }
                }
            });
            sTargets.put(animator, target);
        }

        return animator;
    }

    public static void animateToFinalPosition(Animator animator, float finalPosition) {
        IRtAnimationTarget target = sTargets.get(animator);
        if (target == null) {
            return;
        }
        try {
            target.animateToFinalPosition(finalPosition);
        } catch (Throwable t) {
            Log.w(TAG, "animateToFinalPosition failed", t);
        }
    }

    public static long getFrameNumber(Animator animator) {
        return 0L;
    }
}
