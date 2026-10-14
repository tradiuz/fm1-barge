/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * PHASE wave core adapted from Felucca's GPL-3.0 eng_phase.c / CrispyZebra.
 * Barge owns voice allocation, ADSR, parameter controls and audio routing. */
#define PHASE_VOICES 4u
#define PHASE_FOLD_G0 163u
#define PHASE_FOLD_REF 42852281u
enum { PHASE_MOD_DCW, PHASE_MOD_PITCH, PHASE_MOD_SUB, PHASE_MOD_LINE };

typedef struct {
    instrument_osc_voice_t voice[PHASE_VOICES];
    instrument_modulation_t modulation;
    uint32_t ages;
    uint8_t param[8]; /* WAVE,WAVE2,DCW,ENV,DTN,LINE,SUB,FOLD */
} instrument_phase_state_t;

typedef struct { uint32_t wave, x, peak, resonance, k0, k1; } phase_wave_t;

static uint32_t phase_slope(uint32_t span, uint32_t length)
{
    return (span << 16) / (length ? length : 1u);
}

static void phase_setup(phase_wave_t *shape, uint32_t wave, uint32_t dcw)
{
    uint32_t x;
    shape->wave = wave;
    switch (wave) {
    case 0: /* SAW */
        x = 32768u + ((dcw * 32767u) >> 16);
        shape->k0 = phase_slope(32768u, x);
        shape->k1 = phase_slope(32767u, 65535u - x);
        break;
    case 1: /* SQUARE */
        x = 32768u - ((dcw * 31120u) >> 16);
        shape->k0 = phase_slope(32768u, x);
        shape->k1 = phase_slope(32767u, x);
        break;
    case 2: /* PULSE */
        x = 65535u - ((dcw * 63487u) >> 16);
        shape->k0 = phase_slope(65535u, x);
        break;
    case 3: /* DOUBLE SINE */
        x = 65535u - ((dcw * 49151u) >> 16);
        shape->k0 = phase_slope(65535u, x);
        shape->k1 = phase_slope(65535u, 65535u - x);
        break;
    case 4: /* SAW-PULSE */
        x = 65535u - ((dcw * 32767u) >> 16);
        shape->peak = (x * (32768u + ((dcw * 29491u) >> 16))) >> 16;
        if (!shape->peak) shape->peak = 1u;
        shape->k0 = phase_slope(32768u, shape->peak);
        shape->k1 = phase_slope(32767u, x - shape->peak);
        break;
    default: /* RESONANCE */
        x = 0;
        shape->resonance = (dcw * 44000u) >> 16;
        break;
    }
    shape->x = x;
}

static int32_t phase_cos(uint32_t phase16)
{
    return -instrument_sine_q15(((phase16 & 0xFFFFu) << 16) + 0x40000000u);
}

static int32_t phase_wave(const phase_wave_t *shape, uint32_t phase16)
{
    uint32_t bent, x = shape->x;
    switch (shape->wave) {
    case 0:
        bent = phase16 < x ? (phase16 * shape->k0) >> 16
                           : 32768u + (((phase16 - x) * shape->k1) >> 16);
        break;
    case 1:
        if (phase16 < x) bent = (phase16 * shape->k0) >> 16;
        else if (phase16 < 32768u) bent = 32768u;
        else if (phase16 < 32768u + x) bent = 32768u + (((phase16 - 32768u) * shape->k1) >> 16);
        else bent = 65535u;
        break;
    case 2:
        bent = phase16 < x ? (phase16 * shape->k0) >> 16 : 65535u;
        break;
    case 3:
        bent = phase16 < x ? (phase16 * shape->k0) >> 16 : ((phase16 - x) * shape->k1) >> 16;
        break;
    case 4:
        if (phase16 >= x) bent = 65535u;
        else if (phase16 < shape->peak) bent = (phase16 * shape->k0) >> 16;
        else bent = 32768u + (((phase16 - shape->peak) * shape->k1) >> 16);
        break;
    default: {
        uint32_t core_phase = (phase16 + ((phase16 * shape->resonance) >> 16) * 7u) & 0xFFFFu;
        uint32_t window;
        int32_t core = (phase_cos(core_phase) * 5) >> 3;
        if (shape->wave == 5u) window = 65535u - phase16;
        else if (shape->wave == 6u) window = phase16 < 32768u ? phase16 << 1 : (65535u - phase16) << 1;
        else window = phase16 < 16384u ? phase16 << 2
                    : phase16 > 49152u ? (65535u - phase16) << 2 : 65535u;
        if (window > 65535u) window = 65535u;
        return (core * (int32_t)window) >> 16;
    }
    }
    return phase_cos(bent);
}

