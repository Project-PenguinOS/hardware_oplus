package com.oplus.wrapper.bluetooth;

/* Wrapper over the system-API connection state of a BluetoothDevice. */
public class BluetoothDevice {
    private final android.bluetooth.BluetoothDevice mDevice;

    public BluetoothDevice(android.bluetooth.BluetoothDevice device) {
        mDevice = device;
    }

    public boolean isConnected() {
        return mDevice != null && mDevice.isConnected();
    }
}
