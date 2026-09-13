package com.oplus.shortcuts.chooser;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.ResolveInfo;
import android.os.Bundle;
import android.os.UserHandle;

import java.util.ArrayList;
import java.util.List;

public class DisplayResolveInfo implements TargetInfo {
    private static final String EXTRA_IS_FROM_CHOOSER = "oplus.intent.extra.from_chooser";

    private final ResolveInfo mResolveInfo;
    private final Intent mResolvedIntent;
    private final List<Intent> mSourceIntents = new ArrayList<>();
    private final TargetInfo.IconHolder mDisplayIconHolder = new TargetInfo.SettableIconHolder();
    private final boolean mIsSuspended;
    private volatile CharSequence mDisplayLabel;
    private volatile CharSequence mExtendedInfo;
    private boolean mPinned;

    public static DisplayResolveInfo newDisplayResolveInfo(Intent originalIntent,
            ResolveInfo resolveInfo, Intent resolvedIntent) {
        return newDisplayResolveInfo(originalIntent, resolveInfo, null, null, resolvedIntent);
    }

    public static DisplayResolveInfo newDisplayResolveInfo(Intent originalIntent,
            ResolveInfo resolveInfo, CharSequence displayLabel, CharSequence extendedInfo,
            Intent resolvedIntent) {
        return new DisplayResolveInfo(originalIntent, resolveInfo, displayLabel, extendedInfo,
                resolvedIntent);
    }

    private DisplayResolveInfo(Intent originalIntent, ResolveInfo resolveInfo,
            CharSequence displayLabel, CharSequence extendedInfo, Intent resolvedIntent) {
        mSourceIntents.add(originalIntent);
        mResolveInfo = resolveInfo;
        mDisplayLabel = displayLabel;
        mExtendedInfo = extendedInfo;
        ActivityInfo ai = mResolveInfo.activityInfo;
        mIsSuspended = (ai.applicationInfo.flags & ApplicationInfo.FLAG_SUSPENDED) != 0;
        mResolvedIntent = createResolvedIntent(resolvedIntent, ai);
    }

    private DisplayResolveInfo(DisplayResolveInfo other, Intent baseIntentToSend) {
        mSourceIntents.addAll(other.getAllSourceIntents());
        mResolveInfo = other.mResolveInfo;
        mIsSuspended = other.mIsSuspended;
        mDisplayLabel = other.mDisplayLabel;
        mExtendedInfo = other.mExtendedInfo;
        mResolvedIntent = createResolvedIntent(
                baseIntentToSend == null ? other.mResolvedIntent : baseIntentToSend,
                mResolveInfo.activityInfo);
        mDisplayIconHolder.setDisplayIcon(other.mDisplayIconHolder.getDisplayIcon());
    }

    private DisplayResolveInfo(DisplayResolveInfo other) {
        mSourceIntents.addAll(other.getAllSourceIntents());
        mResolveInfo = other.mResolveInfo;
        mIsSuspended = other.mIsSuspended;
        mDisplayLabel = other.mDisplayLabel;
        mExtendedInfo = other.mExtendedInfo;
        mResolvedIntent = other.mResolvedIntent;
        mDisplayIconHolder.setDisplayIcon(other.mDisplayIconHolder.getDisplayIcon());
    }

    private static Intent createResolvedIntent(Intent resolvedIntent, ActivityInfo ai) {
        Intent result = new Intent(resolvedIntent);
        result.addFlags(Intent.FLAG_ACTIVITY_FORWARD_RESULT
                | Intent.FLAG_ACTIVITY_PREVIOUS_IS_TOP);
        result.setComponent(new ComponentName(ai.applicationInfo.packageName, ai.name));
        return result;
    }

    @Override
    public final boolean isDisplayResolveInfo() {
        return true;
    }

    @Override
    public ResolveInfo getResolveInfo() {
        return mResolveInfo;
    }

    @Override
    public CharSequence getDisplayLabel() {
        return mDisplayLabel;
    }

    public boolean hasDisplayLabel() {
        return mDisplayLabel != null;
    }

    public void setDisplayLabel(CharSequence displayLabel) {
        mDisplayLabel = displayLabel;
    }

    public void setExtendedInfo(CharSequence extendedInfo) {
        mExtendedInfo = extendedInfo;
    }

    @Override
    public TargetInfo.IconHolder getDisplayIconHolder() {
        return mDisplayIconHolder;
    }

    @Override
    public DisplayResolveInfo tryToCloneWithAppliedRefinement(Intent proposedRefinement) {
        Intent matchingBase = getAllSourceIntents().stream()
                .filter(i -> i.filterEquals(proposedRefinement))
                .findFirst()
                .orElse(null);
        if (matchingBase == null) {
            return null;
        }
        return new DisplayResolveInfo(this,
                TargetInfo.mergeRefinementIntoMatchingBaseIntent(matchingBase, proposedRefinement));
    }

    @Override
    public List<Intent> getAllSourceIntents() {
        return mSourceIntents;
    }

    @Override
    public ArrayList<DisplayResolveInfo> getAllDisplayTargets() {
        return new ArrayList<>(List.of(this));
    }

    public void addAlternateSourceIntent(Intent alt) {
        mSourceIntents.add(alt);
    }

    @Override
    public CharSequence getExtendedInfo() {
        return mExtendedInfo;
    }

    @Override
    public Intent getResolvedIntent() {
        return mResolvedIntent;
    }

    @Override
    public ComponentName getResolvedComponentName() {
        return new ComponentName(mResolveInfo.activityInfo.packageName,
                mResolveInfo.activityInfo.name);
    }

    @Override
    public boolean startAsCaller(Activity activity, Bundle options, int userId) {
        TargetInfo.prepareIntentForCrossProfileLaunch(activity, mResolvedIntent, userId);
        mResolvedIntent.putExtra(EXTRA_IS_FROM_CHOOSER, true);
        TargetInfo.refreshIntentCreatorToken(mResolvedIntent);
        activity.startActivityAsCaller(mResolvedIntent, options, false, userId);
        return true;
    }

    @Override
    public boolean startAsUser(Activity activity, Bundle options, UserHandle user) {
        TargetInfo.prepareIntentForCrossProfileLaunch(activity, mResolvedIntent,
                user.getIdentifier());
        mResolvedIntent.putExtra(EXTRA_IS_FROM_CHOOSER, true);
        TargetInfo.refreshIntentCreatorToken(mResolvedIntent);
        activity.startActivityAsUser(mResolvedIntent, options, user);
        return false;
    }

    @Override
    public Intent getTargetIntent() {
        return mResolvedIntent;
    }

    @Override
    public boolean isSuspended() {
        return mIsSuspended;
    }

    @Override
    public boolean isPinned() {
        return mPinned;
    }

    public void setPinned(boolean pinned) {
        mPinned = pinned;
    }

    public DisplayResolveInfo copy() {
        return new DisplayResolveInfo(this);
    }
}
