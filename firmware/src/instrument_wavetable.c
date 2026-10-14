/* SPDX-License-Identifier: GPL-3.0-only */
/* A built-in wavetable renderer with a runtime-uploaded, data-only 32-point waveform. */
#define WAVETABLE_VOICES 4u
#define WAVETABLE_POINTS 32u

typedef struct {
    instrument_osc_voice_t voice[WAVETABLE_VOICES];
    uint32_t ages;
    uint8_t wave[WAVETABLE_POINTS];
} instrument_wavetable_state_t;

static void instrument_wavetable_reset(void *opaque)
{
    instrument_wavetable_state_t *state = (instrument_wavetable_state_t *)opaque;
    memset(state, 0, sizeof *state);
    memcpy(state->wave, INSTRUMENT_WAVE_DEFAULT, sizeof state->wave);
}

static void instrument_wavetable_event(void *opaque, const instrument_event_t *event)
{
    instrument_wavetable_state_t *state = (instrument_wavetable_state_t *)opaque;
    instrument_osc_note_event(state->voice, WAVETABLE_VOICES, &state->ages, event, 58u);
}

static void instrument_wavetable_set(void *opaque, const uint8_t *samples)
{
    instrument_wavetable_state_t *state = (instrument_wavetable_state_t *)opaque;
    memcpy(state->wave, samples, WAVETABLE_POINTS);
}

static void instrument_wavetable_render(void *opaque, int32_t *out, uint32_t frames)
{
    instrument_wavetable_state_t *state = (instrument_wavetable_state_t *)opaque;
    uint32_t v, i;
    for (v = 0; v < WAVETABLE_VOICES; v++) {
        instrument_osc_voice_t *voice = &state->voice[v];
        uint32_t phase = voice->phase[0], increment = voice->increment[0];
        int32_t amp = voice->amp, gate = voice->gate;
        if (!amp && !gate) continue;
        for (i = 0; i < frames; i++) {
            int32_t sample;
            amp = instrument_osc_amp_tick(voice);
            sample = instrument_wave32_q15(state->wave, phase);
            sample = (sample * amp) >> 15;
            out[2u * i] += sample;
            out[2u * i + 1u] += sample;
            phase += increment;
        }
        voice->phase[0] = phase;
        voice->amp = amp;
    }
}

static int instrument_wavetable_quiet(const void *opaque)
{
    const instrument_wavetable_state_t *state = (const instrument_wavetable_state_t *)opaque;
    uint32_t i;
    for (i = 0; i < WAVETABLE_VOICES; i++)
        if (state->voice[i].gate || state->voice[i].amp) return 0;
    return 1;
}

static const instrument_desc_t INSTRUMENT_WAVETABLE = {
    INSTRUMENT_ID_WAVETABLE, INSTRUMENT_ABI_VERSION, "WAVE", sizeof(instrument_wavetable_state_t), 4u,
    instrument_wavetable_reset, instrument_wavetable_event, instrument_wavetable_render, instrument_wavetable_quiet
};
