package com.oplus.shortcuts;

import android.content.ComponentName;
import android.content.Context;
import android.content.IntentFilter;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ShortcutInfo;
import android.content.pm.ShortcutManager;
import android.os.AsyncTask;
import android.os.UserHandle;
import android.os.UserManager;
import android.service.chooser.ChooserTarget;
import android.text.TextUtils;
import android.util.Log;

import com.oplus.shortcuts.chooser.DisplayResolveInfo;
import com.oplus.wrapper.app.prediction.AppPredictor;
import com.oplus.wrapper.app.prediction.AppTarget;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;

public class ShortcutLoader {
    private static final String TAG = "ChooserActivity";
    private static final Request NO_REQUEST = new Request(new DisplayResolveInfo[0]);

    private final Context mContext;
    private final AppPredictor mAppPredictor;
    private final AppPredictor.Callback mAppPredictorCallback;
    private final UserHandle mUserHandle;
    private final boolean mIsPersonalProfile;
    private final IntentFilter mTargetIntentFilter;
    private final Executor mBackgroundExecutor;
    private final Executor mCallbackExecutor;
    private final UserManager mUserManager;
    private final ShortcutToChooserTargetConverter mShortcutToChooserTargetConverter =
            new ShortcutToChooserTargetConverter();
    private final AtomicReference<Consumer<Result>> mCallback = new AtomicReference<>();
    private final AtomicReference<Request> mActiveRequest = new AtomicReference<>(NO_REQUEST);

    public ShortcutLoader(Context context, AppPredictor appPredictor, UserHandle userHandle,
            IntentFilter targetIntentFilter, Consumer<Result> callback) {
        this(context, appPredictor, userHandle, true, targetIntentFilter, AsyncTask.SERIAL_EXECUTOR,
                context.getMainExecutor(), callback);
    }

    ShortcutLoader(Context context, AppPredictor appPredictor, UserHandle userHandle,
            boolean isPersonalProfile, IntentFilter targetIntentFilter, Executor backgroundExecutor,
            Executor callbackExecutor, Consumer<Result> callback) {
        mContext = context;
        mAppPredictor = appPredictor;
        mUserHandle = userHandle;
        mIsPersonalProfile = isPersonalProfile;
        mTargetIntentFilter = targetIntentFilter;
        mBackgroundExecutor = backgroundExecutor;
        mCallbackExecutor = callbackExecutor;
        mCallback.set(callback);
        mUserManager = mContext.getSystemService(UserManager.class);

        if (mAppPredictor != null) {
            mAppPredictorCallback = this::onAppPredictorTargets;
            mAppPredictor.registerPredictionUpdates(mCallbackExecutor, mAppPredictorCallback);
        } else {
            mAppPredictorCallback = null;
        }
    }

    public void destroy() {
        if (mCallback.getAndSet(null) != null && mAppPredictor != null) {
            mAppPredictor.unregisterPredictionUpdates(mAppPredictorCallback);
        }
    }

    private boolean isDestroyed() {
        return mCallback.get() == null;
    }

    public void queryShortcuts(DisplayResolveInfo[] appTargets) {
        if (isDestroyed()) {
            return;
        }
        mActiveRequest.set(new Request(appTargets));
        mBackgroundExecutor.execute(this::loadShortcuts);
    }

    private void loadShortcuts() {
        if (!shouldQueryDirectShareTargets()) {
            return;
        }
        Log.d(TAG, "querying direct share targets");
        queryDirectShareTargets(false);
    }

    private void queryDirectShareTargets(boolean skipAppPredictionService) {
        if (isDestroyed()) {
            return;
        }
        if (!skipAppPredictionService && mAppPredictor != null) {
            mAppPredictor.requestPredictionUpdate();
            return;
        }
        if (mTargetIntentFilter == null) {
            return;
        }

        Context selectedProfileContext = mContext.createContextAsUser(mUserHandle, 0);
        List<ShortcutManager.ShareShortcutInfo> shortcuts;
        try {
            // Stock goes through ColorOS's IOplusLauncherAppsManager, which skips the
            // permission check; the AOSP call needs MANAGE_APP_PREDICTIONS on the caller.
            shortcuts = selectedProfileContext.getSystemService(ShortcutManager.class)
                    .getShareTargets(mTargetIntentFilter);
        } catch (SecurityException e) {
            Log.w(TAG, "caller may not query share targets, reporting none", e);
            shortcuts = Collections.emptyList();
        }
        sendShareShortcutInfoList(shortcuts, false, null);
    }

    private void onAppPredictorTargets(List<AppTarget> appPredictorTargets) {
        if (appPredictorTargets.isEmpty() && shouldQueryDirectShareTargets()) {
            queryDirectShareTargets(true);
            return;
        }

        List<AppTarget> shortcutResults = new ArrayList<>();
        for (AppTarget appTarget : appPredictorTargets) {
            if (appTarget.getShortcutInfo() != null) {
                shortcutResults.add(appTarget);
            }
        }
        List<ShortcutManager.ShareShortcutInfo> shortcuts = new ArrayList<>();
        for (AppTarget appTarget : shortcutResults) {
            shortcuts.add(new ShortcutManager.ShareShortcutInfo(appTarget.getShortcutInfo(),
                    new ComponentName(appTarget.getPackageName(), appTarget.getClassName())));
        }
        sendShareShortcutInfoList(shortcuts, true, shortcutResults);
    }

