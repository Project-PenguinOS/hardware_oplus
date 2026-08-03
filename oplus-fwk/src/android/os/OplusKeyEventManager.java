package android.os;

import android.content.Context;
import android.view.KeyEvent;

/**
 * Stub for ColorOS's global key-event observer service.
 *
 * Only the OnKeyEventObserver interface used to exist here, which was enough
 * while nothing reached the manager itself. Gallery's PhotoBrightenSection --
 * the HDR "brighten" overlay behind persist.sys.feature.hdr_vision_app -- calls
 * getInstance() the moment that section starts, so enabling ProXDR turned a
 * dormant reference into a hard NoSuchMethodError at Fragment.performStart and
 * Gallery force-closed on every launch.
 *
 * Stock backs this with a system service that multiplexes key events to
 * registered observers; there is no such service in this tree, so registration
 * cannot succeed and register/unregister report false. Both call sites in
 * Gallery discard the result -- they invoke-virtual and fall straight through
 * to return-void -- so the section simply never receives key events.
 * getInstance() must never return null: the caller does move-result-object
 * followed immediately by invoke-virtual with no null check.
 *
 * The observer is only a convenience path (driving the brighten overlay from a
 * hardware key); the overlay itself works through normal touch handling.
 */
public class OplusKeyEventManager {

    public interface OnKeyEventObserver {
        void onKeyEvent(KeyEvent event);
    }

    private static final OplusKeyEventManager sInstance = new OplusKeyEventManager();

    public OplusKeyEventManager() {
    }

    public static OplusKeyEventManager getInstance() {
        return sInstance;
    }

    public boolean registerKeyEventObserver(Context context, OnKeyEventObserver observer, int types) {
        return false;
    }

    public boolean unregisterKeyEventObserver(Context context, OnKeyEventObserver observer) {
        return false;
    }
}
