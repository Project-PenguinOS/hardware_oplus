package android.view;

import android.os.Bundle;

/**
 * Window state callbacks from the ColorOS window manager. Nothing posts them on this ROM; the
 * interface exists so AONService and Gallery, which subclass its Stub, can load.
 */
interface IOplusWindowStateObserver {

    void onWindowStateChange(in Bundle bundle);
}
