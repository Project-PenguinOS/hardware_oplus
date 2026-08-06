package com.oplus.multiuser;

import android.os.UserHandle;

public class OplusMultiUserManager {

    private static OplusMultiUserManager instance;

    private OplusMultiUserManager() {}

    public static OplusMultiUserManager getInstance() {
        if (instance == null) {
            instance = new OplusMultiUserManager();
        }
        return instance;
    }

    public boolean isMultiSystemUserHandle(UserHandle userHandle) {
        return false;
    }

    /**
     * The id-based twin of isMultiSystemUserHandle. Both exist on ColorOS and
     * callers pick whichever they already hold; shipping only the UserHandle one
     * left AIUnit crashing on boot during setup:
     *
     *   java.lang.NoSuchMethodError: No virtual method isMultiSystemUserId(I)Z
     *     in class Lcom/oplus/multiuser/OplusMultiUserManager;
     *   at com.oplus.aiunit.settings.search.AIUnitSearchIndexProvider.a(
     *       AIUnitSearchIndexProvider.kt:61)
     *   at com.oplus.settingslib.provider.OplusSearchIndexablesProvider.query(
     *       OplusSearchIndexablesProvider.java:41)
     *
     * Settings queries every SearchIndexablesProvider while building its search
     * index, so this fires unprompted during first-run setup and takes the whole
     * com.oplus.aiunit process down with it (SIGABRT), repeatedly.
     *
     * false matches isMultiSystemUserHandle: this ROM has no ColorOS multi-system
     * user, so no user id is one.
     */
    public boolean isMultiSystemUserId(int userId) {
        return false;
    }
}
