package android.app;

import android.os.RemoteException;

public class OplusNotificationManager {

    // Analytics identity for notification tracking. There is no oplus notification
    // service here, so report "no id" rather than a fabricated one.
    public String getStdid(String pkg, int uid, String type) throws RemoteException {
        return "";
    }
}
