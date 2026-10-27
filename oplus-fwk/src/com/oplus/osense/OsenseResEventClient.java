package com.oplus.osense;

import com.oplus.osense.eventinfo.EventConfig;
import com.oplus.osense.eventinfo.OsenseEventCallback;
import com.oplus.osense.task.BgRunningCallback;
import android.os.Bundle;
import android.content.Context;

public class OsenseResEventClient {

    private static OsenseResEventClient sInstance;

    public static OsenseResEventClient getInstance() {
        if (sInstance == null) {
            sInstance = new OsenseResEventClient();
        }
        return sInstance;
    }

    public int registerEventCallback(OsenseEventCallback callback, EventConfig eventConfig) {
        return 0;
    }

    public int unregisterEventCallback(OsenseEventCallback callback, EventConfig eventConfig) {
        return 0;
    }

    public int unregisterEventCallback(OsenseEventCallback callback) {
        return 0;
    }

    public void requestSceneAction(Bundle bundle) {
        return;
    }

    public int requestInstantCpuLoad() {
        return 0;
    }

    public void startBackgroundRunning(Context context, int bgMode, BgRunningCallback callback) {
        return;
    }

    public boolean stopBackgroundRunning(Context context, int requestId) {
        return true;
    }
}
