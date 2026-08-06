package android.view;

import android.graphics.ColorSpace;
import android.hardware.DataSpace;
import android.os.Process;
import android.util.Log;
import android.window.ScreenCaptureInternal;

import com.oplus.app.OplusScreenShotOptions;
import com.oplus.app.OplusScreenShotResult;

public class OplusWindowManager {

    private static final String TAG = "OplusWindowManager";

    public OplusWindowManager() {}

    public void requestKeyguard(String command) {}
    public boolean setPreferredDisplayMode(int mode) { return false; }

    /**
     * Capture a single SurfaceControl into a HardwareBuffer.
     *
     * Gallery's Perfect Shot builds its candidate strip by screenshotting the
     * editor SurfaceView once per candidate face
     * (SurfaceControlCapturing -> ScreenShotWrapper -> here). Without this
     * method the whole strip comes back empty:
     *
     *   E sysapi-ScreenShotWrapper: [getScreenShot] error
     *       Failed resolution of: Lcom/oplus/app/OplusScreenShotOptions;
     *       com.oplus.aiunit.vision.jyj.a(ScreenShotWrapper.kt:33)
     *   W AIGallery_AIBestTakeSection: takeScreenShot: SurfaceView fetch Bitmap failed
     *
     * On ColorOS this hops through WindowManagerService, which holds
     * READ_FRAME_BUFFER. We run in the caller's process instead, so we rely on
     * SurfaceFlinger's self-capture exemption: validateScreenshotPermissions()
     * accepts the request when captureArgs.uid matches the calling uid, and the
     * layer traversal then keeps only surfaces owned by that uid. That covers
     * an app capturing its own SurfaceView and nothing more.
     *
     * mFullDisplay is therefore not implemented -- a whole-display grab needs
     * READ_FRAME_BUFFER, which an app process does not have. The one caller
     * that sets it (the doodle-engine Toolkit) already null-checks the result.
     */
    public OplusScreenShotResult getScreenshot(OplusScreenShotOptions options) {
        if (options == null || options.mLayer == null) {
            Log.w(TAG, "getScreenshot: no layer to capture"
                    + (options != null && options.mFullDisplay
                            ? ", mFullDisplay is not supported" : ""));
            return null;
        }

        ScreenCaptureInternal.LayerCaptureArgs args =
                new ScreenCaptureInternal.LayerCaptureArgs.Builder(options.mLayer)
                        .setSourceCrop(options.mSourceCrop)
                        .setFrameScale(1.0f)
                        .setUid(Process.myUid())
                        .build();

        ScreenCaptureInternal.ScreenshotHardwareBuffer capture =
                ScreenCaptureInternal.captureLayers(args);
        if (capture == null || capture.getHardwareBuffer() == null) {
            Log.w(TAG, "getScreenshot: captureLayers returned nothing for " + options.mLayer);
            return null;
        }

        OplusScreenShotResult result = new OplusScreenShotResult();
        result.mHardwareBuffer = capture.getHardwareBuffer();
        ColorSpace colorSpace = capture.getColorSpace();
        result.mColorSpaceNamed = colorSpace != null
                ? colorSpace.getDataSpace() : DataSpace.DATASPACE_SRGB;
        return result;
    }
}
