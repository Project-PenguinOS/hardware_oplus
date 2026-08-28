package android.content;

// On ColorOS this is Intent's superclass, carrying oplus-private flags. AOSP's Intent
// cannot be reparented, so this stands alone: callers reach it through an instanceof-
// guarded cast, which now cleanly returns null instead of raising NoClassDefFoundError.
// The accessors exist for the cases that do get here and must not be the ones to throw.
public class OplusBaseIntent {

    public int getOplusFlags() {
        return 0;
    }

    public void setOplusFlags(int flags) {
    }
}
