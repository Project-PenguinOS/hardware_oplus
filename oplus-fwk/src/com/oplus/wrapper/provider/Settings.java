package com.oplus.wrapper.provider;

import android.content.ContentResolver;

/* Wrapper over the hidden per-user Settings accessors. */
public class Settings {
    private Settings() {
    }

    public static final class System {
        private System() {
        }

        public static int getIntForUser(ContentResolver cr, String name, int def, int userHandle) {
            return android.provider.Settings.System.getIntForUser(cr, name, def, userHandle);
        }
    }
}
