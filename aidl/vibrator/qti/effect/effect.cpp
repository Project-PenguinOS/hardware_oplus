/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "effect.h"

#include <algorithm>
#include <iterator>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

#include <VibrationEffectConfig.h>

#include "VibrationEffectLoader.h"

#ifdef HAPTIC_PROFILE_SUPPORT
#include <android-base/properties.h>
#include <string>
#endif

namespace {
VibrationEffectLoader loader;
};  // anonymous namespace

const struct effect_stream* get_effect_stream(uint32_t effect_id) {
    auto ret = loader.getEffectStream(effect_id);
    if (ret) return ret;

#ifdef HAPTIC_PROFILE_SUPPORT
    using android::base::GetProperty;

    std::string profile = GetProperty("persist.sys.haptic_profile", "op13gentle");

    if ((effect_id & 0x8000) != 0) {
        effect_id = effect_id & 0x7fff;
        const struct effect_stream* selected = primitives;
        size_t size = ARRAY_SIZE(primitives);

        if (profile == "crisp" || profile == "op13crisp") {
            selected = primitives_crisp;
            size = ARRAY_SIZE(primitives_crisp);
        } else if (profile == "gentle" || profile == "op13gentle" || profile == "op13soft") {
            selected = primitives_gentle;
            size = ARRAY_SIZE(primitives_gentle);
        }

        for (size_t i = 0; i < size; i++)
            if (effect_id == selected[i].effect_id) return &selected[i];
        return nullptr;
    }

    const struct effect_stream* selected = effects;
    size_t size = ARRAY_SIZE(effects);

    if (profile == "crisp") {
        selected = effects_crisp;
        size = ARRAY_SIZE(effects_crisp);
    } else if (profile == "gentle") {
        selected = effects_gentle;
        size = ARRAY_SIZE(effects_gentle);
    } else if (profile == "op13crisp" || profile == "op13def") {
        selected = effects_op13def;
        size = ARRAY_SIZE(effects_op13def);
    } else if (profile == "op13gentle" || profile == "op13soft") {
        selected = effects_op13soft;
        size = ARRAY_SIZE(effects_op13soft);
    }

    for (size_t i = 0; i < size; i++)
        if (effect_id == selected[i].effect_id) return &selected[i];
    return nullptr;
#else
    auto it = std::find_if(std::begin(effects), std::end(effects),
                           [&](auto&& v) { return v.effect_id == effect_id; });
    return it != std::end(effects) ? &*it : nullptr;
#endif
}
