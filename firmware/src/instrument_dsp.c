/* SPDX-License-Identifier: GPL-3.0-only */
/* Small Barge-owned DSP primitives and shared read-only pitch table.
 * This file is included once by instrument.c before module adapters. */
static const uint32_t INSTRUMENT_INC_C9[12] = {
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u
};
static const uint8_t INSTRUMENT_WAVE_DEFAULT[32] = {
    64, 76, 88, 99, 108, 116, 122, 126, 127, 126, 122, 116, 108, 99, 88, 76,
    64, 52, 40, 29, 20, 12, 6, 2, 1, 2, 6, 12, 20, 29, 40, 52
};
static const uint16_t INSTRUMENT_SVF_G[128] = {
    8, 9, 9, 10, 10, 11, 11, 12, 13, 13, 14, 15, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 27, 28, 30, 31, 33, 34, 36, 38, 40,
    42, 44, 47, 49, 51, 54, 57, 60, 63, 66, 69, 73, 77, 81, 85, 89,
    93, 98, 103, 108, 114, 120, 126, 132, 139, 146, 154, 161, 170, 178, 187, 197,
    207, 217, 228, 240, 252, 265, 279, 293, 308, 324, 340, 357, 376, 395, 415, 436,
    458, 482, 507, 533, 560, 589, 619, 651, 685, 720, 757, 797, 838, 882, 928, 977,
    1029, 1083, 1141, 1202, 1266, 1335, 1408, 1485, 1567, 1655, 1748, 1848, 1955, 2070, 2194, 2327,
    2471, 2628, 2798, 2984, 3189, 3416, 3668, 3951, 4271, 4636, 5058, 5553, 6142, 6859, 7754, 8907
};

static uint32_t instrument_pitch_inc(uint32_t note)
{
    if (note > 127u)
        return 0;
    return INSTRUMENT_INC_C9[note % 12u] >> (10u - note / 12u);
}

static uint32_t instrument_pitch_inc16(int32_t pitch16)
{
    uint32_t increment, fraction;
    if (pitch16 < 0) pitch16 = 0;
    if (pitch16 > 2047) pitch16 = 2047;
    increment = instrument_pitch_inc((uint32_t)pitch16 >> 4);
    fraction = (uint32_t)pitch16 & 15u;
    return increment + (increment >> 12) * fraction * 15u;
}

static uint32_t instrument_pitch_inc_cents(uint8_t note, uint8_t cents)
{
    int32_t d16 = (int32_t)cents * 16 / 100;
    int32_t rem = (int32_t)cents * 16 - d16 * 100;
    uint32_t increment = instrument_pitch_inc16((int32_t)note * 16 + d16);
    return increment + (uint32_t)((int32_t)(increment >> 12) * (rem * 2367 / 16000));
}

