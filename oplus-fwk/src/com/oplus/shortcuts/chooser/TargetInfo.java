package com.oplus.shortcuts.chooser;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.content.pm.ShortcutInfo;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.os.IBinder;
import android.os.RemoteException;
import android.os.UserHandle;
import android.text.TextUtils;

import com.oplus.wrapper.app.prediction.AppTarget;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public interface TargetInfo {

    interface IconHolder {
        Drawable getDisplayIcon();

        void setDisplayIcon(Drawable icon);
    }

    final class SettableIconHolder implements IconHolder {
        private Drawable mDisplayIcon;

        @Override
        public Drawable getDisplayIcon() {
            return mDisplayIcon;
        }

        @Override
        public void setDisplayIcon(Drawable icon) {
            mDisplayIcon = icon;
        }
    }

    Intent getResolvedIntent();

    Intent getTargetIntent();

    ComponentName getResolvedComponentName();

    boolean startAsCaller(Activity activity, Bundle options, int userId);

    boolean startAsUser(Activity activity, Bundle options, UserHandle user);

    ResolveInfo getResolveInfo();

    CharSequence getDisplayLabel();

    CharSequence getExtendedInfo();

    IconHolder getDisplayIconHolder();

    List<Intent> getAllSourceIntents();

    ArrayList<DisplayResolveInfo> getAllDisplayTargets();

    boolean isSuspended();

    boolean isPinned();

    TargetInfo tryToCloneWithAppliedRefinement(Intent proposedRefinement);

    default ComponentName getChooserTargetComponentName() {
        return null;
    }

    default boolean hasDisplayIcon() {
        return getDisplayIconHolder().getDisplayIcon() != null;
    }

    default boolean isSimilar(TargetInfo other) {
        if (other == null) {
            return false;
        }
        if (isChooserTargetInfo()) {
            return other.isChooserTargetInfo()
                    && Objects.equals(getChooserTargetComponentName(),
                            other.getChooserTargetComponentName())
                    && TextUtils.equals(getDisplayLabel(), other.getDisplayLabel())
                    && TextUtils.equals(getExtendedInfo(), other.getExtendedInfo());
        }
        return !other.isChooserTargetInfo() && Objects.equals(this, other);
    }

    default float getModifiedScore() {
        return -0.1f;
    }

    default ShortcutInfo getDirectShareShortcutInfo() {
        return null;
    }

    default String getDirectShareShortcutId() {
        ShortcutInfo shortcut = getDirectShareShortcutInfo();
        return shortcut == null ? null : shortcut.getId();
    }

    default AppTarget getDirectShareAppTarget() {
        return null;
    }

    default DisplayResolveInfo getDisplayResolveInfo() {
        return null;
    }

    default boolean isChooserTargetInfo() {
        return false;
    }

    default boolean isDisplayResolveInfo() {
        return false;
    }

    default boolean isMultiDisplayResolveInfo() {
        return false;
    }

    default boolean isSelectableTargetInfo() {
        return false;
    }

    default boolean isNotSelectableTargetInfo() {
        return false;
    }

    default boolean isEmptyTargetInfo() {
        return false;
    }

    default boolean isPlaceHolderTargetInfo() {
        return false;
    }

    default boolean isInDirectShareMetricsCategory() {
        return isChooserTargetInfo();
    }

    // Stock keeps media/contacts URIs unfixed when either side is a multi-app clone;
    // there is no OplusMultiAppManager here, so this is the plain AOSP behaviour.
    static void prepareIntentForCrossProfileLaunch(Activity activity, Intent intent,
            int targetUserId) {
        int callingUserId = UserHandle.getUserId(activity.getLaunchedFromUid());
        if (targetUserId != callingUserId) {
            intent.fixUris(callingUserId);
        }
    }

    static void prepareIntentForCrossProfileLaunch(Intent intent, int targetUserId) {
        int currentUserId = UserHandle.myUserId();
        if (targetUserId != currentUserId) {
            intent.fixUris(currentUserId);
        }
    }

    static void refreshIntentCreatorToken(Intent intent) {
        try {
            IBinder token = ActivityManager.getService()
                    .refreshIntentCreatorToken(intent.cloneForCreatorToken());
            if (token != null) {
                intent.setCreatorToken(token);
            }
        } catch (RemoteException e) {
            throw new RuntimeException("Failure from system", e);
        } catch (SecurityException e) {
            // Only the system chooser may refresh a creator token; callers that cannot
            // launch as caller have nothing to refresh either.
        }
    }

    static Intent mergeRefinementIntoMatchingBaseIntent(Intent base, Intent refinement) {
        Intent merged = new Intent(base);
        merged.fillIn(refinement, Intent.FILL_IN_CLIP_DATA);
        merged.putExtras(refinement);
        return merged;
    }
}
