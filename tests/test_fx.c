/* SPDX-License-Identifier: GPL-3.0-only */
/* Unit vectors for the Barge FX manager and stateless clipper. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fx.c"

static int check(int condition, const char *name)
{
    if (!condition) fprintf(stderr, "FAIL: %s\n", name);
    return !condition;
}

int main(void)
{
    int failures = 0;
    int32_t samples[8] = {INT32_MIN, -16385, -16384, -1, 0, 16384, 16385, INT32_MAX};
    const int32_t original[8] = {INT32_MIN, -16385, -16384, -1, 0, 16384, 16385, INT32_MAX};
    int32_t block[FX_MAX_FRAMES * 2u];
    uint32_t i;

    fx_init(1, 16384u);
    fx_process(samples, 4u);
    failures += check(samples[0] == -16384 && samples[1] == -16384, "negative extremes clamp safely");
    failures += check(samples[2] == -16384 && samples[3] == -1, "negative boundary and interior preserved");
    failures += check(samples[4] == 0 && samples[5] == 16384, "zero and positive boundary preserved");
    failures += check(samples[6] == 16384 && samples[7] == 16384, "positive extremes clamp safely");

    memcpy(samples, original, sizeof samples);
    fx_set_enabled(0);
    fx_process(samples, 4u);
    failures += check(memcmp(samples, original, sizeof samples) == 0, "bypass is bit-exact");

    fx_set_threshold(4096u);
    fx_set_enabled(1);
    for (i = 0; i < FX_MAX_FRAMES * 2u; i++) block[i] = (i & 1u) ? INT32_MAX : INT32_MIN;
    fx_process(block, FX_MAX_FRAMES);
    failures += check(block[0] == -4096 && block[1] == 4096 && block[511] == 4096, "full block clamps both channels");

    block[0] = INT32_MIN;
    fx_process(block, FX_MAX_FRAMES + 1u);
    failures += check(block[0] == INT32_MIN, "oversized block is rejected without touching samples");
    if (failures) return 1;
    puts("FX clipper vectors OK");
    return 0;
}
