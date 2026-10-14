/* SPDX-License-Identifier: GPL-3.0-only */
/* Statically linked instrument registry and CPU0 audio-thread manager. */
#include "instrument.h"

#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

#include "instrument_dsp.c"
#include "instrument_modulation.c"
#ifndef BARGE_ENABLE_ANALOG
#define BARGE_ENABLE_ANALOG 1
#endif
#if BARGE_ENABLE_ANALOG
#include "instrument_analog.c"
#endif
#ifndef BARGE_ENABLE_PHASE
#define BARGE_ENABLE_PHASE 1
#endif
#if BARGE_ENABLE_PHASE
#include "instrument_phase.c"
#endif
#ifndef BARGE_ENABLE_LOFI
#define BARGE_ENABLE_LOFI 1
#endif
#if BARGE_ENABLE_LOFI
#include "instrument_lofi.c"
#endif
#ifndef BARGE_ENABLE_NOISE
#define BARGE_ENABLE_NOISE 1
#endif
#if BARGE_ENABLE_NOISE
#include "instrument_noise.c"
#endif
#ifndef BARGE_ENABLE_WAVETABLE
#define BARGE_ENABLE_WAVETABLE 1
#endif
#if BARGE_ENABLE_WAVETABLE
#include "instrument_wavetable.c"
#endif

typedef union {
    uint32_t align;
    uint8_t bytes[INSTRUMENT_STATE_BYTES];
} instrument_state_t;

static const instrument_desc_t *const INSTRUMENTS[] = {
#if BARGE_ENABLE_ANALOG
    &INSTRUMENT_ANALOG,
#endif
#if BARGE_ENABLE_PHASE
    &INSTRUMENT_PHASE,
#endif
#if BARGE_ENABLE_LOFI
    &INSTRUMENT_LOFI,
#endif
#if BARGE_ENABLE_NOISE
    &INSTRUMENT_NOISE,
#endif
#if BARGE_ENABLE_WAVETABLE
    &INSTRUMENT_WAVETABLE,
#endif
};
#define INSTRUMENT_COUNT (sizeof INSTRUMENTS / sizeof INSTRUMENTS[0])

#if BARGE_ENABLE_ANALOG
#define INSTRUMENT_FALLBACK (&INSTRUMENT_ANALOG)
#define INSTRUMENT_FALLBACK_ID INSTRUMENT_ID_ANALOG
#elif BARGE_ENABLE_WAVETABLE
#define INSTRUMENT_FALLBACK (&INSTRUMENT_WAVETABLE)
#define INSTRUMENT_FALLBACK_ID INSTRUMENT_ID_WAVETABLE
#else
#error "Build at least one of BARGE_ENABLE_ANALOG or BARGE_ENABLE_WAVETABLE for the recovery instrument"
#endif

typedef char instrument_queue_is_power_of_two[
    (INSTRUMENT_EVENT_CAPACITY && !(INSTRUMENT_EVENT_CAPACITY & (INSTRUMENT_EVENT_CAPACITY - 1u))) ? 1 : -1
];
#if BARGE_ENABLE_ANALOG
typedef char instrument_analog_state_fits[
    (sizeof(instrument_analog_state_t) <= INSTRUMENT_STATE_BYTES) ? 1 : -1
];
#endif
#if BARGE_ENABLE_PHASE
typedef char instrument_phase_state_fits[
    (sizeof(instrument_phase_state_t) <= INSTRUMENT_STATE_BYTES) ? 1 : -1
];
#endif
#if BARGE_ENABLE_LOFI
typedef char instrument_lofi_state_fits[
    (sizeof(instrument_lofi_state_t) <= INSTRUMENT_STATE_BYTES) ? 1 : -1
];
#endif
#if BARGE_ENABLE_NOISE
typedef char instrument_noise_state_fits[
    (sizeof(instrument_noise_state_t) <= INSTRUMENT_STATE_BYTES) ? 1 : -1
];
#endif
#if BARGE_ENABLE_WAVETABLE
typedef char instrument_wavetable_state_fits[
    (sizeof(instrument_wavetable_state_t) <= INSTRUMENT_STATE_BYTES) ? 1 : -1
];
#endif

