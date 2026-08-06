/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stub for stock com.oplus.media.OplusImageHdrImpl (oplus-framework.jar).
 *
 * Gallery's only route to a photo's Ultra HDR gainmap. OplusImageHdrWrapper
 * (com.oplus.aiunit.vision.r1g) calls straight into this class and has no
 * fallback for the gainmap paths, so without it every photo looks SDR:
 *
 *   E sysapi-OplusImageHdrImplWrapper: getUHdrImageAndInfo is failed
 *       Failed resolution of: Lcom/oplus/media/OplusImageHdrImpl;
 *       com.oplus.aiunit.vision.r1g.d(OplusImageHdrWrapper.kt:22)
 *
 * The wrapper gates every call on SDK_INT >= 34, which is exactly when AOSP
 * grew Bitmap.getGainmap(), so the whole class maps onto android.graphics.
 * Gainmap. The field-for-field mapping below is not guesswork -- Gallery
 * carries its own Gainmap -> UltraHdrInfo converter
 * (com.oplus.gallery.addon.graphics.a.b) and this matches it.
 */
package com.oplus.media;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Gainmap;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.util.Log;

import java.io.FileDescriptor;
import java.io.InputStream;
import java.io.OutputStream;

public class OplusImageHdrImpl {

    private static final String TAG = "OplusImageHdrImpl";

    /** Sample size for the isUHdrImage probe -- we only need presence, not pixels. */
    private static final int PROBE_SAMPLE_SIZE = 8;

    /**
     * Gallery reads mHdrScale straight through to UltraHdrInfo.scale. It also
     * reaches for Gainmap.getHdrScale() reflectively elsewhere and substitutes
     * -1.0f when that ColorOS-only method is missing, so -1.0f is the value the
     * app already treats as "no scale reported".
     */
    private static final float NO_HDR_SCALE = -1.0f;

    public static class GainmapInfo {
        public boolean mIsJpegR;
        public Bitmap mGainmap;
        public float[] mGainmapRatioMin = new float[] {1.0f, 1.0f, 1.0f};
        public float[] mGainmapRatioMax = new float[] {1.0f, 1.0f, 1.0f};
        public float[] mGainmapGamma = new float[] {1.0f, 1.0f, 1.0f};
        public float[] mEpsilonSdr = new float[3];
        public float[] mEpsilonHdr = new float[3];
        public float mDisplayRatioSdr = 1.0f;
        public float mDisplayRatioHdr = 1.0f;
        public float mHdrScale = NO_HDR_SCALE;
        public int mBaseImageType;
        public int mType;

        public GainmapInfo() {}
    }

    /**
     * @param sampleSize BitmapFactory.Options.inSampleSize for the decode; the
     *                   wrapper passes 1 for the InputStream flavour and
     *                   forwards the caller's value for the fd flavour.
     */
    public static GainmapInfo decodeGainmapAndMetadata(FileDescriptor fd, int sampleSize) {
        BitmapFactory.Options options = optionsFor(sampleSize);
        return fromBitmap(BitmapFactory.decodeFileDescriptor(fd, null, options));
    }

    public static GainmapInfo decodeGainmapAndMetadata(InputStream is, int sampleSize) {
        BitmapFactory.Options options = optionsFor(sampleSize);
        return fromBitmap(BitmapFactory.decodeStream(is, null, options));
    }

    /**
     * Backs isUHdrImage(). Only mIsJpegR is read, so decode small.
     */
    public static GainmapInfo demuxFile(FileDescriptor fd) {
        BitmapFactory.Options options = optionsFor(PROBE_SAMPLE_SIZE);
        Bitmap bitmap = BitmapFactory.decodeFileDescriptor(fd, null, options);
        if (bitmap == null) {
            return null;
        }
        GainmapInfo info = new GainmapInfo();
        info.mIsJpegR = bitmap.getGainmap() != null;
        bitmap.recycle();
        return info;
    }

    /**
     * The base (SDR) image with the gainmap dropped, so nothing downstream
     * tone-maps it a second time.
     */
    public static Bitmap decodeBaseJpeg(InputStream is, BitmapFactory.Options options) {
        Bitmap bitmap = BitmapFactory.decodeStream(is, null, options);
        if (bitmap != null) {
            bitmap.setGainmap(null);
        }
        return bitmap;
    }

    /**
     * Serialise a single-channel gainmap. Gainmap contents come back as
     * ALPHA_8 and the caller checks that config before getting here, but the
     * platform encoders will not take an alpha-only bitmap, so flatten it to
     * luminance first.
     */
    public static boolean compressAlpha8(Bitmap bitmap, Bitmap.CompressFormat format,
            int quality, OutputStream out) {
        if (bitmap == null || out == null) {
            return false;
        }
        Bitmap flattened = null;
        try {
            if (bitmap.getConfig() == Bitmap.Config.ALPHA_8) {
                flattened = Bitmap.createBitmap(bitmap.getWidth(), bitmap.getHeight(),
                        Bitmap.Config.ARGB_8888);
                Canvas canvas = new Canvas(flattened);
                canvas.drawColor(Color.BLACK, PorterDuff.Mode.SRC);
                Paint paint = new Paint();
                paint.setColor(Color.WHITE);
                canvas.drawBitmap(bitmap, 0.0f, 0.0f, paint);
                return flattened.compress(format, quality, out);
            }
            return bitmap.compress(format, quality, out);
        } catch (Exception e) {
            Log.w(TAG, "compressAlpha8 failed", e);
            return false;
        } finally {
            if (flattened != null) {
                flattened.recycle();
            }
        }
    }

    private static BitmapFactory.Options optionsFor(int sampleSize) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = sampleSize > 0 ? sampleSize : 1;
        return options;
    }

    /**
     * The base bitmap is deliberately not recycled: callers keep only the
     * gainmap, but recycling here would be the first thing to blame if a
     * decode ever came back sharing pixels, and these decodes are no more
     * frequent than the ones Gallery already does.
     */
    private static GainmapInfo fromBitmap(Bitmap bitmap) {
        if (bitmap == null) {
            return null;
        }
        Gainmap gainmap = bitmap.getGainmap();
        if (gainmap == null) {
            GainmapInfo info = new GainmapInfo();
            info.mIsJpegR = false;
            return info;
        }

        GainmapInfo info = new GainmapInfo();
        info.mIsJpegR = true;
        info.mGainmap = gainmap.getGainmapContents();
        info.mGainmapRatioMin = gainmap.getRatioMin();
        info.mGainmapRatioMax = gainmap.getRatioMax();
        info.mGainmapGamma = gainmap.getGamma();
        info.mEpsilonSdr = gainmap.getEpsilonSdr();
        info.mEpsilonHdr = gainmap.getEpsilonHdr();
        info.mDisplayRatioSdr = gainmap.getMinDisplayRatioForHdrTransition();
        info.mDisplayRatioHdr = gainmap.getDisplayRatioForFullHdr();
        info.mHdrScale = NO_HDR_SCALE;
        // mBaseImageType and mType are ColorOS classifiers with no AOSP
        // equivalent. Gallery's own Gainmap -> UltraHdrInfo converter leaves
        // both at their Kotlin default of 0, so match that.
        info.mBaseImageType = 0;
        info.mType = 0;
        return info;
    }
}
