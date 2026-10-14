/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Adapted from Felucca's ANALOG engine concepts and fixed-point oscillator/filter DSP.
 * This first Barge adapter intentionally implements a smaller parameter set and its own
 * voice/envelope state; it does not include Felucca's track, voice, modulation or UI framework.
 */

#define ANALOG_VOICES 4u
#define ANALOG_CTL_WAVE 0u
#define ANALOG_CTL_DETUNE 1u
#define ANALOG_CTL_MIX 2u
#define ANALOG_CTL_NOISE 3u
#define ANALOG_CTL_CUTOFF 4u
#define ANALOG_CTL_RESONANCE 5u
#define ANALOG_CTL_DRIVE 6u
#define ANALOG_CTL_KEYTRACK 7u
#define ANALOG_CTL_FILTER_TYPE 8u
#define ANALOG_CTL_SHAPE 9u
#define ANALOG_CTL_ENV_FILTER 10u
enum { ANALOG_MOD_CUTOFF, ANALOG_MOD_SHAPE, ANALOG_MOD_PWM };

/* Compact 32-point versions of the same four waves exposed by WAVE. Unlike the
 * user-uploaded WAVE table, these are immutable ROM data selected by WAVE. */
static const uint8_t ANALOG_WAVE_TABLE[3][32] = {
    {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
     64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124},
    {127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120,
     127, 120, 112, 104, 96, 88, 80, 72, 64, 56, 48, 40, 32, 24, 16, 8}
};

typedef struct {
    instrument_osc_voice_t voice[ANALOG_VOICES];
    instrument_modulation_t modulation;
    uint32_t ages;
    uint8_t wave;
    uint8_t detune;
    uint8_t mix;
    uint8_t noise;
    uint8_t cutoff;
    uint8_t resonance;
    uint8_t drive;
    uint8_t keytrack;
    uint8_t filter_type;
    uint8_t shape;
    uint8_t env_filter;
} instrument_analog_state_t;

static int32_t analog_wave_sample(uint8_t wave, uint32_t phase, uint8_t shape)
{
    if (wave < 4u)
        return instrument_wave32_q15(wave == 3u ? INSTRUMENT_WAVE_DEFAULT : ANALOG_WAVE_TABLE[wave], phase);
    if (wave == 4u) {
        uint32_t span = (0x60000000u / 127u) * shape
                      + ((0x60000000u % 127u) * shape) / 127u;
        uint32_t width = 0x20000000u + span;
        int32_t mean = (int32_t)(width >> 16) - 32768;
        return phase < width ? 32767 - mean : -32768 - mean;
    }
    return instrument_wave32_q15(ANALOG_WAVE_TABLE[wave == 6u ? 1u : 0u], phase);
}

static uint32_t analog_sync_increment(uint8_t note, int32_t offset16)
{
    return instrument_pitch_inc16((int32_t)note * 16 + offset16);
}

static int32_t analog_drive(int32_t sample, uint8_t drive)
{
    int32_t gain = 32768 + (int32_t)drive * 512;
    int32_t value = instrument_mul_q15_split(sample, gain);
    value = instrument_clamp_i32(value, -65535, 65535);
    if (value > 16384) value = 16384 + ((value - 16384) >> 1);
    else if (value < -16384) value = -16384 + ((value + 16384) >> 1);
    return instrument_clamp_i32(value, -32767, 32767);
}

static void instrument_analog_reset(void *opaque)
{
    instrument_analog_state_t *state = (instrument_analog_state_t *)opaque;
    memset(state, 0, sizeof *state);
    state->wave = 0;
    state->detune = 8;
    state->mix = 64;
    state->noise = 0;
    state->cutoff = 90;
    state->resonance = 30;
    state->drive = 0;
    state->keytrack = 64;
    state->filter_type = 0;
    state->shape = 64;
    state->env_filter = 64;
    instrument_mod_reset(&state->modulation);
}

static void instrument_analog_event(void *opaque, const instrument_event_t *event)
{
    instrument_analog_state_t *state = (instrument_analog_state_t *)opaque;
    if (event->type == INSTRUMENT_EVENT_CONTROL) {
        if (event->note >= INSTRUMENT_MOD_CTL_RATE && event->note < INSTRUMENT_MOD_CTL_COUNT) {
            instrument_mod_control(&state->modulation, event->note, event->velocity);
            if (event->note == INSTRUMENT_MOD_CTL_ROUTE && state->modulation.route > ANALOG_MOD_PWM)
                state->modulation.route = ANALOG_MOD_PWM;
            return;
        }
        switch (event->note) {
        case ANALOG_CTL_WAVE: state->wave = event->velocity % 7u; break;
        case ANALOG_CTL_DETUNE: state->detune = event->velocity; break;
        case ANALOG_CTL_MIX: state->mix = event->velocity; break;
        case ANALOG_CTL_NOISE: state->noise = event->velocity; break;
        case ANALOG_CTL_CUTOFF: state->cutoff = event->velocity; break;
        case ANALOG_CTL_RESONANCE: state->resonance = event->velocity; break;
        case ANALOG_CTL_DRIVE: state->drive = event->velocity; break;
        case ANALOG_CTL_KEYTRACK: state->keytrack = event->velocity; break;
        case ANALOG_CTL_FILTER_TYPE: state->filter_type = event->velocity % 3u; break;
        case ANALOG_CTL_SHAPE: state->shape = event->velocity; break;
        case ANALOG_CTL_ENV_FILTER: state->env_filter = event->velocity; break;
        default: break;
        }
        return;
    }
    if (event->type != INSTRUMENT_EVENT_NOTE_ON && event->type != INSTRUMENT_EVENT_NOTE_OFF
        && event->type != INSTRUMENT_EVENT_ALL_NOTES_OFF)
        return;
    {
        int32_t slot = instrument_osc_note_event(state->voice, ANALOG_VOICES, &state->ages, event, 68u);
        if (slot >= 0) instrument_mod_note_on(&state->modulation, event->note);
    }
}

