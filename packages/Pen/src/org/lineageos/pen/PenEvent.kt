/*
 * Copyright (C) 2026 The PenguinOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.pen

sealed interface PenEvent {
    data object DoubleTap : PenEvent

    data class Battery(val level: Int) : PenEvent

    data class Charging(val isCharging: Boolean) : PenEvent
}