/* Original Barge/Dinghy refined-parabola sine approximation. */
static int32_t instrument_sine_q15(uint32_t phase)
{
    int32_t x = (int32_t)phase >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

/* Split multiply for bounded intermediate values larger than Q15 audio samples. */
static int32_t instrument_mul_q15_split(int32_t value, int32_t gain)
{
    int32_t high = value >> 8;
    int32_t low = value - high * 256;
    return ((high * gain) >> 7) + ((low * gain) >> 15);
}

static int32_t instrument_clamp_i32(int32_t value, int32_t low, int32_t high)
{
    return value < low ? low : value > high ? high : value;
}

static uint32_t instrument_xorshift32(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value ? value : 1u;
    return *state;
}

static uint32_t instrument_voice_score(uint8_t voice_note, int32_t gate, uint32_t age,
                                       int32_t amp, uint32_t requested_note)
{
    if (voice_note == requested_note && gate)
        return 0u;
    return gate ? 0x80000000u + age : (uint32_t)amp + 1u;
}

enum { INSTRUMENT_ENV_OFF, INSTRUMENT_ENV_ATTACK, INSTRUMENT_ENV_DECAY,
       INSTRUMENT_ENV_SUSTAIN, INSTRUMENT_ENV_RELEASE };
static uint8_t instrument_adsr[4] = {4, 32, 96, 32}; /* A D S R, values 0..127 */
static int32_t instrument_adsr_attack_step, instrument_adsr_decay_step;
static int32_t instrument_adsr_release_step, instrument_adsr_sustain;

static uint32_t instrument_adsr_samples(uint8_t value)
{
    return 32u + (uint32_t)value * value * 2u;
}

static void instrument_adsr_set(uint32_t index, uint8_t value)
{
    if (index < 4u)
        instrument_adsr[index] = value & 127u;
}

static void instrument_adsr_prepare(void)
{
    uint32_t a = instrument_adsr_samples(instrument_adsr[0]);
    uint32_t d = instrument_adsr_samples(instrument_adsr[1]);
    uint32_t r = instrument_adsr_samples(instrument_adsr[3]);
    instrument_adsr_attack_step = (32767 + (int32_t)a - 1) / (int32_t)a;
    instrument_adsr_sustain = (int32_t)instrument_adsr[2] * 258;
    instrument_adsr_decay_step = instrument_adsr_sustain < 32767
        ? (32767 - instrument_adsr_sustain + (int32_t)d - 1) / (int32_t)d : 1;
    instrument_adsr_release_step = (32767 + (int32_t)r - 1) / (int32_t)r;
}

static void instrument_adsr_note_on(int32_t *peak, int32_t *amp, uint16_t *level,
                                    uint8_t *stage, uint32_t velocity, uint32_t scale)
{
    *peak = (int32_t)(velocity * scale);
    *amp = 0;
    *level = 0;
    *stage = INSTRUMENT_ENV_ATTACK;
}

static void instrument_adsr_note_off(uint8_t *stage)
{
    if (*stage != INSTRUMENT_ENV_OFF)
        *stage = INSTRUMENT_ENV_RELEASE;
}

static int32_t instrument_adsr_tick(int32_t *peak, uint16_t *level, uint8_t *stage)
{
    int32_t value = *level;
    switch (*stage) {
    case INSTRUMENT_ENV_ATTACK:
        value += instrument_adsr_attack_step;
        if (value >= 32767) { value = 32767; *stage = INSTRUMENT_ENV_DECAY; }
        break;
    case INSTRUMENT_ENV_DECAY:
        value -= instrument_adsr_decay_step;
        if (value <= instrument_adsr_sustain) { value = instrument_adsr_sustain; *stage = INSTRUMENT_ENV_SUSTAIN; }
        break;
    case INSTRUMENT_ENV_SUSTAIN:
        value = instrument_adsr_sustain;
        break;
    case INSTRUMENT_ENV_RELEASE:
        value -= instrument_adsr_release_step;
        if (value <= 0) { value = 0; *stage = INSTRUMENT_ENV_OFF; *peak = 0; }
        break;
    default:
        value = 0;
        *peak = 0;
        *stage = INSTRUMENT_ENV_OFF;
        break;
    }
    *level = (uint16_t)value;
    return (*peak * value) >> 15;
}

typedef struct {
    uint32_t phase[3], increment[2], age, rng;
    int32_t gate, amp, filter[2];
    uint16_t env_level;
    uint8_t env_stage;
    uint8_t flags;
    uint8_t note;
    int32_t mod[3];
} instrument_osc_voice_t;

static int32_t instrument_osc_note_event(instrument_osc_voice_t *voices, uint32_t count,
                                        uint32_t *ages, const instrument_event_t *event,
                                        uint32_t velocity_scale)
{
    uint32_t i, pick = 0, best = 0xFFFFFFFFu;
    if (event->type == INSTRUMENT_EVENT_ALL_NOTES_OFF) {
        for (i = 0; i < count; i++) instrument_adsr_note_off(&voices[i].env_stage);
        return -1;
    }
    if (event->type == INSTRUMENT_EVENT_NOTE_OFF) {
        for (i = 0; i < count; i++)
            if (voices[i].note == event->note) instrument_adsr_note_off(&voices[i].env_stage);
        return -1;
    }
    if (event->type != INSTRUMENT_EVENT_NOTE_ON || !event->velocity || event->note > 127u)
        return -1;
    for (i = 0; i < count; i++) {
        int32_t scoring_gate = voices[i].env_stage == INSTRUMENT_ENV_RELEASE ? 0 : voices[i].gate;
        uint32_t score = instrument_voice_score(voices[i].note, scoring_gate, voices[i].age,
                                                voices[i].amp, event->note);
        if (score < best) { best = score; pick = i; }
    }
    voices[pick].note = event->note;
    voices[pick].age = ++*ages;
    voices[pick].rng = ((uint32_t)event->note + 1u) * 0x9E3779B9u ^ (*ages * 0x85EBCA6Bu);
    if (!voices[pick].rng) voices[pick].rng = 1u;
    voices[pick].increment[0] = instrument_pitch_inc(event->note);
    voices[pick].increment[1] = voices[pick].increment[0];
    instrument_adsr_note_on(&voices[pick].gate, &voices[pick].amp, &voices[pick].env_level,
                            &voices[pick].env_stage, event->velocity, velocity_scale);
    return (int32_t)pick;
}

static int32_t instrument_osc_amp_tick(instrument_osc_voice_t *voice)
{
    return instrument_adsr_tick(&voice->gate, &voice->env_level, &voice->env_stage);
}

static int32_t instrument_wave32_q15(const uint8_t *wave, uint32_t phase)
{
    uint32_t index = phase >> 27;
    uint32_t next = (index + 1u) & 31u;
    uint32_t frac = (phase >> 12) & 0x7FFFu;
    int32_t a = ((int32_t)wave[index] - 64) * 512;
    int32_t b = ((int32_t)wave[next] - 64) * 512;
    return a + (((b - a) * (int32_t)frac) >> 15);
}

typedef struct { int32_t a1, a2, a3, k; } instrument_tsvf_t;

static void instrument_tsvf_prepare(instrument_tsvf_t *filter, uint32_t cutoff, uint32_t resonance)
{
    int32_t g, den;
    if (cutoff > 127u) cutoff = 127u;
    if (resonance > 127u) resonance = 127u;
    g = INSTRUMENT_SVF_G[cutoff];
    filter->k = 8192 - (int32_t)resonance * 7600 / 127;
    den = 4096 + ((g * (g + filter->k)) >> 12);
    filter->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    filter->a2 = (filter->a1 * g) >> 12;
    filter->a3 = (filter->a2 * g) >> 12;
}

static int32_t instrument_tsvf_process(const instrument_tsvf_t *filter, int32_t input,
                                       int32_t *ic1, int32_t *ic2, uint32_t type)
{
    int32_t v3 = input - *ic2;
    int32_t v1 = (filter->a1 * *ic1 + filter->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((filter->a2 * *ic1 + filter->a3 * v3) >> 13);
    *ic1 = instrument_clamp_i32(2 * v1 - *ic1, -150000, 150000);
    *ic2 = instrument_clamp_i32(2 * v2 - *ic2, -150000, 150000);
    if (type == 1u) return v1;
    if (type == 2u) return input - ((filter->k * v1) >> 12) - v2;
    return v2;
}

static int32_t instrument_soft_knee(int32_t value, int32_t knee)
{
    int32_t magnitude = value < 0 ? -value : value;
    if (magnitude <= knee) return value;
    magnitude = knee + ((magnitude - knee) * knee) / (magnitude + knee);
    return value < 0 ? -magnitude : magnitude;
}
