/* SPDX-License-Identifier: GPL-3.0-only */
/* Barge-native LFO/source, routing, retrigger, and voice-lifetime vectors. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/instrument.c"

static int failed;
static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failed++;
    }
}

static void control(const instrument_desc_t *desc, void *state, uint8_t parameter, uint8_t value)
{
    instrument_event_t event = {INSTRUMENT_EVENT_CONTROL, 0, parameter, value};
    desc->event(state, &event);
}

static void note(const instrument_desc_t *desc, void *state, uint8_t type, uint8_t key)
{
    instrument_event_t event = {type, 0, key, type == INSTRUMENT_EVENT_NOTE_ON ? 120u : 0u};
    desc->event(state, &event);
}

static void render(const instrument_desc_t *desc, void *state, int32_t *out, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames * 2u; i++) out[i] = 0;
    desc->render(state, out, frames);
}

static void test_waves_and_endpoints(void)
{
    instrument_modulation_t mod;
    uint32_t wave, i;
    for (wave = 0; wave < 5u; wave++) {
        int32_t low = 32767, high = -32768;
        instrument_mod_reset(&mod);
        mod.wave = (uint8_t)wave;
        mod.increment = 0x40000000u;
        for (i = 0; i < 8u; i++) {
            int32_t sample;
            instrument_mod_render(&mod, 1u);
            sample = mod.block[0];
            if (sample < low) low = sample;
            if (sample > high) high = sample;
            check(sample >= -32768 && sample <= 32767, "every LFO waveform stays in signed Q15 bounds");
        }
        check(high > low, "every supported LFO waveform produces a changing signal");
    }
    instrument_mod_reset(&mod);
    instrument_mod_control(&mod, INSTRUMENT_MOD_CTL_RATE, 0u);
    check(mod.increment > 0u && mod.increment < 2000u, "minimum LFO rate remains positive and slow");
    instrument_mod_control(&mod, INSTRUMENT_MOD_CTL_RATE, 127u);
    check(mod.increment > 1000000u && mod.increment < 1300000u, "maximum LFO rate is bounded near 12 Hz");
    mod.block[0] = 32767;
    mod.depth = 0u;
    check(instrument_mod_value(&mod, 0u, 32767u) == 0, "zero modulation depth disables a route");
    mod.depth = 127u;
    check(instrument_mod_value(&mod, 0u, 0u) >= 126, "maximum LFO depth reaches its positive endpoint");
    mod.block[0] = -32768;
    check(instrument_mod_value(&mod, 0u, 0u) <= -127, "maximum LFO depth reaches its negative endpoint");
    mod.source = 1u;
    check(instrument_mod_value(&mod, 0u, 0u) == 0
          && instrument_mod_value(&mod, 0u, 32767u) >= 126,
          "ADSR source spans zero to the selected depth");
}

static void test_retrigger_and_free_run(void)
{
    instrument_modulation_t a, b;
    instrument_mod_reset(&a);
    instrument_mod_reset(&b);
    a.wave = b.wave = 4u;
    a.increment = b.increment = 0x40000000u;
    a.retrigger = b.retrigger = 1u;
    instrument_mod_note_on(&a, 60u);
    instrument_mod_note_on(&b, 60u);
    check(a.phase == 0u && a.held == b.held && a.rng == b.rng,
          "retriggered sample-hold starts deterministically for the same note");
    instrument_mod_render(&a, 8u);
    instrument_mod_render(&b, 8u);
    check(a.phase == b.phase && a.held == b.held, "retriggered LFO sequences are repeatable");
    a.retrigger = 0u;
    a.phase = 0x12345678u;
    instrument_mod_note_on(&a, 67u);
    check(a.phase == 0x12345678u, "free-running phase is continuous across note-on");
    instrument_mod_render(&a, 1u);
    check(a.phase == 0x12345678u + a.increment, "free-running phase advances sample by sample");
}

static void configure_shared_mod(const instrument_desc_t *desc, void *state, uint8_t base,
                                 uint8_t wave, uint8_t depth, uint8_t route, uint8_t source)
{
    control(desc, state, INSTRUMENT_MOD_CTL_RATE, 127u);
    control(desc, state, INSTRUMENT_MOD_CTL_WAVE, wave);
    control(desc, state, INSTRUMENT_MOD_CTL_DEPTH, depth);
    control(desc, state, INSTRUMENT_MOD_CTL_ROUTE, route);
    control(desc, state, INSTRUMENT_MOD_CTL_MODE, 1u);
    control(desc, state, INSTRUMENT_MOD_CTL_SOURCE, source);
    (void)base;
}

static int different(const int32_t *a, const int32_t *b, uint32_t frames)
{
    return memcmp(a, b, frames * 2u * sizeof(int32_t)) != 0;
}

static void test_analog_routes_and_lifetime(void)
{
    instrument_analog_state_t dry, cutoff, shape, pwm, env;
    static int32_t a[2u * 2048u], b[2u * 2048u], c[2u * 2048u], d[2u * 2048u];
    uint32_t block, i;
    instrument_adsr_set(0u, 0u);
    instrument_adsr_set(1u, 0u);
    instrument_adsr_set(2u, 100u);
    instrument_adsr_set(3u, 0u);
    instrument_adsr_prepare();
    INSTRUMENT_ANALOG.reset(&dry);
    INSTRUMENT_ANALOG.reset(&cutoff);
    INSTRUMENT_ANALOG.reset(&shape);
    INSTRUMENT_ANALOG.reset(&pwm);
    control(&INSTRUMENT_ANALOG, &dry, 0u, 4u);
    control(&INSTRUMENT_ANALOG, &cutoff, 0u, 4u);
    control(&INSTRUMENT_ANALOG, &shape, 0u, 4u);
    control(&INSTRUMENT_ANALOG, &pwm, 0u, 4u);
    configure_shared_mod(&INSTRUMENT_ANALOG, &cutoff, 0, 0u, 127u, 0u, 0u);
    configure_shared_mod(&INSTRUMENT_ANALOG, &shape, 0, 1u, 127u, 1u, 0u);
    configure_shared_mod(&INSTRUMENT_ANALOG, &pwm, 0, 2u, 127u, 2u, 0u);
    note(&INSTRUMENT_ANALOG, &dry, INSTRUMENT_EVENT_NOTE_ON, 60u);
    note(&INSTRUMENT_ANALOG, &cutoff, INSTRUMENT_EVENT_NOTE_ON, 60u);
    note(&INSTRUMENT_ANALOG, &shape, INSTRUMENT_EVENT_NOTE_ON, 60u);
    note(&INSTRUMENT_ANALOG, &pwm, INSTRUMENT_EVENT_NOTE_ON, 60u);
    for (block = 0; block < 8u; block++) {
        render(&INSTRUMENT_ANALOG, &dry, a + block * 512u, 256u);
        render(&INSTRUMENT_ANALOG, &cutoff, b + block * 512u, 256u);
        render(&INSTRUMENT_ANALOG, &shape, c + block * 512u, 256u);
        render(&INSTRUMENT_ANALOG, &pwm, d + block * 512u, 256u);
    }
    check(different(a, b, 2048u), "ANALOG cutoff modulation changes the rendered signal");
    check(different(a, c, 2048u), "ANALOG shape modulation changes the rendered signal");
    check(different(a, d, 2048u), "ANALOG PWM modulation changes the rendered signal");

    INSTRUMENT_ANALOG.reset(&env);
    control(&INSTRUMENT_ANALOG, &env, 0u, 4u);
    configure_shared_mod(&INSTRUMENT_ANALOG, &env, 0, 0u, 127u, 0u, 1u);
    note(&INSTRUMENT_ANALOG, &env, INSTRUMENT_EVENT_NOTE_ON, 60u);
    for (block = 0; block < 4u; block++) render(&INSTRUMENT_ANALOG, &env, b, 256u);
    note(&INSTRUMENT_ANALOG, &env, INSTRUMENT_EVENT_NOTE_OFF, 60u);
    for (block = 0; block < 4u; block++) {
        render(&INSTRUMENT_ANALOG, &env, b, 256u);
        for (i = 0; i < 512u; i++)
            check(b[i] >= -100000 && b[i] <= 100000, "ANALOG modulation output remains bounded");
    }
    check(INSTRUMENT_ANALOG.quiet(&env), "ADSR-sourced modulation follows note-off and releases the voice");
    check(env.voice[0].env_stage == INSTRUMENT_ENV_OFF, "modulation does not leave a stuck ANALOG voice");
}

static void test_phase_routes_and_lifetime(void)
{
    instrument_phase_state_t dry, dcw, pitch, sub, line, env;
    static int32_t a[2u * 2048u], b[2u * 2048u], c[2u * 2048u], d[2u * 2048u], e[2u * 2048u];
    uint32_t block, i;
    instrument_adsr_set(0u, 0u);
    instrument_adsr_set(1u, 0u);
    instrument_adsr_set(2u, 100u);
    instrument_adsr_set(3u, 0u);
    instrument_adsr_prepare();
    INSTRUMENT_PHASE.reset(&dry);
    INSTRUMENT_PHASE.reset(&dcw);
    INSTRUMENT_PHASE.reset(&pitch);
    INSTRUMENT_PHASE.reset(&sub);
    INSTRUMENT_PHASE.reset(&line);
    control(&INSTRUMENT_PHASE, &dry, 1u, 4u);
    control(&INSTRUMENT_PHASE, &dcw, 1u, 4u);
    control(&INSTRUMENT_PHASE, &pitch, 1u, 4u);
    control(&INSTRUMENT_PHASE, &sub, 1u, 4u);
    control(&INSTRUMENT_PHASE, &line, 1u, 4u);
    control(&INSTRUMENT_PHASE, &dry, 1u, 2u);
    control(&INSTRUMENT_PHASE, &dcw, 1u, 2u);
    control(&INSTRUMENT_PHASE, &pitch, 1u, 2u);
    control(&INSTRUMENT_PHASE, &sub, 1u, 2u);
    control(&INSTRUMENT_PHASE, &line, 1u, 2u);
    control(&INSTRUMENT_PHASE, &dry, 4u, 64u);
    control(&INSTRUMENT_PHASE, &dcw, 4u, 64u);
    control(&INSTRUMENT_PHASE, &pitch, 4u, 64u);
    control(&INSTRUMENT_PHASE, &sub, 4u, 64u);
    control(&INSTRUMENT_PHASE, &line, 4u, 64u);
    configure_shared_mod(&INSTRUMENT_PHASE, &dcw, 0, 0u, 127u, 0u, 0u);
    configure_shared_mod(&INSTRUMENT_PHASE, &pitch, 0, 1u, 127u, 1u, 0u);
    configure_shared_mod(&INSTRUMENT_PHASE, &sub, 0, 2u, 127u, 2u, 0u);
    configure_shared_mod(&INSTRUMENT_PHASE, &line, 0, 3u, 127u, 3u, 0u);
    note(&INSTRUMENT_PHASE, &dry, INSTRUMENT_EVENT_NOTE_ON, 60u);
    note(&INSTRUMENT_PHASE, &dcw, INSTRUMENT_EVENT_NOTE_ON, 60u);
    note(&INSTRUMENT_PHASE, &pitch, INSTRUMENT_EVENT_NOTE_ON, 60u);
    note(&INSTRUMENT_PHASE, &sub, INSTRUMENT_EVENT_NOTE_ON, 60u);
    note(&INSTRUMENT_PHASE, &line, INSTRUMENT_EVENT_NOTE_ON, 60u);
    for (block = 0; block < 8u; block++) {
        render(&INSTRUMENT_PHASE, &dry, a + block * 512u, 256u);
        render(&INSTRUMENT_PHASE, &dcw, b + block * 512u, 256u);
        render(&INSTRUMENT_PHASE, &pitch, c + block * 512u, 256u);
        render(&INSTRUMENT_PHASE, &sub, d + block * 512u, 256u);
        render(&INSTRUMENT_PHASE, &line, e + block * 512u, 256u);
    }
    check(different(a, b, 2048u), "PHASE DCW modulation changes the rendered signal");
    check(different(a, c, 2048u), "PHASE pitch modulation changes the rendered signal");
    check(different(a, d, 2048u), "PHASE sub-level modulation changes the rendered signal");
    check(different(a, e, 2048u), "PHASE line modulation changes the rendered signal");

    INSTRUMENT_PHASE.reset(&env);
    control(&INSTRUMENT_PHASE, &env, 1u, 4u);
    configure_shared_mod(&INSTRUMENT_PHASE, &env, 0, 0u, 127u, 0u, 1u);
    note(&INSTRUMENT_PHASE, &env, INSTRUMENT_EVENT_NOTE_ON, 60u);
    for (block = 0; block < 4u; block++) render(&INSTRUMENT_PHASE, &env, b, 256u);
    note(&INSTRUMENT_PHASE, &env, INSTRUMENT_EVENT_NOTE_OFF, 60u);
    for (block = 0; block < 4u; block++) {
        render(&INSTRUMENT_PHASE, &env, b, 256u);
        for (i = 0; i < 512u; i++)
            check(b[i] >= -100000 && b[i] <= 100000, "PHASE modulation output remains bounded");
    }
    check(INSTRUMENT_PHASE.quiet(&env), "ADSR-sourced PHASE modulation follows note-off and releases the voice");
    check(env.voice[0].env_stage == INSTRUMENT_ENV_OFF, "modulation does not leave a stuck PHASE voice");
}

int main(void)
{
    test_waves_and_endpoints();
    test_retrigger_and_free_run();
    test_analog_routes_and_lifetime();
    test_phase_routes_and_lifetime();
    if (failed) return 1;
    puts("modulation waveforms, endpoints, routes, retrigger/free-run, ADSR and voice lifetime OK");
    return 0;
}
