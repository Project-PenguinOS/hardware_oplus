package android.view;

/*
 * Stub for the ColorOS base class of WindowManager.LayoutParams.
 *
 * COUI dialogs check "attrs instanceof OplusBaseLayoutParams" (or go through
 * OplusTypeCastingHelper) before touching oplusFlags. Our LayoutParams does not
 * extend this class, so those checks fail and the flag is skipped; the class
 * only has to exist so the check itself resolves.
 */
public abstract class OplusBaseLayoutParams {
    public int oplusFlags;
}
