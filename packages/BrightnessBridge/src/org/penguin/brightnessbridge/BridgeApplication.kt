/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.penguin.brightnessbridge

import android.app.Application
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.res.Configuration
import android.database.ContentObserver
import android.hardware.camera2.CameraManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemProperties
import android.os.UserHandle
import android.provider.Settings
import android.util.Log
import java.util.TimeZone

class BridgeApplication : Application() {

    private lateinit var handler: Handler
    private val wifiNetworks = mutableSetOf<Network>()
    private val torches = mutableMapOf<String, Boolean>()

    override fun onCreate() {
        super.onCreate()
        val thread = HandlerThread(TAG).apply { start() }
        handler = Handler(thread.looper)
        handler.post { start() }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        handler.post { publishDarkMode(newConfig) }
    }

    private fun start() {
        publishDarkMode(resources.configuration)
        publishAutoMode()
        publishUtcOffset()

        contentResolver.registerContentObserver(
            Settings.System.getUriFor(Settings.System.SCREEN_BRIGHTNESS_MODE),
            false,
            object : ContentObserver(handler) {
                override fun onChange(selfChange: Boolean) = publishAutoMode()
            },
            UserHandle.USER_ALL,
        )

        registerReceiver(
            object : BroadcastReceiver() {
                override fun onReceive(context: Context, intent: Intent) {
                    if (intent.action == Intent.ACTION_USER_SWITCHED) {
                        publishAutoMode()
                    } else {
                        publishUtcOffset()
                    }
                }
            },
            IntentFilter().apply {
                addAction(Intent.ACTION_TIMEZONE_CHANGED)
                addAction(Intent.ACTION_TIME_CHANGED)
                addAction(Intent.ACTION_TIME_TICK)
                addAction(Intent.ACTION_USER_SWITCHED)
            },
            null,
            handler,
        )

        val connectivity = getSystemService(ConnectivityManager::class.java)
        set(PROP_WIFI, false)
        connectivity.registerNetworkCallback(
            NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .build(),
            object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: Network) {
                    wifiNetworks.add(network)
                    set(PROP_WIFI, true)
                }

                override fun onLost(network: Network) {
                    wifiNetworks.remove(network)
                    set(PROP_WIFI, wifiNetworks.isNotEmpty())
                }
            },
            handler,
        )

        set(PROP_TORCH, false)
        getSystemService(CameraManager::class.java).registerTorchCallback(
            object : CameraManager.TorchCallback() {
                override fun onTorchModeChanged(cameraId: String, enabled: Boolean) {
                    torches[cameraId] = enabled
                    set(PROP_TORCH, torches.values.any { it })
                }

                override fun onTorchModeUnavailable(cameraId: String) {
                    torches[cameraId] = false
                    set(PROP_TORCH, torches.values.any { it })
                }
            },
            handler,
        )
        Log.i(TAG, "Started")
    }

    private fun publishDarkMode(config: Configuration) {
        set(
            PROP_DARK_MODE,
            config.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES,
        )
    }

    private fun publishAutoMode() {
        val mode = Settings.System.getIntForUser(
            contentResolver,
            Settings.System.SCREEN_BRIGHTNESS_MODE,
            Settings.System.SCREEN_BRIGHTNESS_MODE_MANUAL,
            UserHandle.USER_CURRENT,
        )
        set(PROP_AUTO, mode == Settings.System.SCREEN_BRIGHTNESS_MODE_AUTOMATIC)
    }

    private fun publishUtcOffset() {
        val offset = TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 1000
        setValue(PROP_UTC_OFFSET, offset.toString())
    }

    private fun set(name: String, value: Boolean) = setValue(name, if (value) "1" else "0")

    private fun setValue(name: String, value: String) {
        if (SystemProperties.get(name) == value) return
        try {
            SystemProperties.set(name, value)
        } catch (e: RuntimeException) {
            Log.w(TAG, "Unable to set $name", e)
        }
    }

    companion object {
        private const val TAG = "BrightnessBridge"
        private const val PREFIX = "sys.penguin.brightness."
        private const val PROP_WIFI = PREFIX + "wifi"
        private const val PROP_DARK_MODE = PREFIX + "dark_mode"
        private const val PROP_TORCH = PREFIX + "torch"
        private const val PROP_AUTO = PREFIX + "auto"
        private const val PROP_UTC_OFFSET = PREFIX + "utc_offset"
    }
}
