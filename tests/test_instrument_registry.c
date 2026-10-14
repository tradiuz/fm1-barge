/* SPDX-License-Identifier: GPL-3.0-only */
/* Direct host checks for fallback resolution and stable registry IDs. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/instrument.c"

static int expect(int condition, const char *message)
{
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return !condition;
}

int main(void)
{
    int failed = 0;
    instrument_init(INSTRUMENT_ID_RETIRED_SINE);
    failed += expect(instrument_active_id() == INSTRUMENT_ID_ANALOG,
                     "retired SINE settings recover to ANALOG");
    failed += expect(!instrument_has_id(INSTRUMENT_ID_RETIRED_SINE),
                     "retired SINE ID is not selectable");
    failed += expect(instrument_has_id(INSTRUMENT_ID_WAVETABLE),
                     "WAVE renderer remains available");
    failed += expect(instrument_fallback_id() == INSTRUMENT_ID_ANALOG,
                     "ANALOG is the normal recovery module");
    failed += expect(instrument_select(INSTRUMENT_ID_WAVETABLE),
                     "WAVE can be requested by stable ID");
    if (failed) return 1;
    puts("instrument registry fallback and IDs OK");
    return 0;
}
