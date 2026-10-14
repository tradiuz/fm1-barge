/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BARGE_INSTRUMENT_H
#define BARGE_INSTRUMENT_H

#include <stdint.h>

enum {
    INSTRUMENT_RATE = 44100u,
    INSTRUMENT_MAX_FRAMES = 256u,
    INSTRUMENT_EVENT_CAPACITY = 32u,
    INSTRUMENT_STATE_BYTES = 1024u,
    INSTRUMENT_ABI_VERSION = 1u,
    INSTRUMENT_MOD_CTL_RATE = 11u,
    INSTRUMENT_MOD_CTL_WAVE = 12u,
    INSTRUMENT_MOD_CTL_DEPTH = 13u,
    INSTRUMENT_MOD_CTL_ROUTE = 14u,
    INSTRUMENT_MOD_CTL_MODE = 15u,
    INSTRUMENT_MOD_CTL_SOURCE = 16u,
    INSTRUMENT_MOD_CTL_COUNT = 17u
};

enum {
    INSTRUMENT_ID_RETIRED_SINE = 0x0001u, /* kept reserved so saved IDs are never reused */
    INSTRUMENT_ID_ANALOG = 0x0100u,
    INSTRUMENT_ID_PHASE = 0x0200u,
    INSTRUMENT_ID_LOFI = 0x0300u,
    INSTRUMENT_ID_NOISE = 0x0400u,
    INSTRUMENT_ID_WAVETABLE = 0x0500u,
    INSTRUMENT_WAVETABLE_POINTS = 32u
};

typedef enum {
    INSTRUMENT_EVENT_NOTE_ON,
    INSTRUMENT_EVENT_NOTE_OFF,
    INSTRUMENT_EVENT_ALL_NOTES_OFF,
    INSTRUMENT_EVENT_CONTROL,
    INSTRUMENT_EVENT_ADSR
} instrument_event_type_t;

typedef struct {
    uint8_t type;
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
} instrument_event_t;

/* Events are delivered in producer order at a block boundary. CONTROL uses note as
 * the module-local parameter index and velocity as its 0..127 value. */
typedef struct {
    uint16_t id;
    uint16_t abi_version;
    const char *name;
    uint16_t state_bytes;
    uint8_t state_align;
    void (*reset)(void *state);
    void (*event)(void *state, const instrument_event_t *event);
    /* Add interleaved signed Q15-scale samples to the host-zeroed stereo int32 buffer. */
    void (*render)(void *state, int32_t *stereo_q15, uint32_t frames);
    int (*quiet)(const void *state);
} instrument_desc_t;

void instrument_init(uint16_t preferred_id);
int instrument_select(uint16_t id);
int instrument_send(const instrument_event_t *event);
void instrument_render(int32_t *stereo_q15, uint32_t frames);
int instrument_is_quiet(void);
int instrument_wave_upload(const uint8_t *samples, uint32_t count);
uint16_t instrument_active_id(void);
uint16_t instrument_next_id(uint16_t id);
const char *instrument_name(uint16_t id);
uint16_t instrument_fallback_id(void);
int instrument_has_id(uint16_t id);

#endif