/* SPDX-License-Identifier: GPL-3.0-only */
/* Barge-owned, caller-state LFO/source helper. Included once by instrument.c. */
typedef struct {
    uint32_t phase;
    uint32_t increment;
    uint32_t rng;
    int16_t held;
    int16_t block[INSTRUMENT_MAX_FRAMES];
    uint8_t rate;
    uint8_t wave;
    uint8_t depth;
    uint8_t route;
    uint8_t retrigger;
    uint8_t source;
} instrument_modulation_t;

static uint32_t instrument_mod_increment(uint8_t rate)
{
    uint32_t frequency_millihz = 10u + ((uint32_t)rate * 11990u) / 127u;
    return (frequency_millihz * 97337u) / 1000u;
}

static void instrument_mod_reset(instrument_modulation_t *mod)
{
    memset(mod, 0, sizeof *mod);
    mod->rate = 32u;
    mod->wave = 0u;
    mod->depth = 0u;
    mod->route = 0u;
    mod->retrigger = 0u;
    mod->source = 0u;
    mod->increment = instrument_mod_increment(mod->rate);
    mod->rng = 0xA341316Cu;
}

static void instrument_mod_control(instrument_modulation_t *mod, uint8_t parameter, uint8_t value)
{
    if (value > 127u) value = 127u;
    switch (parameter) {
    case INSTRUMENT_MOD_CTL_RATE:
        mod->rate = value;
        mod->increment = instrument_mod_increment(value);
        break;
    case INSTRUMENT_MOD_CTL_WAVE:
        mod->wave = value > 4u ? 4u : value;
        break;
    case INSTRUMENT_MOD_CTL_DEPTH:
        mod->depth = value;
        break;
    case INSTRUMENT_MOD_CTL_ROUTE:
        mod->route = value > 3u ? 3u : value;
        break;
    case INSTRUMENT_MOD_CTL_MODE:
        mod->retrigger = value ? 1u : 0u;
        break;
    case INSTRUMENT_MOD_CTL_SOURCE:
        mod->source = value ? 1u : 0u;
        break;
    default:
        break;
    }
}

static void instrument_mod_note_on(instrument_modulation_t *mod, uint8_t note)
{
    if (mod->retrigger) {
        mod->phase = 0;
        mod->rng = 0xA341316Cu ^ ((uint32_t)note * 0x9E3779B9u);
        if (!mod->rng) mod->rng = 1u;
        mod->held = (int16_t)((int32_t)(instrument_xorshift32(&mod->rng) >> 16) - 32768);
    }
}

static int32_t instrument_mod_wave(const instrument_modulation_t *mod)
{
    uint32_t phase = mod->phase;
    uint32_t p = phase >> 16;
    switch (mod->wave) {
    case 1u: /* triangle */
        return p < 32768u ? -32767 + (int32_t)(p * 2u) : 98303 - (int32_t)(p * 2u);
    case 2u: /* rising saw */
        return (int32_t)p - 32768;
    case 3u: /* square */
        return p < 32768u ? 32767 : -32767;
    case 4u: /* bounded sample and hold */
        return mod->held;
    default: /* sine approximation already used by Barge's PHASE oscillator */
        return instrument_sine_q15(phase);
    }
}

static void instrument_mod_render(instrument_modulation_t *mod, uint32_t frames)
{
    uint32_t i;
    if (frames > INSTRUMENT_MAX_FRAMES) frames = INSTRUMENT_MAX_FRAMES;
    for (i = 0; i < frames; i++) {
        uint32_t old = mod->phase;
        mod->block[i] = (int16_t)instrument_mod_wave(mod);
        mod->phase += mod->increment;
        if (mod->phase < old && mod->wave == 4u)
            mod->held = (int16_t)((int32_t)(instrument_xorshift32(&mod->rng) >> 16) - 32768);
    }
}

/* Return destination units in [-depth,+depth] for LFO, [0,depth] for ADSR. */
static int32_t instrument_mod_value(const instrument_modulation_t *mod, uint32_t frame, uint16_t env_level)
{
    int32_t source;
    if (!mod->depth) return 0;
    if (mod->source)
        source = env_level;
    else
        source = mod->block[frame];
    return (source * (int32_t)mod->depth) >> 15;
}