static uint32_t phase_fold_gain(uint32_t fold, uint32_t increment)
{
    uint32_t ext = fold * ((2048u - PHASE_FOLD_G0) << 4) / 127u;
    if (increment > PHASE_FOLD_REF)
        ext = ext * (PHASE_FOLD_REF >> 12) / (increment >> 12);
    return PHASE_FOLD_G0 + (ext >> 4);
}

static int32_t phase_fold_sample(instrument_osc_voice_t *voice, int32_t sample, uint32_t gain)
{
    int32_t folded = instrument_sine_q15((uint32_t)(sample * (int32_t)gain) << 7);
    voice->filter[1] += folded - voice->filter[0] - (voice->filter[1] >> 9);
    voice->filter[0] = folded;
    return instrument_clamp_i32(voice->filter[1], -65535, 65535);
}

static void instrument_phase_reset(void *opaque)
{
    instrument_phase_state_t *state = (instrument_phase_state_t *)opaque;
    memset(state, 0, sizeof *state);
    state->param[0] = 0;
    state->param[1] = 0;
    state->param[2] = 60;
    state->param[3] = 64;
    state->param[4] = 0;
    state->param[5] = 0;
    state->param[6] = 0;
    state->param[7] = 0;
    instrument_mod_reset(&state->modulation);
}

static void instrument_phase_event(void *opaque, const instrument_event_t *event)
{
    instrument_phase_state_t *state = (instrument_phase_state_t *)opaque;
    if (event->type == INSTRUMENT_EVENT_CONTROL) {
        if (event->note >= INSTRUMENT_MOD_CTL_RATE && event->note < INSTRUMENT_MOD_CTL_COUNT) {
            instrument_mod_control(&state->modulation, event->note, event->velocity);
            return;
        }
        if (event->note < 8u) {
            uint8_t value = event->velocity;
            if (event->note == 0u && value > 7u) value = 7u;
            if (event->note == 1u && value > 8u) value = 8u;
            if (event->note == 5u && value > 1u) value = 1u;
            state->param[event->note] = value;
        }
        return;
    }
    if (event->type == INSTRUMENT_EVENT_NOTE_ON) {
        int32_t slot = instrument_osc_note_event(state->voice, PHASE_VOICES, &state->ages, event, 58u);
        if (slot >= 0) {
            instrument_osc_voice_t *voice = &state->voice[slot];
            voice->phase[1] = 0;
            voice->phase[2] = 0;
            voice->flags = 0;
            voice->filter[0] = voice->filter[1] = 0;
            voice->mod[0] = 0;
            instrument_mod_note_on(&state->modulation, event->note);
        }
        return;
    }
    instrument_osc_note_event(state->voice, PHASE_VOICES, &state->ages, event, 58u);
}

