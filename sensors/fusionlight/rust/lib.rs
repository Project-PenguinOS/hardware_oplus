/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

//! FusionLight: lux from the high-PWM RGB light sensor with the screen's light leakage removed.

// The items are the internal interface between the sub-HAL and its host test tools.
#![allow(missing_docs)]

pub mod config;
pub mod json;
pub mod lux;
