package com.oplus.osense.complexscene;

import java.util.List;

/*
 * Stub for the ColorOS complex-scene (floating/zoom window) observer.
 *
 * The COS17 OplusCamera subclasses this in FloatingWindowDetectHelper's
 * constructor, so a missing class kills the camera with NoClassDefFoundError.
 * OplusActivityManager never registers it, so onChanged() is never called.
 */
public class OplusComplexSceneObserver {
    public OplusComplexSceneObserver() {
    }

    public void onChanged(int sceneType, List<String> pkgList) {
    }
}
