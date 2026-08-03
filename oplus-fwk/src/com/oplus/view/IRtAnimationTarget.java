package com.oplus.view;

/**
 * Animation target driven by OplusRenderNodeAnimator.
 *
 * On stock this is what ColorOS hands to RenderThread so a spring animation can
 * be stepped off the UI thread. The shape below is taken from the only class in
 * Gallery that implements it,
 * com.coui.appcompat.animation.dynamicanimation.COUIRtAnimationImpl, which
 * declares exactly these eight methods -- so the interface is modelled, not
 * guessed. Both createRtAnimator() call sites pass that same class.
 *
 * doFrame() follows the AndroidX DynamicAnimation contract this is forked from:
 * it steps the spring and returns true once the animation has settled.
 */
public interface IRtAnimationTarget {
    void animateToFinalPosition(float finalPosition);

    void cancel();

    boolean doFrame(long frameTime);

    void end();

    boolean isRunning();

    void setAnimationHandler();

    void skipToEnd();

    void start();
}