static void instrument_phase_render(void *opaque, int32_t *out, uint32_t frames)
{
    instrument_phase_state_t *state = (instrument_phase_state_t *)opaque;
    uint32_t v, i;
    uint32_t wave1 = state->param[0] & 7u;
    uint32_t wave2 = state->param[1];
    int32_t detune = state->param[4];
    uint32_t line2 = detune != 0 || state->param[5] != 0;
    uint32_t ring = state->param[5] != 0;
    uint32_t sub_gain = (uint32_t)state->param[6] * 200u;
    uint32_t fold = state->param[7];
    instrument_mod_render(&state->modulation, frames);
    for (v = 0; v < PHASE_VOICES; v++) {
        instrument_osc_voice_t *voice = &state->voice[v];
        uint32_t phase = voice->phase[0], phase2 = voice->phase[1], phase_sub = voice->phase[2];
        uint32_t increment = voice->increment[0], increment2 = voice->increment[0], flags = voice->flags;
        uint32_t effective_wave2 = wave2 ? wave2 - 1u : wave1;
        int32_t amp = voice->amp;
        uint32_t dcw = 0, fg = 0, line_mix = 0, old_folded = (flags >> 2) & 1u;
        uint32_t fold_active = fold != 0;
        phase_wave_t shape1, shape2;
        if (!amp && !voice->gate) continue;
        if (fold_active && !old_folded) voice->filter[0] = voice->filter[1] = 0;
        for (i = 0; i < frames; i++) {
            uint32_t old_phase = phase, old_phase2 = phase2;
            int32_t raw, second = 0, unfolded, folded, sample;
            if ((i & 15u) == 0u) {
                int32_t mod = instrument_mod_value(&state->modulation, i, voice->env_level);
                int32_t current_depth = (int32_t)state->param[2]
                    + (((int32_t)voice->env_level * state->param[3]) >> 15);
                if (state->modulation.route == PHASE_MOD_DCW) current_depth += mod;
                current_depth = instrument_clamp_i32(current_depth, 0, 127);
                dcw = (uint32_t)current_depth * 56000u / 127u;
                if (state->modulation.route == PHASE_MOD_PITCH) {
                    int32_t pitch_offset16 = (mod * 192) / 127;
                    increment = instrument_pitch_inc16((int32_t)voice->note * 16 + pitch_offset16);
                } else increment = voice->increment[0];
                increment2 = detune ? instrument_pitch_inc_cents(voice->note, (uint8_t)detune) : increment;
                line_mix = ring ? 127u : (line2 ? 64u : 0u);
                if (state->modulation.route == PHASE_MOD_LINE)
                    line_mix = (uint32_t)instrument_clamp_i32((int32_t)line_mix + mod, 0, 127);
                phase_setup(&shape1, wave1, dcw);
                phase_setup(&shape2, effective_wave2, dcw);
                if (fold_active) fg = phase_fold_gain(fold, increment);
                else if (old_folded) fg = (uint32_t)voice->mod[0];
                if (state->modulation.route == PHASE_MOD_SUB) {
                    int32_t sub = instrument_clamp_i32((int32_t)state->param[6] + mod, 0, 127);
                    sub_gain = (uint32_t)sub * 200u;
                } else sub_gain = (uint32_t)state->param[6] * 200u;
            }
            amp = instrument_osc_amp_tick(voice);
            raw = phase_wave((flags & 1u) ? &shape2 : &shape1, phase >> 16);
            phase += increment;
            if (phase < old_phase) flags ^= 1u;
            if (line_mix || detune) {
                second = phase_wave((flags & 2u) ? &shape2 : &shape1, phase2 >> 16);
                phase2 += increment2;
                if (phase2 < old_phase2) flags ^= 2u;
                if (state->modulation.route == PHASE_MOD_LINE) {
                    int32_t ring_sample = (raw * second) >> 15;
                    raw = (raw * (int32_t)(127u - line_mix) + ring_sample * (int32_t)line_mix) / 127;
                } else {
                    raw = ring ? (raw * second) >> 15
                        : (raw * (int32_t)(127u - line_mix) + second * (int32_t)line_mix) / 127;
                }
            }
            unfolded = raw;
            if (sub_gain) {
                phase_sub += increment >> 1;
                unfolded += instrument_mul_q15_split(instrument_sine_q15(phase_sub), (int32_t)sub_gain);
            }
            if (fold_active || old_folded) {
                folded = phase_fold_sample(voice, raw, fg);
                if (sub_gain) folded += instrument_mul_q15_split(instrument_sine_q15(phase_sub), (int32_t)sub_gain);
                if (fold_active != old_folded) {
                    int32_t blend = (int32_t)(fold_active ? i + 1u : frames - 1u - i);
                    sample = unfolded + (folded - unfolded) * blend / (int32_t)frames;
                } else sample = folded;
            } else sample = unfolded;
            sample = instrument_clamp_i32(sample, -65535, 65535);
            sample = (sample * amp) >> 15;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
        }
        if (fold_active) voice->mod[0] = (int32_t)fg;
        if (fold_active) flags |= 4u; else flags &= ~4u;
        voice->phase[0] = phase;
        voice->phase[1] = phase2;
        voice->phase[2] = phase_sub;
        voice->flags = (uint8_t)flags;
        voice->amp = amp;
    }
}

static int instrument_phase_quiet(const void *opaque)
{
    const instrument_phase_state_t *state = (const instrument_phase_state_t *)opaque;
    uint32_t i;
    for (i = 0; i < PHASE_VOICES; i++)
        if (state->voice[i].env_stage != INSTRUMENT_ENV_OFF || state->voice[i].amp) return 0;
    return 1;
}

static const instrument_desc_t INSTRUMENT_PHASE = {
    INSTRUMENT_ID_PHASE, INSTRUMENT_ABI_VERSION, "PHASE", sizeof(instrument_phase_state_t), 4u,
    instrument_phase_reset, instrument_phase_event, instrument_phase_render, instrument_phase_quiet
};
