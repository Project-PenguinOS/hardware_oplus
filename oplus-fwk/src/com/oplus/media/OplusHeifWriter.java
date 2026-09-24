/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stock com.oplus.media.OplusHeifWriter (oplus-framework.jar), rebuilt from its dex.
 *
 * liboplusheifwriter.so's JNI_OnLoad registers these natives and aborts the
 * camera if the class or any Options field is missing. APS's Java HEIF route
 * loads it through libHeifWinBufExchg-jni -> libHeifEncoderWrapper.
 */

package com.oplus.media;

import android.graphics.Bitmap;
import android.util.Log;

import java.io.FileDescriptor;

public class OplusHeifWriter {
    public static final int COLOR_FMT_YUV420Planar = 0;
    public static final int COLOR_FMT_P010 = 1;
    public static final int COLOR_FMT_RGBA8888 = 2;
    public static final int COLOR_FMT_NV12 = 3;
    public static final int COLOR_FMT_NV21 = 4;
    public static final int COLOR_FMT_MAX = 5;

    static final int minValue = 0;
    static final int maxValue = 100;

    private static final String TAG = "OplusHeifWriter_Java";

    private long mNativeObject;

    public static class Options {
        public byte[] fileExtender;
        public FileDescriptor gainmapFd;
        public long mcroVideoPresentationTimestampUs;
        public FileDescriptor videoFd;
        public String xmpData;
    }

    static {
        Log.v(TAG, "loadLibrary");
        System.loadLibrary("oplusheifwriter");
    }

    public OplusHeifWriter() {
        mNativeObject = nativeSetup();
    }

    private static native long nativeSetup();

    private static native long nativeCreate(long nativeObject, int width, int height,
            int strideWidth, int strideHeight, int format, int quality, int rotation);

    private static native long nativeProcessHeicPhotoFrame(long nativeObject, byte[] yuvBuffer,
            byte[] exifData, FileDescriptor fd);

    private static native long nativeCreateLivePhoto(long nativeObject, byte[] yuvBuffer,
            byte[] exifData, FileDescriptor fd, Options opts);

    private static native long nativeCreateLivePhotoByBmp(long nativeObject, Bitmap bmp,
            byte[] exifData, FileDescriptor fd, Options opts);

    private static native void nativeDestory(long nativeObject);

    public boolean createPrimaryImage(int width, int height, int strideWidth, int strideHeight,
            int format, int quality, int rotation) {
        if (quality <= minValue || quality > maxValue) {
            throw new IllegalArgumentException("quality range error");
        }
        if (width <= 0 || height <= 0 || strideWidth <= 0 || strideHeight <= 0
                || format < 0 || format >= COLOR_FMT_MAX) {
            Log.i(TAG, "Input param error.");
            return false;
        }
        long ret = nativeCreate(mNativeObject, width, height, strideWidth, strideHeight, format,
                quality, rotation);
        Log.i(TAG, " OplusHeifWriter start! quality: " + quality);
        return ret >= 0;
    }

    public boolean processPrimaryImage(byte[] yuvBuffer, byte[] exifData, FileDescriptor fd) {
        return nativeProcessHeicPhotoFrame(mNativeObject, yuvBuffer, exifData, fd) >= 0;
    }

    public boolean processPrimaryLivePhoto(Bitmap bmp, byte[] exifData, FileDescriptor fd,
            Options opts) {
        if (nativeCreateLivePhotoByBmp(mNativeObject, bmp, exifData, fd, opts) < 0) {
            Log.i(TAG, "processPrimaryLivePhoto failed!");
            return false;
        }
        return true;
    }

    public boolean processPrimaryLivePhoto(byte[] yuvBuffer, byte[] exifData, FileDescriptor fd,
            Options opts) {
        if (nativeCreateLivePhoto(mNativeObject, yuvBuffer, exifData, fd, opts) < 0) {
            Log.i(TAG, "processPrimaryLivePhoto failed!");
            return false;
        }
        return true;
    }

    public void destory() {
        Log.i(TAG, " OplusHeifWriter destory!");
        nativeDestory(mNativeObject);
        mNativeObject = 0;
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            if (mNativeObject != 0) {
                destory();
                mNativeObject = 0;
            }
        } finally {
            super.finalize();
        }
    }
}
