/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed-order statically linked FX manager. Parameters are published as one block snapshot. */
#include "fx.h"
#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif
#ifndef BARGE_ENABLE_FX_CLIPPER
#define BARGE_ENABLE_FX_CLIPPER 1
#endif
#if BARGE_ENABLE_FX_CLIPPER
#include "fx_clipper.c"
#endif

#define FX_ENABLED_BIT (0x1000u << 4)
#define FX_THRESHOLD_MASK 0x00007FFFu
static volatile uint32_t fx_requested_config;
#if BARGE_ENABLE_FX_CLIPPER
static const fx_desc_t *const FX_CHAIN[] = {&FX_CLIPPER};
#endif

void fx_init(int enabled, uint16_t threshold)
{
    if (threshold > 32767u) threshold = 32767u;
#if !BARGE_ENABLE_FX_CLIPPER
    enabled = 0;
#endif
    fx_requested_config = (enabled ? FX_ENABLED_BIT : 0u) | threshold;
    RING_PUBLISH();
}

void fx_set_enabled(int enabled)
{
    uint32_t config = fx_requested_config & ~FX_ENABLED_BIT;
#if BARGE_ENABLE_FX_CLIPPER
    if (enabled) config |= FX_ENABLED_BIT;
#else
    (void)enabled;
#endif
    fx_requested_config = config;
    RING_PUBLISH();
}

void fx_set_threshold(uint16_t threshold)
{
    uint32_t config = fx_requested_config & FX_ENABLED_BIT;
    if (threshold > 32767u) threshold = 32767u;
    config |= threshold;
    fx_requested_config = config;
    RING_PUBLISH();
}

int fx_requested_enabled(void) { return (fx_requested_config & FX_ENABLED_BIT) != 0; }
uint16_t fx_requested_threshold(void) { return (uint16_t)(fx_requested_config & FX_THRESHOLD_MASK); }
int fx_compiled(void) { return BARGE_ENABLE_FX_CLIPPER != 0; }

void fx_process(int32_t *stereo, uint32_t frames)
{
    uint32_t config = fx_requested_config;
    uint32_t i;
    if (!stereo || frames > FX_MAX_FRAMES || !(config & FX_ENABLED_BIT))
        return;
#if BARGE_ENABLE_FX_CLIPPER
    for (i = 0; i < sizeof FX_CHAIN / sizeof FX_CHAIN[0]; i++) {
        const fx_desc_t *effect = FX_CHAIN[i];
        if (effect->abi_version == FX_ABI_VERSION && effect->id == FX_ID_CLIPPER && effect->process)
            effect->process(0, stereo, frames, (uint16_t)(config & FX_THRESHOLD_MASK));
    }
#else
    (void)i;
#endif
}
