package com.oplus.wrapper.app.prediction;

import android.content.pm.ShortcutInfo;
import android.os.Parcelable;
import android.os.UserHandle;

public class AppTarget {
    private final android.app.prediction.AppTarget mAppTarget;

    public AppTarget(android.app.prediction.AppTarget appTarget) {
        mAppTarget = appTarget;
    }

    public android.app.prediction.AppTarget getAppTarget() {
        return mAppTarget;
    }

    public ShortcutInfo getShortcutInfo() {
        return mAppTarget.getShortcutInfo();
    }

    public String getClassName() {
        return mAppTarget.getClassName();
    }

    public String getPackageName() {
        return mAppTarget.getPackageName();
    }

    public UserHandle getUser() {
        return mAppTarget.getUser();
    }

    public Parcelable getParcelable() {
        return mAppTarget;
    }
}