static instrument_state_t instrument_state;
static instrument_event_t event_queue[INSTRUMENT_EVENT_CAPACITY];
#if BARGE_ENABLE_WAVETABLE
static uint8_t instrument_wave_current[INSTRUMENT_WAVETABLE_POINTS];
static uint8_t instrument_wave_staging[INSTRUMENT_WAVETABLE_POINTS];
static volatile uint8_t instrument_wave_pending, instrument_wave_dirty;
#endif
static volatile uint8_t event_write, event_read, event_panic;
static volatile uint16_t pending_id;
static volatile uint8_t pending_select;
static volatile uint8_t pending_event_start;
static uint8_t pending_release_sent;
static const instrument_desc_t *active_instrument = INSTRUMENT_FALLBACK;
static volatile uint16_t active_id = INSTRUMENT_FALLBACK_ID;
static volatile uint8_t ready, audio_quiet = 1;

static const instrument_desc_t *instrument_find(uint16_t id)
{
    uint32_t i;
    for (i = 0; i < INSTRUMENT_COUNT; i++)
        if (INSTRUMENTS[i]->id == id)
            return INSTRUMENTS[i];
    return 0;
}

void instrument_init(uint16_t preferred_id)
{
    const instrument_desc_t *selected = instrument_find(preferred_id);
    uint32_t i;
    if (!selected)
        selected = INSTRUMENT_FALLBACK;
    for (i = 0; i < INSTRUMENT_COUNT; i++) {
        uint32_t j;
        if (INSTRUMENTS[i]->id == 0 || INSTRUMENTS[i]->abi_version != INSTRUMENT_ABI_VERSION
            || INSTRUMENTS[i]->state_bytes > INSTRUMENT_STATE_BYTES || !INSTRUMENTS[i]->state_align
            || INSTRUMENTS[i]->state_align > sizeof(uint32_t)
            || (INSTRUMENTS[i]->state_align & (INSTRUMENTS[i]->state_align - 1u))
            || !INSTRUMENTS[i]->name || !INSTRUMENTS[i]->reset || !INSTRUMENTS[i]->event
            || !INSTRUMENTS[i]->render || !INSTRUMENTS[i]->quiet)
            selected = INSTRUMENT_FALLBACK;
        for (j = i + 1u; j < INSTRUMENT_COUNT; j++)
            if (INSTRUMENTS[i]->id == INSTRUMENTS[j]->id)
                selected = INSTRUMENT_FALLBACK;
    }
    event_write = event_read = event_panic = pending_select = pending_release_sent = 0;
#if BARGE_ENABLE_WAVETABLE
    memcpy(instrument_wave_current, INSTRUMENT_WAVE_DEFAULT, sizeof instrument_wave_current);
    memcpy(instrument_wave_staging, INSTRUMENT_WAVE_DEFAULT, sizeof instrument_wave_staging);
    instrument_wave_pending = instrument_wave_dirty = 0;
#endif
    instrument_adsr[0] = 4;
    instrument_adsr[1] = 32;
    instrument_adsr[2] = 96;
    instrument_adsr[3] = 32;
    instrument_adsr_prepare();
    active_instrument = selected;
    active_id = selected->id;
    memset(&instrument_state, 0, sizeof instrument_state);
    active_instrument->reset(instrument_state.bytes);
    audio_quiet = 1;
    RING_PUBLISH();
    ready = 1;
}

int instrument_select(uint16_t id)
{
    if (!instrument_find(id))
        return 0;
    if (id == active_id && !pending_select)
        return 1;
    pending_id = id;
    pending_event_start = event_write;
    RING_PUBLISH();
    pending_select = 1;
    return 1;
}

int instrument_send(const instrument_event_t *event)
{
    uint8_t write, read;
    if (!event || !ready)
        return 0;
    if (event_panic)
        return 0;
    write = event_write;
    read = event_read;
    if ((uint8_t)(write - read) >= INSTRUMENT_EVENT_CAPACITY) {
        event_panic = 1;
        return 0;
    }
    event_queue[write & (INSTRUMENT_EVENT_CAPACITY - 1u)] = *event;
    RING_PUBLISH();
    event_write = (uint8_t)(write + 1u);
    return 1;
}

