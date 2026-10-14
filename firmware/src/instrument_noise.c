/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Barge white-noise/dust adapter informed by Felucca's NOISE source modes.
 * This reduced implementation uses deterministic per-voice xorshift and cascaded LP stages. */
#define NOISE_VOICES 4u
typedef struct {
    uint32_t rng, age;
    int32_t gate, amp, filter0, filter1;
    uint16_t env_level;
    uint8_t env_stage;
    uint8_t note;
} instrument_noise_voice_t;
typedef struct {
    instrument_noise_voice_t voice[NOISE_VOICES];
    uint32_t ages;
    uint8_t mode, tone, density, level;
} instrument_noise_state_t;

static void instrument_noise_reset(void *opaque)
{
    instrument_noise_state_t *state = (instrument_noise_state_t *)opaque;
    memset(state, 0, sizeof *state);
    state->mode = 0;
    state->tone = 96;
    state->density = 24;
    state->level = 96;
}

static void instrument_noise_event(void *opaque, const instrument_event_t *event)
{
    instrument_noise_state_t *state = (instrument_noise_state_t *)opaque;
    uint32_t i, pick = 0, best = 0xFFFFFFFFu;
    if (event->type == INSTRUMENT_EVENT_CONTROL) {
        switch (event->note) {
        case 0: state->mode = event->velocity & 1u; break;
        case 1: state->tone = event->velocity; break;
        case 2: state->density = event->velocity; break;
        case 3: state->level = event->velocity; break;
        default: break;
        }
        return;
    }
    if (event->type == INSTRUMENT_EVENT_ALL_NOTES_OFF) {
        for (i = 0; i < NOISE_VOICES; i++) instrument_adsr_note_off(&state->voice[i].env_stage);
        return;
    }
    if (event->type == INSTRUMENT_EVENT_NOTE_OFF) {
        for (i = 0; i < NOISE_VOICES; i++)
            if (state->voice[i].note == event->note) instrument_adsr_note_off(&state->voice[i].env_stage);
        return;
    }
    if (event->type != INSTRUMENT_EVENT_NOTE_ON || !event->velocity || event->note > 127u)
        return;
    for (i = 0; i < NOISE_VOICES; i++) {
        instrument_noise_voice_t *voice = &state->voice[i];
        int32_t scoring_gate = voice->env_stage == INSTRUMENT_ENV_RELEASE ? 0 : voice->gate;
        uint32_t score = instrument_voice_score(voice->note, scoring_gate, voice->age,
                            voice->amp, event->note);
        if (score < best) { best = score; pick = i; }
    }
    state->voice[pick].note = event->note;
    state->voice[pick].age = ++state->ages;
    state->voice[pick].rng = (0x9E3779B9u * ((uint32_t)event->note + 1u)) ^ (state->ages * 0x85EBCA6Bu);
    if (!state->voice[pick].rng) state->voice[pick].rng = 1u;
    state->voice[pick].filter0 = state->voice[pick].filter1 = 0;
    instrument_adsr_note_on(&state->voice[pick].gate, &state->voice[pick].amp,
                            &state->voice[pick].env_level, &state->voice[pick].env_stage,
                            event->velocity, 48u);
}

static void instrument_noise_render(void *opaque, int32_t *out, uint32_t frames)
{
    instrument_noise_state_t *state = (instrument_noise_state_t *)opaque;
    uint32_t v, i;
    int32_t cutoff_hz = 80 + (int32_t)state->tone * 12000 / 127;
    int32_t alpha = cutoff_hz * 467 / 100;
    uint32_t impulse_threshold = (uint32_t)state->density * 6u;
    int32_t gain = (int32_t)state->level * 258;
    if (alpha > 32767) alpha = 32767;
    for (v = 0; v < NOISE_VOICES; v++) {
        instrument_noise_voice_t *voice = &state->voice[v];
        uint32_t rng = voice->rng;
        int32_t amp = voice->amp, gate = voice->gate;
        int32_t filter0 = voice->filter0, filter1 = voice->filter1;
        if (!amp && !gate) continue;
        for (i = 0; i < frames; i++) {
            uint32_t random;
            int32_t sample, level;
            amp = instrument_adsr_tick(&voice->gate, &voice->env_level, &voice->env_stage);
            random = instrument_xorshift32(&rng);
            if (state->mode == 0u) {
                sample = (int32_t)(random >> 17) - 16384;
                if ((random & 0xFFFFu) < impulse_threshold)
                    sample += (random & (1u << 16)) ? 32767 : -32768;
            } else {
                sample = (random & 0xFFFFu) < impulse_threshold
                       ? ((random & (1u << 16)) ? 32767 : -32768) : 0;
            }
            filter0 += instrument_mul_q15_split(sample - filter0, alpha);
            filter1 += instrument_mul_q15_split(filter0 - filter1, alpha);
            filter0 = instrument_clamp_i32(filter0, -100000, 100000);
            filter1 = instrument_clamp_i32(filter1, -100000, 100000);
            level = instrument_mul_q15_split(filter1, gain);
            sample = (level * amp) >> 15;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
        }
        voice->rng = rng;
        voice->amp = amp;
        voice->filter0 = filter0;
        voice->filter1 = filter1;
    }
}

static int instrument_noise_quiet(const void *opaque)
{
    const instrument_noise_state_t *state = (const instrument_noise_state_t *)opaque;
    uint32_t i;
    for (i = 0; i < NOISE_VOICES; i++) if (state->voice[i].gate || state->voice[i].amp) return 0;
    return 1;
}

static const instrument_desc_t INSTRUMENT_NOISE = {
    INSTRUMENT_ID_NOISE, INSTRUMENT_ABI_VERSION, "NOISE", sizeof(instrument_noise_state_t), 4u,
    instrument_noise_reset, instrument_noise_event, instrument_noise_render, instrument_noise_quiet
};
