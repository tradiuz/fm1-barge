/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Barge low-bit oscillator/noise adapter, informed by Felucca's LOFI engine.
 * Deliberately omits Felucca framework code, wave-RAM sets, arp and stepped-envelope mode. */
#define LOFI_VOICES 4u
typedef struct {
    uint32_t phase, increment, rng, age;
    int32_t gate, amp, held, filter;
    uint16_t hold_count;
    uint16_t env_level;
    uint8_t env_stage;
    uint8_t note;
} instrument_lofi_voice_t;
typedef struct {
    instrument_lofi_voice_t voice[LOFI_VOICES];
    uint32_t ages;
    uint8_t wave, duty, crush, tone;
} instrument_lofi_state_t;

static void instrument_lofi_reset(void *opaque)
{
    instrument_lofi_state_t *state = (instrument_lofi_state_t *)opaque;
    memset(state, 0, sizeof *state);
    state->wave = 0;
    state->duty = 64;
    state->crush = 16;
    state->tone = 110;
}

static void instrument_lofi_event(void *opaque, const instrument_event_t *event)
{
    instrument_lofi_state_t *state = (instrument_lofi_state_t *)opaque;
    uint32_t i, pick = 0, best = 0xFFFFFFFFu;
    if (event->type == INSTRUMENT_EVENT_CONTROL) {
        switch (event->note) {
        case 0: state->wave = event->velocity & 3u; break;
        case 1: state->duty = event->velocity; break;
        case 2: state->crush = event->velocity; break;
        case 3: state->tone = event->velocity; break;
        default: break;
        }
        return;
    }
    if (event->type == INSTRUMENT_EVENT_ALL_NOTES_OFF) {
        for (i = 0; i < LOFI_VOICES; i++) instrument_adsr_note_off(&state->voice[i].env_stage);
        return;
    }
    if (event->type == INSTRUMENT_EVENT_NOTE_OFF) {
        for (i = 0; i < LOFI_VOICES; i++)
            if (state->voice[i].note == event->note) instrument_adsr_note_off(&state->voice[i].env_stage);
        return;
    }
    if (event->type != INSTRUMENT_EVENT_NOTE_ON || !event->velocity || event->note > 127u)
        return;
    for (i = 0; i < LOFI_VOICES; i++) {
        instrument_lofi_voice_t *voice = &state->voice[i];
        int32_t scoring_gate = voice->env_stage == INSTRUMENT_ENV_RELEASE ? 0 : voice->gate;
        uint32_t score = instrument_voice_score(voice->note, scoring_gate, voice->age,
                            voice->amp, event->note);
        if (score < best) { best = score; pick = i; }
    }
    state->voice[pick].note = event->note;
    state->voice[pick].age = ++state->ages;
    state->voice[pick].increment = instrument_pitch_inc(event->note);
    state->voice[pick].rng = (0x9E3779B9u * ((uint32_t)event->note + 1u)) ^ (state->ages * 0x85EBCA6Bu);
    if (!state->voice[pick].rng) state->voice[pick].rng = 1u;
    state->voice[pick].held = 0;
    state->voice[pick].filter = 0;
    state->voice[pick].hold_count = 0;
    instrument_adsr_note_on(&state->voice[pick].gate, &state->voice[pick].amp,
                            &state->voice[pick].env_level, &state->voice[pick].env_stage,
                            event->velocity, 58u);
}

static int32_t instrument_lofi_wave(uint8_t wave, uint32_t phase, uint32_t increment,
                                    uint8_t duty, uint32_t *rng)
{
    switch (wave & 3u) {
    case 1: {
        uint32_t x = phase >> 15;
        return x < 65536u ? (int32_t)x - 32768 : 98303 - (int32_t)x;
    }
    case 2: return (int32_t)(phase >> 16) - 32768;
    case 3: return (instrument_xorshift32(rng) & 1u) ? 32767 : -32768;
    default: {
        uint32_t duty_u = duty;
        uint32_t span = (0x60000000u / 127u) * duty_u
                      + ((0x60000000u % 127u) * duty_u) / 127u;
        uint32_t width = 0x20000000u + span;
        int32_t mean = (int32_t)(width >> 16) - 32768;
        (void)increment;
        return phase < width ? 32767 - mean : -32768 - mean;
    }
    }
}

static void instrument_lofi_render(void *opaque, int32_t *out, uint32_t frames)
{
    instrument_lofi_state_t *state = (instrument_lofi_state_t *)opaque;
    uint32_t v, i;
    int32_t cutoff_hz = 120 + (int32_t)state->tone * 12000 / 127;
    int32_t alpha = cutoff_hz * 467 / 100;
    uint32_t hold = 1u + state->crush / 24u;
    uint32_t bits = 8u - (state->crush * 6u / 127u);
    int32_t quantum = 1 << (15u - bits);
    if (alpha > 32767) alpha = 32767;
    for (v = 0; v < LOFI_VOICES; v++) {
        instrument_lofi_voice_t *voice = &state->voice[v];
        uint32_t phase = voice->phase, rng = voice->rng;
        int32_t amp = voice->amp, gate = voice->gate, held = voice->held, filter = voice->filter;
        uint32_t count = voice->hold_count;
        if (!amp && !gate) continue;
        for (i = 0; i < frames; i++) {
            int32_t sample;
            amp = instrument_adsr_tick(&voice->gate, &voice->env_level, &voice->env_stage);
            if (!count) {
                sample = instrument_lofi_wave(state->wave, phase, voice->increment, state->duty, &rng);
                held = (sample / quantum) * quantum;
                count = hold;
            }
            count--;
            phase += voice->increment;
            filter += instrument_mul_q15_split(held - filter, alpha);
            filter = instrument_clamp_i32(filter, -100000, 100000);
            sample = (filter * amp) >> 15;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
        }
        voice->phase = phase;
        voice->rng = rng;
        voice->amp = amp;
        voice->held = held;
        voice->filter = filter;
        voice->hold_count = (uint16_t)count;
    }
}

static int instrument_lofi_quiet(const void *opaque)
{
    const instrument_lofi_state_t *state = (const instrument_lofi_state_t *)opaque;
    uint32_t i;
    for (i = 0; i < LOFI_VOICES; i++) if (state->voice[i].gate || state->voice[i].amp) return 0;
    return 1;
}

static const instrument_desc_t INSTRUMENT_LOFI = {
    INSTRUMENT_ID_LOFI, INSTRUMENT_ABI_VERSION, "LOFI", sizeof(instrument_lofi_state_t), 4u,
    instrument_lofi_reset, instrument_lofi_event, instrument_lofi_render, instrument_lofi_quiet
};
