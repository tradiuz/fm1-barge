/* SPDX-License-Identifier: GPL-3.0-only */
/* Deterministic vectors for the shared block-prepared ADSR implementation. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/instrument.c"

static int check(int condition, const char *message)
{
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return !condition;
}

int main(void)
{
    int failed = 0;
    instrument_osc_voice_t voice;
    int32_t previous = 0, level;
    uint32_t i;
    memset(&voice, 0, sizeof voice);
    instrument_adsr_set(0, 0);   /* 32-sample attack */
    instrument_adsr_set(1, 0);   /* 32-sample decay */
    instrument_adsr_set(2, 64);  /* half-ish sustain */
    instrument_adsr_set(3, 0);   /* 32-sample release */
    instrument_adsr_prepare();
    instrument_adsr_note_on(&voice.gate, &voice.amp, &voice.env_level, &voice.env_stage, 100, 80);
    failed += check(voice.env_stage == INSTRUMENT_ENV_ATTACK && voice.gate == 8000,
                    "note-on starts attack at velocity-scaled peak");
    for (i = 0; i < 32u; i++) {
        level = instrument_adsr_tick(&voice.gate, &voice.env_level, &voice.env_stage);
        failed += check(level >= previous, "attack is monotonic");
        previous = level;
    }
    failed += check(voice.env_stage == INSTRUMENT_ENV_DECAY && voice.env_level == 32767u,
                    "attack reaches full scale then enters decay");
    for (i = 0; i < 32u; i++)
        (void)instrument_adsr_tick(&voice.gate, &voice.env_level, &voice.env_stage);
    failed += check(voice.env_stage == INSTRUMENT_ENV_SUSTAIN && voice.env_level == 64u * 258u,
                    "decay reaches configured sustain");
    level = instrument_adsr_tick(&voice.gate, &voice.env_level, &voice.env_stage);
    failed += check(level == (8000 * (64 * 258)) / 32768, "sustain holds the velocity-scaled envelope");
    instrument_adsr_note_off(&voice.env_stage);
    previous = level;
    for (i = 0; i < 32u; i++) {
        level = instrument_adsr_tick(&voice.gate, &voice.env_level, &voice.env_stage);
        failed += check(level <= previous, "release is monotonic");
        previous = level;
    }
    failed += check(voice.env_stage == INSTRUMENT_ENV_OFF && voice.gate == 0 && level == 0,
                    "release reaches quiet and frees the voice");
    if (failed) return 1;
    puts("ADSR stages and bounds OK");
    return 0;
}
