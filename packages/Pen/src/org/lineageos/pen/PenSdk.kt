/*
 * Copyright (C) 2026 The PenguinOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.pen

import android.os.IBinder
import android.os.RemoteCallbackList
import android.util.Log
import java.io.PrintWriter
import org.lineageos.pen.sdk.IPenSdk
import org.lineageos.pen.sdk.IPenSdkCallback

class PenSdk(private val relay: () -> PenRelay?) : IPenSdk.Stub() {
    private val callbacks = RemoteCallbackList<IPenSdkCallback>()
    private val lock = Any()

    private var isConnected = false
    private var batteryLevel = -1
    private var isCharging = false

    private var vibrationType = 0
    private var vibrationLevel = DEFAULT_VIBRATION_LEVEL

    private var feedbackToken: IBinder? = null
    private val feedbackDeath = IBinder.DeathRecipient { onFeedbackOwnerDied() }

    override fun getVersion() = VERSION

    override fun isConnected() = synchronized(lock) { isConnected }

    override fun getBatteryLevel() = synchronized(lock) { batteryLevel }

    override fun isCharging() = synchronized(lock) { isCharging }

    override fun supportsVibration() = isConnected() && relay()?.hasInterface == true

    override fun registerCallback(callback: IPenSdkCallback?) {
        callback?.let { callbacks.register(it) }
    }

    override fun unregisterCallback(callback: IPenSdkCallback?) {
        callback?.let { callbacks.unregister(it) }
    }

    override fun setVibration(type: Int, level: Int) {
        synchronized(lock) {
            vibrationType = type.coerceIn(0, PenProtocol.VIBRATION_TYPE_MAX)
            vibrationLevel = level.coerceIn(0, PenProtocol.VIBRATION_LEVEL_MAX)
            applyVibration()
        }
    }

    override fun setWritingFeedback(token: IBinder?, active: Boolean) {
        synchronized(lock) {
            if (active) {
                if (token == null || !isConnected || vibrationLevel == 0) {
                    return
                }
                if (feedbackToken !== token) {
                    releaseFeedbackToken()
                    try {
                        token.linkToDeath(feedbackDeath, 0)
                    } catch (e: android.os.RemoteException) {
                        return
                    }
                    feedbackToken = token
                }
            } else {
                if (feedbackToken == null || (token != null && token !== feedbackToken)) {
                    return
                }
                releaseFeedbackToken()
            }
            relay()?.setWritingFeedback(active)
        }
    }

    override fun vibrateOnce() {
        if (isConnected()) {
            relay()?.sendInterfaceCommand(PenProtocol.VIBRATE_ONCE)
        }
    }

    fun onConnectionChanged(connected: Boolean) {
        synchronized(lock) {
            if (isConnected == connected) {
                return
            }
            isConnected = connected
            if (connected) {
                applyVibration()
            } else {
                releaseFeedbackToken()
                batteryLevel = -1
                isCharging = false
            }
        }
        broadcast { it.onConnectionChanged(connected) }
    }

    fun onEvent(event: PenEvent) {
        when (event) {
            PenEvent.DoubleTap -> broadcast { it.onDoubleTap() }
            is PenEvent.Battery -> {
                val charging = synchronized(lock) {
                    batteryLevel = event.level
                    isCharging
                }
                broadcast { it.onBatteryChanged(event.level, charging) }
            }
            is PenEvent.Charging -> {
                val level = synchronized(lock) {
                    isCharging = event.isCharging
                    batteryLevel
                }
                broadcast { it.onBatteryChanged(level, event.isCharging) }
            }
        }
    }

    fun dump(pw: PrintWriter, args: Array<String>) {
        when (args.getOrNull(0)) {
            "vibrate" -> vibrateOnce()
            "vibration" -> {
                val type = args.getOrNull(1)?.toIntOrNull() ?: 0
                val level = args.getOrNull(2)?.toIntOrNull() ?: DEFAULT_VIBRATION_LEVEL
                setVibration(type, level)
            }
            "feedback" -> setWritingFeedback(dumpToken, args.getOrNull(1) == "on")
            "doubletap" -> onEvent(PenEvent.DoubleTap)
        }
        synchronized(lock) {
            pw.println("PenSdk v$VERSION")
            pw.println("  connected=$isConnected interface=${relay()?.hasInterface == true}")
            pw.println("  battery=$batteryLevel charging=$isCharging")
            pw.println("  vibration type=$vibrationType level=$vibrationLevel")
            pw.println("  feedbackActive=${feedbackToken != null}")
            pw.println("  callbacks=${callbacks.registeredCallbackCount}")
        }
        pw.println("  args: vibrate | vibration <type 0-4> <level 0-3> | feedback on|off | doubletap")
    }

    private fun applyVibration() {
        if (isConnected) {
            relay()?.sendInterfaceCommand(PenProtocol.vibrationType(vibrationType, vibrationLevel))
        }
    }

    private fun onFeedbackOwnerDied() {
        synchronized(lock) {
            Log.w(TAG, "Writing feedback owner died, stopping feedback")
            feedbackToken = null
            relay()?.setWritingFeedback(false)
        }
    }

    private fun releaseFeedbackToken() {
        feedbackToken?.unlinkToDeath(feedbackDeath, 0)
        feedbackToken = null
    }

    private fun broadcast(action: (IPenSdkCallback) -> Unit) {
        synchronized(callbacks) {
            val count = callbacks.beginBroadcast()
            try {
                for (i in 0 until count) {
                    try {
                        action(callbacks.getBroadcastItem(i))
                    } catch (e: android.os.RemoteException) {
                        // RemoteCallbackList drops dead callbacks on its own
                    }
                }
            } finally {
                callbacks.finishBroadcast()
            }
        }
    }

    companion object {
        private const val TAG = "OplusPenSdk"

        const val ACTION_SDK = "org.lineageos.pen.SDK"

        private const val VERSION = 1
        private const val DEFAULT_VIBRATION_LEVEL = 3

        private val dumpToken = android.os.Binder()
    }
}
