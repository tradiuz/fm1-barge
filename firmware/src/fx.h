/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BARGE_FX_H
#define BARGE_FX_H
#include <stdint.h>

enum { FX_ABI_VERSION = 1u, FX_MAX_FRAMES = 256u, FX_ID_CLIPPER = 0x1000u };

typedef void (*fx_process_fn)(void *state, int32_t *stereo, uint32_t frames, uint16_t parameter);
typedef struct {
    uint16_t id;
    uint16_t abi_version;
    const char *name;
    uint16_t state_bytes;
    uint8_t state_align;
    fx_process_fn process;
} fx_desc_t;

void fx_init(int enabled, uint16_t threshold);
void fx_set_enabled(int enabled);
void fx_set_threshold(uint16_t threshold);
int fx_requested_enabled(void);
uint16_t fx_requested_threshold(void);
int fx_compiled(void);
void fx_process(int32_t *stereo, uint32_t frames);

#endif
