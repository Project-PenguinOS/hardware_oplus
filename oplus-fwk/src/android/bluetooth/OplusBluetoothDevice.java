package android.bluetooth;

public class OplusBluetoothDevice {
    private final BluetoothDevice mBluetoothDevice;

    public OplusBluetoothDevice(BluetoothDevice device) {
        mBluetoothDevice = device;
    }

    public int getOplusBluetoothClass() {
        return 0;
    }

    public boolean setRemoteDelayReport(String[] configKeys, int[] configValues) {
        if (mBluetoothDevice == null || configKeys == null || configValues == null
                || configKeys.length != configValues.length) {
            return false;
        }
        return false;
    }
}
