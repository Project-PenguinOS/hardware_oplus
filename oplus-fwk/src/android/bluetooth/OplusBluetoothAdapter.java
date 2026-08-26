package android.bluetooth;

import java.util.List;

public class OplusBluetoothAdapter {

    private static OplusBluetoothAdapter sAdapter;

    public static synchronized OplusBluetoothAdapter getOplusBluetoothAdapter() {
        if (sAdapter == null) {
            sAdapter = new OplusBluetoothAdapter();
        }
        return sAdapter;
    }

    public boolean registerOplusBluetoothRssiDetectCallback(
            OplusBluetoothRssiDetectCallback callback) {
        return false;
    }

    public boolean registerFilteredBluetoothRssiDetectCallback(
            OplusBluetoothRssiDetectCallback callback, List<?> filters) {
        return false;
    }

    public boolean unregisterOplusBluetoothRssiDetectCallback(
            OplusBluetoothRssiDetectCallback callback) {
        return false;
    }

    public boolean setPageScanInterval(int interval) {
        return false;
    }
}