void instrument_render(int32_t *out, uint32_t frames)
{
    uint8_t read, write;
    instrument_event_t event;
    if (!ready || frames > INSTRUMENT_MAX_FRAMES)
        return;

    if (pending_select) {
        const instrument_desc_t *next = instrument_find(pending_id);
        if (!pending_release_sent) {
            instrument_event_t panic = {INSTRUMENT_EVENT_ALL_NOTES_OFF, 0, 0, 0};
            active_instrument->event(instrument_state.bytes, &panic);
            RING_PUBLISH();
            event_read = pending_event_start; /* discard old-module events, keep commands queued after select */
            event_panic = 0;
            pending_release_sent = 1;
        }
        if (!active_instrument->quiet(instrument_state.bytes)) {
            active_instrument->render(instrument_state.bytes, out, frames);
            audio_quiet = (uint8_t)active_instrument->quiet(instrument_state.bytes);
            return;
        }
        pending_select = 0;
        pending_release_sent = 0;
        if (next && next != active_instrument) {
            active_instrument = next;
            memset(&instrument_state, 0, sizeof instrument_state);
            active_instrument->reset(instrument_state.bytes);
#if BARGE_ENABLE_WAVETABLE
            if (next->id == INSTRUMENT_ID_WAVETABLE)
                instrument_wavetable_set(instrument_state.bytes, instrument_wave_current);
#endif
            active_id = next->id;
        }
    }

#if BARGE_ENABLE_WAVETABLE
    if (instrument_wave_pending) {
        memcpy(instrument_wave_current, instrument_wave_staging, sizeof instrument_wave_current);
        RING_PUBLISH();
        instrument_wave_pending = 0;
        instrument_wave_dirty = 1;
    }
    if (instrument_wave_dirty && active_instrument->id == INSTRUMENT_ID_WAVETABLE) {
        instrument_wavetable_set(instrument_state.bytes, instrument_wave_current);
        instrument_wave_dirty = 0;
    }
#endif

    if (event_panic) {
        instrument_event_t panic = {INSTRUMENT_EVENT_ALL_NOTES_OFF, 0, 0, 0};
        active_instrument->event(instrument_state.bytes, &panic);
        event_read = event_write;
        event_panic = 0;
    } else {
        read = event_read;
        write = event_write;
        while (read != write) {
            event = event_queue[read & (INSTRUMENT_EVENT_CAPACITY - 1u)];
            if (event.type == INSTRUMENT_EVENT_ADSR)
                instrument_adsr_set(event.note, event.velocity);
            else
                active_instrument->event(instrument_state.bytes, &event);
            read = (uint8_t)(read + 1u);
        }
        RING_PUBLISH();
        event_read = read;
    }

    instrument_adsr_prepare();
    active_instrument->render(instrument_state.bytes, out, frames);
    audio_quiet = (uint8_t)active_instrument->quiet(instrument_state.bytes);
}

int instrument_wave_upload(const uint8_t *samples, uint32_t count)
{
#if BARGE_ENABLE_WAVETABLE
    uint32_t i;
    if (!samples || count != INSTRUMENT_WAVETABLE_POINTS || instrument_wave_pending)
        return 0;
    for (i = 0; i < count; i++)
        if (samples[i] > 127u)
            return 0;
    memcpy(instrument_wave_staging, samples, count);
    RING_PUBLISH();
    instrument_wave_pending = 1;
    return 1;
#else
    (void)samples;
    (void)count;
    return 0;
#endif
}

int instrument_wavetable_compiled(void)
{
#if BARGE_ENABLE_WAVETABLE
    return 1;
#else
    return 0;
#endif
}

int instrument_is_quiet(void) { return audio_quiet != 0; }
uint16_t instrument_active_id(void) { return active_id; }

uint16_t instrument_next_id(uint16_t id)
{
    uint32_t i;
    for (i = 0; i < INSTRUMENT_COUNT; i++)
        if (INSTRUMENTS[i]->id == id)
            return INSTRUMENTS[(i + 1u) % INSTRUMENT_COUNT]->id;
    return INSTRUMENTS[0]->id;
}

const char *instrument_name(uint16_t id)
{
    const instrument_desc_t *instrument = instrument_find(id);
    return instrument ? instrument->name : INSTRUMENT_FALLBACK->name;
}

uint16_t instrument_fallback_id(void) { return INSTRUMENT_FALLBACK->id; }
int instrument_has_id(uint16_t id) { return instrument_find(id) != 0; }