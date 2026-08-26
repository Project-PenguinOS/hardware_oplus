package android.bluetooth;

import android.bluetooth.le.ScanResult;

public abstract class OplusBluetoothRssiDetectCallback {
    public void onRssiDetectResultCallback(ScanResult result, float modifRssi) {
    }

    public void onRssiDetectDistanceCallback(ScanResult result, float modifRssi, float distance) {
    }
}
