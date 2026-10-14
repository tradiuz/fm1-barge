/* SPDX-License-Identifier: GPL-3.0-only */
/* Stateless, zero-latency Barge insert: clamp Q15-scale stereo samples. */
static void fx_clipper_process(void *state, int32_t *stereo, uint32_t frames, uint16_t threshold)
{
    uint32_t i;
    int32_t limit = threshold;
    (void)state;
    for (i = 0; i < frames * 2u; i++) {
        if (stereo[i] > limit) stereo[i] = limit;
        else if (stereo[i] < -limit) stereo[i] = -limit;
    }
}

static const fx_desc_t FX_CLIPPER = {
    FX_ID_CLIPPER, FX_ABI_VERSION, "CLIPPER", 0u, 1u, fx_clipper_process
};
