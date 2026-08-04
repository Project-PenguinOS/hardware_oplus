package com.oplus.app.job;

import android.app.job.JobInfo;

/**
 * ColorOS wrapper that hangs two extra scheduling constraints off a stock
 * JobInfo.Builder. AIUnit reaches for it two different ways, and only one of
 * them survives the class being absent:
 *
 *   AISysMonitorManager constructs it directly --
 *
 *     new OplusJobInfo.Builder(builder).setRequiresChargingRestriction(flag);
 *     jobScheduler.schedule(builder.build());
 *
 *   so off ColorOS that line throws NoClassDefFoundError and takes the
 *   schedule() call down with it; the job is never registered at all.
 *
 *   UnitDownLoadManager goes through reflection and catches
 *   ClassNotFoundException, so it degrades quietly -- it just logs and moves on
 *   without applying setRequiresBattIdle.
 *
 * Both constraints are deliberately no-ops rather than forwarded. There is
 * nothing to forward them to: they exist to defer work until the device is
 * charging or the battery is idle, which is enforced by the ColorOS job
 * scheduler extensions that are not part of this build. Implementing them
 * against stock JobInfo.Builder would mean calling setRequiresCharging(true) or
 * setRequiresDeviceIdle(true), which is strictly worse than doing nothing --
 * it would gate the AI model downloads behind the charger, and the download
 * conditions are already the thing that stalls them:
 *
 *   AIUnit-BatteryMonitor: onEvents:battery not charging
 *   -> onConditionNotSatisfied -> cancelTask
 *
 * Keeping them no-ops lets the jobs schedule and run unconditionally, which is
 * the behaviour we want here.
 */
public class OplusJobInfo {

    public static final class Builder {

        private final JobInfo.Builder mBuilder;

        public Builder(JobInfo.Builder builder) {
            mBuilder = builder;
        }

        public void setRequiresBattIdle(boolean requiresBattIdle) {}

        public void setRequiresChargingRestriction(boolean requiresChargingRestriction) {}
    }
}
