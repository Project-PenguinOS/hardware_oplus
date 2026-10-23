/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.pen

import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattConnectionSettings
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.os.Handler
import android.util.Log

class PenGattSession(
    val device: BluetoothDevice,
    private val touchHal: TouchHal,
    private val handler: Handler,
    private val onReady: (PenGattSession) -> Unit,
    private val onClosed: (PenGattSession) -> Unit,
    private val onPencilStatus: (PenGattSession, Boolean) -> Unit,
    private val onEvent: (PenGattSession, PenEvent) -> Unit,
) {
    private var gatt: BluetoothGatt? = null
    private var pressChar: BluetoothGattCharacteristic? = null
    private var speedChar: BluetoothGattCharacteristic? = null
    private var interfaceChar: BluetoothGattCharacteristic? = null

    private val pendingCccs = ArrayDeque<BluetoothGattDescriptor>()

    var isPencilActive: Boolean? = null
        private set

    private var isReady = false
    private var isClosed = false
    private var pressPacketCount = 0

    private val pendingWrites = ArrayDeque<Pair<BluetoothGattCharacteristic, ByteArray>>()
    private var isWriteInFlight = false

    private val callback =
        object : BluetoothGattCallback() {
            override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
                when (newState) {
                    BluetoothProfile.STATE_CONNECTED -> {
                        if (!gatt.discoverServices()) {
                            Log.e(TAG, "discoverServices failed")
                            close()
                        }
                    }
                    BluetoothProfile.STATE_DISCONNECTED -> close()
                }
            }

            override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Log.e(TAG, "Service discovery failed: $status")
                    close()
                    return
                }
                val service = gatt.getService(PenProtocol.SERVICE_UUID)
                val press = service?.getCharacteristic(PenProtocol.PRESS_CHAR_UUID)
                val ccc = press?.getDescriptor(PenProtocol.CCC_DESCRIPTOR_UUID)
                if (press != null && ccc != null) {
                    pressChar = press
                    speedChar = service.getCharacteristic(PenProtocol.SPEED_CHAR_UUID)
                    gatt.setCharacteristicNotification(press, true)
                    pendingCccs.addLast(ccc)
                }

                val iface =
                    gatt
                        .getService(PenProtocol.INTERFACE_SERVICE_UUID)
                        ?.getCharacteristic(PenProtocol.INTERFACE_CHAR_UUID)
                val ifaceCcc = iface?.getDescriptor(PenProtocol.CCC_DESCRIPTOR_UUID)
                if (iface != null && ifaceCcc != null) {
                    interfaceChar = iface
                    gatt.setCharacteristicNotification(iface, true)
                    pendingCccs.addLast(ifaceCcc)
                }

                // Double tap, battery and charging notifications are optional extras
                gatt.getService(PenProtocol.EVENT_SERVICE_UUID)?.let { events ->
                    listOf(
                            PenProtocol.DOUBLE_TAP_CHAR_UUID,
                            PenProtocol.BATTERY_CHAR_UUID,
                            PenProtocol.CHARGING_CHAR_UUID,
                        )
                        .forEach { uuid ->
                            val char = events.getCharacteristic(uuid) ?: return@forEach
                            val desc =
                                char.getDescriptor(PenProtocol.CCC_DESCRIPTOR_UUID)
                                    ?: return@forEach
                            gatt.setCharacteristicNotification(char, true)
                            pendingCccs.addLast(desc)
                        }
                }

                if (pressChar == null && interfaceChar == null) {
                    pendingCccs.clear()
                    Log.d(TAG, "${device.address} has no pen services")
                    close()
                    return
                }
                if (!writeNextCcc(gatt)) {
                    close()
                }
            }

            override fun onDescriptorWrite(
                gatt: BluetoothGatt,
                descriptor: BluetoothGattDescriptor,
                status: Int,
            ) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    val uuid = descriptor.characteristic.uuid
                    if (uuid in OPTIONAL_EVENT_CHARS) {
                        Log.w(TAG, "Notifications unavailable on $uuid: $status")
                    } else if (uuid != PenProtocol.INTERFACE_CHAR_UUID || pressChar == null) {
                        Log.e(TAG, "CCC write failed: $status")
                        close()
                        return
                    } else {
                        Log.w(TAG, "Pencil status notifications unavailable: $status")
                        interfaceChar = null
                    }
                }
                if (pendingCccs.isNotEmpty()) {
                    if (!writeNextCcc(gatt)) {
                        close()
                    }
                    return
                }
                if (!gatt.requestMtu(PenProtocol.MTU)) {
                    Log.w(TAG, "requestMtu failed, continuing with default MTU")
                    markReady()
                }
            }

            override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
                Log.d(TAG, "MTU changed to $mtu, status $status")
                markReady()
            }

            override fun onCharacteristicChanged(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                value: ByteArray,
            ) {
                if (characteristic.uuid == PenProtocol.INTERFACE_CHAR_UUID) {
                    val status = PenProtocol.parsePencilStatus(value)
                    if (status == null) {
                        Log.v(TAG, "Interface notify ${value.toHexString()}")
                        return
                    }
                    Log.d(TAG, "Pencil ${if (status) "active" else "idle"}")
                    isPencilActive = status
                    onPencilStatus(this@PenGattSession, status)
                    return
                }
                parseEvent(characteristic, value)?.let {
                    Log.d(TAG, "Pen event $it")
                    onEvent(this@PenGattSession, it)
                    return
                }
                if (characteristic.uuid != PenProtocol.PRESS_CHAR_UUID) {
                    return
                }
                if (PenProtocol.isPressurePacket(value)) {
                    if (pressPacketCount == PenProtocol.KEEP_ALIVE_INTERVAL) {
                        pressPacketCount = 0
                        speedChar?.let { enqueueWrite(it, PenProtocol.KEEP_ALIVE) }
                    } else {
                        pressPacketCount++
                    }
                }
                touchHal.writeBtInfo(value.toHexString())
            }

            override fun onCharacteristicWrite(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                status: Int,
            ) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Log.w(TAG, "Write to ${characteristic.uuid} failed: $status")
                }
                isWriteInFlight = false
                drainWrites()
            }
        }

    fun open() {
        Log.d(TAG, "Probing ${device.address}")
        gatt =
            device.connectGatt(
                BluetoothGattConnectionSettings.Builder()
                    .setTransport(BluetoothDevice.TRANSPORT_LE)
                    .setAutomaticMtuEnabled(false)
                    .build(),
                { handler.post(it) },
                callback,
            )
        if (gatt == null) {
            Log.e(TAG, "connectGatt failed for ${device.address}")
            close()
        }
    }

    fun sendCommand(info: String) {
        handler.post {
            if (!isReady) {
                return@post
            }
            val payload = PenProtocol.decodeCommand(info)
            if (payload == null) {
                Log.e(TAG, "Invalid touch command: $info")
                return@post
            }
            val target = if (PenProtocol.isSpeedCommand(payload)) speedChar else pressChar
            target?.let { enqueueWrite(it, payload) }
        }
    }

    val hasInterface: Boolean
        get() = interfaceChar != null

    fun sendInterfaceCommand(command: ByteArray) {
        handler.post {
            if (!isReady) {
                return@post
            }
            interfaceChar?.let { enqueueWrite(it, command) }
        }
    }

    fun sendPencilStatusAck(isActive: Boolean) {
        handler.post {
            if (!isReady) {
                return@post
            }
            interfaceChar?.let { enqueueWrite(it, PenProtocol.pencilStatusAck(isActive)) }
        }
    }

    fun close() {
        if (isClosed) {
            return
        }
        isClosed = true
        if (isReady) {
            isReady = false
            if (pressChar != null) {
                touchHal.writeBtInfo(PenProtocol.PRESSURE_CLEAR)
                touchHal.writeNode(PenProtocol.NODE_PENCIL_CONNECTED, "0")
                touchHal.unregisterCallback()
            }
            Log.i(TAG, "Pen ${device.address} disconnected")
        }
        pendingWrites.clear()
        pendingCccs.clear()
        gatt?.close()
        gatt = null
        onClosed(this)
    }

    private fun markReady() {
        if (isReady) {
            return
        }
        isReady = true
        pressPacketCount = 0
        if (pressChar != null) {
            touchHal.registerCallback()
            touchHal.writeNode(PenProtocol.NODE_PENCIL_CONNECTED, "0")
            touchHal.writeNode(PenProtocol.NODE_PENCIL_CONNECTED, "1")
        }
        interfaceChar?.let { enqueueWrite(it, PenProtocol.BATTERY_QUERY) }
        Log.i(TAG, "Pen ${device.address} ready")
        onReady(this)
    }

    private fun parseEvent(characteristic: BluetoothGattCharacteristic, value: ByteArray) =
        when {
            value.isEmpty() -> null
            characteristic.uuid == PenProtocol.DOUBLE_TAP_CHAR_UUID -> PenEvent.DoubleTap
            characteristic.uuid == PenProtocol.BATTERY_CHAR_UUID ->
                PenEvent.Battery(value[0].toInt().coerceIn(0, 100))
            characteristic.uuid == PenProtocol.CHARGING_CHAR_UUID ->
                PenEvent.Charging(value[0].toInt() == PenProtocol.CHARGING_STATE_CHARGING)
            else -> null
        }

    private fun writeNextCcc(gatt: BluetoothGatt): Boolean {
        val ccc = pendingCccs.removeFirst()
        val ret = gatt.writeDescriptor(ccc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        if (ret != BluetoothStatusCodes.SUCCESS) {
            Log.e(TAG, "Failed to enable notifications on ${ccc.characteristic.uuid}: $ret")
            return false
        }
        return true
    }

    private fun enqueueWrite(characteristic: BluetoothGattCharacteristic, value: ByteArray) {
        pendingWrites.addLast(characteristic to value)
        drainWrites()
    }

    private fun drainWrites() {
        val gatt = gatt ?: return
        while (!isWriteInFlight) {
            val (characteristic, value) = pendingWrites.removeFirstOrNull() ?: return
            val noResponse =
                characteristic.properties and
                    BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE != 0
            val writeType =
                if (noResponse) {
                    BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                } else {
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                }
            val ret = gatt.writeCharacteristic(characteristic, value, writeType)
            if (ret == BluetoothStatusCodes.SUCCESS) {
                isWriteInFlight = true
            } else {
                Log.w(TAG, "Dropping write to ${characteristic.uuid}: $ret")
            }
        }
    }

    companion object {
        private const val TAG = "OplusPenGatt"

        private val OPTIONAL_EVENT_CHARS =
            setOf(
                PenProtocol.DOUBLE_TAP_CHAR_UUID,
                PenProtocol.BATTERY_CHAR_UUID,
                PenProtocol.CHARGING_CHAR_UUID,
            )
    }
}
