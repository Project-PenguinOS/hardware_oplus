/*
 * Copyright (C) 2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} AlsRect;

typedef struct {
    // Resolution of the largest display mode.
    int32_t panel_width;
    int32_t panel_height;
    int32_t layer_stack_width;
    int32_t layer_stack_height;
    // ui::Rotation of the layer stack relative to the panel, 0-3.
    int32_t rotation;
} AlsDisplayGeometry;

// Called with the locked RGBA_8888 capture; stride is in pixels.
typedef void (*AlsPixelsCallback)(void* context, const uint8_t* pixels, uint32_t width,
                                  uint32_t height, uint32_t stride);

bool als_get_display_geometry(AlsDisplayGeometry* out);
bool als_capture(const AlsRect* crop, int32_t width, int32_t height, AlsPixelsCallback callback,
                 void* context);

#ifdef __cplusplus
}
#endif
