package com.oplus.bluetooth;

import android.bluetooth.BluetoothDevice;

public abstract class OplusA2dpSharingStateCallback {
    public static final int SHARING_NO_ERROR = 0;
    public static final int SHARING_FAILED_ALREADY_STARTED = 1;
    public static final int SHARING_FAILED_FEATURE_UNSUPPORTED = 2;
    public static final int SHARING_FAILED_INTERNAL_ERROR = 3;
    public static final int SHARING_STATUS_SUCCESS = 0;
    public static final int SHARING_STATUE_FAILED = 1;

    public void onSharingStarted(int status, int errCode, int sharingType,
            BluetoothDevice primary, BluetoothDevice secondary) {
    }

    public void onSharingStopped(int status, int errCode, int sharingType,
            BluetoothDevice primary, BluetoothDevice secondary) {
    }
}