static void instrument_analog_render(void *opaque, int32_t *out, uint32_t frames)
{
    instrument_analog_state_t *state = (instrument_analog_state_t *)opaque;
    uint32_t v, i;
    instrument_mod_render(&state->modulation, frames);
    int32_t mix2 = (int32_t)state->mix * 258;
    int32_t mix1 = 32767 - mix2;
    int32_t noise_gain = (int32_t)state->noise * 200;
    for (v = 0; v < ANALOG_VOICES; v++) {
        instrument_osc_voice_t *voice = &state->voice[v];
        uint32_t phase0 = voice->phase[0], phase1 = voice->phase[1];
        int32_t amp = voice->amp;
        int32_t cutoff = (int32_t)state->cutoff
                   + ((int32_t)state->keytrack * ((int32_t)voice->note - 60)) / 127;
        instrument_tsvf_t filter = {0, 0, 0, 0};
        if (!amp && !voice->gate)
            continue;
        if (state->wave == 5u) {
            int32_t offset16 = (int32_t)state->detune * 3 + ((int32_t)state->shape - 64) * 3;
            voice->increment[1] = analog_sync_increment(voice->note, offset16);
        } else if (state->wave == 6u) {
            voice->increment[1] = instrument_pitch_inc_cents(voice->note, state->detune) >> 1;
        } else {
            voice->increment[1] = instrument_pitch_inc_cents(voice->note, state->detune);
        }
        for (i = 0; i < frames; i++) {
            int32_t a, b, sample, shape = state->shape;
            if ((i & 15u) == 0u) {
                int32_t mod = instrument_mod_value(&state->modulation, i, voice->env_level);
                int32_t mod_cutoff = state->modulation.route == ANALOG_MOD_CUTOFF ? mod : 0;
                int32_t mod_shape = state->modulation.route == ANALOG_MOD_SHAPE
                    || (state->modulation.route == ANALOG_MOD_PWM && state->wave == 4u) ? mod : 0;
                int32_t current_cutoff = cutoff + mod_cutoff
                    + (((int32_t)voice->env_level * ((int32_t)state->env_filter - 64)) >> 15);
                current_cutoff = instrument_clamp_i32(current_cutoff, 0, 127);
                shape = instrument_clamp_i32((int32_t)state->shape + mod_shape, 0, 127);
                instrument_tsvf_prepare(&filter, (uint32_t)current_cutoff, state->resonance);
            }
            amp = instrument_osc_amp_tick(voice);
            a = analog_wave_sample(state->wave == 5u || state->wave == 6u ? 0u : state->wave,
                                   phase0, (uint8_t)shape);
            b = analog_wave_sample(state->wave == 5u ? 0u : state->wave == 6u ? 6u : state->wave,
                                   phase1, (uint8_t)shape);
            sample = ((a * mix1) >> 15) + ((b * mix2) >> 15);
            if (noise_gain)
                sample += instrument_mul_q15_split((int32_t)(instrument_xorshift32(&voice->rng) >> 17) - 16384,
                                                   noise_gain);
            phase0 += voice->increment[0];
            if (state->wave == 5u && phase0 < voice->increment[0])
                phase1 = 0;
            else
                phase1 += voice->increment[1];
            if (state->drive) sample = analog_drive(sample, state->drive);
            sample = instrument_tsvf_process(&filter, sample, &voice->filter[0], &voice->filter[1],
                                             state->filter_type);
            sample = instrument_soft_knee(sample, 16000);
            sample = (sample * amp) >> 15;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
        }
        voice->phase[0] = phase0;
        voice->phase[1] = phase1;
        voice->amp = amp;
    }
}

static int instrument_analog_quiet(const void *opaque)
{
    const instrument_analog_state_t *state = (const instrument_analog_state_t *)opaque;
    uint32_t i;
    for (i = 0; i < ANALOG_VOICES; i++)
        if (state->voice[i].env_stage != INSTRUMENT_ENV_OFF || state->voice[i].amp)
            return 0;
    return 1;
}

static const instrument_desc_t INSTRUMENT_ANALOG = {
    INSTRUMENT_ID_ANALOG, INSTRUMENT_ABI_VERSION, "ANALOG", sizeof(instrument_analog_state_t), 4u,
    instrument_analog_reset, instrument_analog_event, instrument_analog_render, instrument_analog_quiet
};
