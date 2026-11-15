package com.oplus.wrapper.hardware.camera2;

import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraDevice;

import java.util.concurrent.Executor;

/* Wrapper over the hidden CameraManager calls. */
public class CameraManager {
    private final android.hardware.camera2.CameraManager mManager;

    public CameraManager(android.hardware.camera2.CameraManager manager) {
        mManager = manager;
    }

    public void openCamera(String cameraId, int oomScoreOffset, Executor executor,
            CameraDevice.StateCallback callback) throws CameraAccessException {
        mManager.openCamera(cameraId, oomScoreOffset, executor, callback);
    }
}
