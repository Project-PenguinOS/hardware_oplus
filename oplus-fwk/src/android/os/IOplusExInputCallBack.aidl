package android.os;

import android.view.InputEvent;

/**
 * Input event callbacks from the ColorOS input service. Nothing posts them on this ROM; the
 * interface exists so Gallery's HeyTap SDK, which subclasses its Stub, can load.
 */
interface IOplusExInputCallBack {

    void onInputEvent(in InputEvent event);
}