    private void sendShareShortcutInfoList(List<ShortcutManager.ShareShortcutInfo> shortcuts,
            boolean isFromAppPredictor, List<AppTarget> appPredictorTargets) {
        if (appPredictorTargets != null && appPredictorTargets.size() != shortcuts.size()) {
            throw new RuntimeException("resultList and appTargets must have the same size."
                    + " resultList.size()=" + shortcuts.size()
                    + " appTargets.size()=" + appPredictorTargets.size());
        }

        Context selectedProfileContext = mContext.createContextAsUser(mUserHandle, 0);
        for (int i = shortcuts.size() - 1; i >= 0; i--) {
            String packageName = shortcuts.get(i).getTargetComponent().getPackageName();
            if (!isPackageEnabled(selectedProfileContext, packageName)) {
                shortcuts.remove(i);
                if (appPredictorTargets != null) {
                    appPredictorTargets.remove(i);
                }
            }
        }

        HashMap<ChooserTarget, AppTarget> directShareAppTargetCache = new HashMap<>();
        HashMap<ChooserTarget, ShortcutInfo> directShareShortcutInfoCache = new HashMap<>();
        DisplayResolveInfo[] appTargets = mActiveRequest.get().appTargets;
        List<ShortcutResultInfo> resultRecords = new ArrayList<>();
        for (DisplayResolveInfo displayResolveInfo : appTargets) {
            List<ShortcutManager.ShareShortcutInfo> matchingShortcuts =
                    filterShortcutsByTargetComponentName(shortcuts,
                            displayResolveInfo.getResolvedComponentName());
            if (matchingShortcuts.isEmpty()) {
                continue;
            }
            List<ChooserTarget> chooserTargets =
                    mShortcutToChooserTargetConverter.convertToChooserTarget(matchingShortcuts,
                            shortcuts, appPredictorTargets, directShareAppTargetCache,
                            directShareShortcutInfoCache);
            resultRecords.add(new ShortcutResultInfo(displayResolveInfo, chooserTargets));
        }

        postReport(new Result(isFromAppPredictor, appTargets,
                resultRecords.toArray(new ShortcutResultInfo[0]), directShareAppTargetCache,
                directShareShortcutInfoCache));
    }

    private void postReport(Result result) {
        mCallbackExecutor.execute(() -> report(result));
    }

    private void report(Result result) {
        Consumer<Result> callback = mCallback.get();
        if (callback != null) {
            callback.accept(result);
        }
    }

    private boolean shouldQueryDirectShareTargets() {
        return mIsPersonalProfile || isProfileActive();
    }

    protected boolean isProfileActive() {
        return mUserManager.isUserRunning(mUserHandle)
                && mUserManager.isUserUnlocked(mUserHandle)
                && !mUserManager.isQuietModeEnabled(mUserHandle);
    }

    private static boolean isPackageEnabled(Context context, String packageName) {
        if (TextUtils.isEmpty(packageName)) {
            return false;
        }
        try {
            ApplicationInfo appInfo = context.getPackageManager().getApplicationInfo(packageName,
                    PackageManager.ApplicationInfoFlags.of(PackageManager.GET_META_DATA));
            return appInfo != null && appInfo.enabled
                    && (appInfo.flags & ApplicationInfo.FLAG_SUSPENDED) == 0;
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        }
    }

    private static List<ShortcutManager.ShareShortcutInfo> filterShortcutsByTargetComponentName(
            List<ShortcutManager.ShareShortcutInfo> allShortcuts, ComponentName requiredTarget) {
        List<ShortcutManager.ShareShortcutInfo> matchingShortcuts = new ArrayList<>();
        for (ShortcutManager.ShareShortcutInfo shortcut : allShortcuts) {
            if (requiredTarget.equals(shortcut.getTargetComponent())) {
                matchingShortcuts.add(shortcut);
            }
        }
        return matchingShortcuts;
    }

    private static class Request {
        final DisplayResolveInfo[] appTargets;

        Request(DisplayResolveInfo[] targets) {
            appTargets = targets;
        }
    }

    public static class Result {
        public final boolean isFromAppPredictor;
        public final DisplayResolveInfo[] appTargets;
        public final ShortcutResultInfo[] shortcutsByApp;
        public final Map<ChooserTarget, AppTarget> directShareAppTargetCache;
        public final Map<ChooserTarget, ShortcutInfo> directShareShortcutInfoCache;

        public Result(boolean isFromAppPredictor, DisplayResolveInfo[] appTargets,
                ShortcutResultInfo[] shortcutsByApp,
                Map<ChooserTarget, AppTarget> directShareAppTargetCache,
                Map<ChooserTarget, ShortcutInfo> directShareShortcutInfoCache) {
            this.isFromAppPredictor = isFromAppPredictor;
            this.appTargets = appTargets;
            this.shortcutsByApp = shortcutsByApp;
            this.directShareAppTargetCache = directShareAppTargetCache;
            this.directShareShortcutInfoCache = directShareShortcutInfoCache;
        }
    }

    public static class ShortcutResultInfo {
        public final DisplayResolveInfo appTarget;
        public final List<ChooserTarget> shortcuts;

        public ShortcutResultInfo(DisplayResolveInfo appTarget, List<ChooserTarget> shortcuts) {
            this.appTarget = appTarget;
            this.shortcuts = shortcuts;
        }
    }
}
