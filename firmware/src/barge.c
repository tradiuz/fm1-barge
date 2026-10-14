/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
/* Barge's instrument host and minimal control/UI application.
 * This file owns the app behavior. The shared platform provides updates, USB rescue,
 * USB-MIDI, USB audio, keys, knobs, LEDs, the screen, a settings sector.
 *
 *   keys       play the selected instrument; the notes go out over USB-MIDI too
 *   OCT- OCT+  the octave (kept in flash)
 *   SEL        select the next compiled instrument
 *   MASTER     the volume (platform.c)
 *   USB-MIDI   notes play the selected instrument with their velocity
 *
 * The target has a single-precision FPU (build flags in tools/build.py); no hardware double or 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
static int app_module_sysex(const uint8_t *payload, uint32_t length);
static int ota_wire_send(const uint8_t *payload, uint32_t length);
#include "platform.c"
#include "instrument.c"
#include "fx.c"

static void app_audio(int32_t *out, uint32_t n)
{
    instrument_render(out, n);
    fx_process(out, n);
}

/* ---- notes (main loop) */
static uint32_t last_note = 255;
static uint32_t app_key_down;

static void app_note(uint32_t type, uint32_t channel, uint32_t note, uint32_t velocity)
{
    instrument_event_t event;
    event.type = (uint8_t)type;
    event.channel = (uint8_t)(channel & 15u);
    event.note = (uint8_t)note;
    event.velocity = (uint8_t)velocity;
    (void)instrument_send(&event);
    if (type == INSTRUMENT_EVENT_NOTE_ON && velocity)
        last_note = note;
}

/* ---- the screen: the name, the last note big, the keyboard with what sounds, the octave */
#define C_ACC RGB(180, 140, 255)                /* lilac, as the logo */
#define C_KEY RGB(210, 210, 210)
#define C_OFF RGB(90, 90, 90)
#define KEY0 53u                                /* key 0 = F3 */
static int black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }

static void draw_note(void)
{
    static const char *const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char s[8] = "-";
    if (last_note < 128u) {
        const char *p = NAME[last_note % 12u];
        uint32_t i = 0;
        while (*p) s[i++] = *p++;
        if (last_note >= 12u) s[i++] = (char)('0' + last_note / 12u - 1u);
        s[i] = 0;
    }
    draw_text_box(0, 82, 240, &FONT_L, s, C_WHITE, 1);
}

static void draw_keys(uint32_t lit)             /* bit k: key k sounds */
{
    uint32_t k, w = 0;
    cv_begin(240, 56, C_BLACK);
    for (k = 0; k < 27u; k++)                   /* 16 white keys of 14 px from x = 8 */
        if (!black(KEY0 + k)) {
            cv_rect(8 + (int32_t)w * 14, 0, 13, 56, lit >> k & 1u ? C_ACC : C_KEY);
            w++;
        }
    for (k = 0, w = 0; k < 27u; k++)            /* the black ones on the line between two white */
        if (black(KEY0 + k))
            cv_rect(8 + (int32_t)w * 14 - 5, 0, 9, 34, lit >> k & 1u ? C_ACC : C_BLACK);
        else
            w++;
    cv_blit(0, 140);
}

static void draw_status(int32_t oct)
{
    char s[12] = "OCTAVE +0";
    s[7] = oct < 0 ? '-' : '+';
    s[8] = (char)('0' + (oct < 0 ? -oct : oct));
    draw_text_box(0, 212, 240, &FONT_S, s, C_OFF, 1);
}

static struct {
    uint8_t schema;
    int8_t oct;
    uint16_t instrument;
    uint8_t controls[INSTRUMENT_MOD_CTL_COUNT];
    uint8_t adsr[4];
    uint8_t fx_enabled;
    uint16_t fx_threshold;
    uint8_t wave[INSTRUMENT_WAVETABLE_POINTS];
    uint8_t wave_loaded;
} app_settings;
static uint32_t app_instrument_drawn = 0xFFFFFFFFu, app_controls_drawn = 0xFFFFFFFFu;
static uint16_t app_controls_module_drawn = 0xFFFFu;
static uint8_t app_edit_page, app_env_edit;
static uint32_t app_prev, app_t_change, app_dirty, app_lit_drawn = ~0u, app_note_drawn = ~0u;
static int32_t app_oct_drawn = 99;
static uint8_t app_held[27];                    /* the note each key plays */

static void app_control(uint32_t parameter, int32_t value)
{
    instrument_event_t event;
    uint32_t event_parameter = parameter;
    if (parameter >= sizeof app_settings.controls)
        return;
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    app_settings.controls[parameter] = (uint8_t)value;
    if (app_settings.instrument == INSTRUMENT_ID_PHASE && parameter >= 8u && parameter < 14u)
        event_parameter = INSTRUMENT_MOD_CTL_RATE + parameter - 8u;
    event.type = INSTRUMENT_EVENT_CONTROL;
    event.channel = 0;
    event.note = (uint8_t)event_parameter;
    event.velocity = (uint8_t)value;
    (void)instrument_send(&event);
}

static void app_adsr_control(uint32_t parameter, int32_t value)
{
    instrument_event_t event;
    if (parameter >= 4u) return;
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    app_settings.adsr[parameter] = (uint8_t)value;
    event.type = INSTRUMENT_EVENT_ADSR;
    event.channel = 0;
    event.note = (uint8_t)parameter;
    event.velocity = (uint8_t)value;
    (void)instrument_send(&event);
}

static void app_control_defaults(uint16_t id, uint8_t *controls)
{
    static const uint8_t analog[INSTRUMENT_MOD_CTL_COUNT] = {
        0, 10, 64, 0, 90, 30, 0, 64, 0, 64, 64, 32, 0, 0, 0, 0, 0
    };
    static const uint8_t phase[INSTRUMENT_MOD_CTL_COUNT] = {
        0, 0, 60, 64, 0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0
    };
    static const uint8_t lofi[INSTRUMENT_MOD_CTL_COUNT] = {0, 64, 16, 110};
    static const uint8_t noise[INSTRUMENT_MOD_CTL_COUNT] = {0, 96, 24, 96};
    static const uint8_t wave[INSTRUMENT_MOD_CTL_COUNT] = {0};
    const uint8_t *defaults = analog;
    if (id == INSTRUMENT_ID_ANALOG) defaults = analog;
    else if (id == INSTRUMENT_ID_PHASE) defaults = phase;
    else if (id == INSTRUMENT_ID_LOFI) defaults = lofi;
    else if (id == INSTRUMENT_ID_NOISE) defaults = noise;
    else if (id == INSTRUMENT_ID_WAVETABLE) defaults = wave;
    memcpy(controls, defaults, INSTRUMENT_MOD_CTL_COUNT);
}

static uint32_t app_module_parameter_count(uint16_t id)
{
    if (id == INSTRUMENT_ID_ANALOG) return INSTRUMENT_MOD_CTL_COUNT;
    if (id == INSTRUMENT_ID_PHASE) return 14u;
    if (id == INSTRUMENT_ID_LOFI || id == INSTRUMENT_ID_NOISE) return 4u;
    return 0u;
}

static uint32_t app_module_page_count(uint16_t id)
{
    uint32_t n = app_module_parameter_count(id);
    return (n + 3u) / 4u;
}

static void app_text(char **dst, const char *text)
{
    while (*text)
        *(*dst)++ = *text++;
    **dst = 0;
}

static void app_number(char **dst, uint32_t value)
{
    char digits[6];
    uint32_t n = 0;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && n < sizeof digits);
    while (n)
        *(*dst)++ = digits[--n];
    **dst = 0;
}

static void app_number2(char **dst, uint32_t value)
{
    *(*dst)++ = (char)('0' + (value / 10u) % 10u);
    *(*dst)++ = (char)('0' + value % 10u);
    **dst = 0;
}

/* User modules are data interpreted by a linked Barge renderer, never uploaded native code.
 * Request body (between F0/F7): 7D 42 4D 01, 32 samples (0..127), checksum(sum & 7F). */
static int app_module_sysex(const uint8_t *payload, uint32_t length)
{
    uint8_t reply[7] = {0x7D, 0x42, 0x4D, 0x01, 0, 1, 0};
    uint8_t wire[9] = {0xF0, 0, 0, 0, 0, 0, 0, 0, 0xF7};
    uint32_t i, sum = 0;
    if (!payload || length < 3u || payload[0] != 0x7Du || payload[1] != 0x42u || payload[2] != 0x4Du)
        return 0;
    if (payload[3] == 1u && length == 37u) {
        reply[3] = 1u;
        reply[4] = 0u;
        for (i = 0; i < 36u; i++)
            sum += payload[i];
        if (((uint8_t)sum & 0x7Fu) != payload[36])
            reply[4] = 2u;                       /* invalid checksum */
        else if (!instrument_wavetable_compiled())
            reply[4] = 3u;                       /* this build excludes the renderer */
        else if (!instrument_wave_upload(payload + 4, INSTRUMENT_WAVETABLE_POINTS))
            reply[4] = 4u;                       /* busy or invalid sample values */
        else {
            memcpy(app_settings.wave, payload + 4, INSTRUMENT_WAVETABLE_POINTS);
            app_settings.wave_loaded = 1u;
            app_settings.instrument = INSTRUMENT_ID_WAVETABLE;
            app_dirty = 1;
            app_t_change = fm1_ms;
            (void)instrument_select(INSTRUMENT_ID_WAVETABLE);
        }
        for (i = 0; i < 5u; i++) reply[5] = (uint8_t)((reply[5] + reply[i]) & 0x7Fu);
        memcpy(wire + 1, reply, 6u);
        wire[7] = 0xF7;
        (void)ota_wire_send(wire, 8u);
        return 1;
    }
    if (payload[3] == 2u && length >= 11u) {
        uint32_t module = payload[4], count = payload[5], expected;
        uint16_t id = module == 1u ? INSTRUMENT_ID_ANALOG : module == 2u ? INSTRUMENT_ID_PHASE : 0u;
        expected = module == 1u ? 11u : module == 2u ? 8u : 0u;
        reply[3] = 2u;
        reply[4] = (uint8_t)module;
        reply[5] = 1u;
        if (!id || (count != expected && count != (module == 1u ? 17u : 14u))
            || length != 11u + count) {
            reply[5] = 1u;                       /* unknown module, malformed length */
        } else {
            sum = 0;
            for (i = 0; i + 1u < length; i++) sum += payload[i];
            if (((uint8_t)sum & 0x7Fu) != payload[length - 1u]) {
                reply[5] = 2u;                   /* checksum */
            } else if (!instrument_has_id(id)) {
                reply[5] = 3u;                   /* module not in this firmware */
            } else {
                uint32_t valid = 1u;
                for (i = 6u; i < length - 1u; i++)
                    if (payload[i] > 127u) valid = 0u;
                if (!valid) {
                    reply[5] = 4u;               /* invalid parameter data */
                } else {
                    app_settings.instrument = id;
                    app_control_defaults(id, app_settings.controls);
                    for (i = 0; i < 4u; i++) app_settings.adsr[i] = payload[6u + i];
                    for (i = 0; i < count; i++) app_settings.controls[i] = payload[10u + i];
                    app_edit_page = app_env_edit = 0;
                    (void)instrument_select(id);
                    for (i = 0; i < 4u; i++) app_adsr_control(i, app_settings.adsr[i]);
                    for (i = 0; i < count; i++) app_control(i, app_settings.controls[i]);
                    app_dirty = 1;
                    app_t_change = fm1_ms;
                    reply[5] = 0u;
                }
            }
        }
        for (i = 0; i < 6u; i++) reply[6] = (uint8_t)((reply[6] + reply[i]) & 0x7Fu);
        memcpy(wire + 1, reply, 7u);
        wire[8] = 0xF7;
        (void)ota_wire_send(wire, sizeof wire);
        return 1;
    }
    reply[5] = 1u;
    for (i = 0; i < 6u; i++) reply[6] = (uint8_t)((reply[6] + reply[i]) & 0x7Fu);
    memcpy(wire + 1, reply, 7u);
    wire[8] = 0xF7;
    (void)ota_wire_send(wire, sizeof wire);
    return 1;
}

static void app_start(void)
{
    struct { int8_t oct; } legacy_settings;
    struct {
        uint8_t schema; int8_t oct; uint16_t instrument; uint8_t controls[12]; uint8_t adsr[4];
        uint8_t fx_enabled; uint16_t fx_threshold; uint8_t wave[INSTRUMENT_WAVETABLE_POINTS]; uint8_t wave_loaded;
    } old_b6;
    struct { uint8_t schema; int8_t oct; uint16_t instrument; uint8_t controls[4]; } old_b2;
    struct { uint8_t schema; int8_t oct; uint16_t instrument; uint8_t controls[4]; uint8_t fx_enabled; uint16_t fx_threshold; } old_b3;
    struct {
        uint8_t schema; int8_t oct; uint16_t instrument; uint8_t controls[4];
        uint8_t fx_enabled; uint16_t fx_threshold; uint8_t wave[INSTRUMENT_WAVETABLE_POINTS]; uint8_t wave_loaded;
    } old_b4;
    struct {
        uint8_t schema; int8_t oct; uint16_t instrument; uint8_t controls[4];
        uint8_t fx_enabled; uint16_t fx_threshold; uint8_t wave[INSTRUMENT_WAVETABLE_POINTS]; uint8_t wave_loaded;
    } old_b5;
    int loaded = 0;
    app_settings.schema = 0xB7u;
    app_settings.oct = 0;
    app_settings.instrument = instrument_fallback_id();
    app_control_defaults(app_settings.instrument, app_settings.controls);
    app_settings.adsr[0] = 4;
    app_settings.adsr[1] = 32;
    app_settings.adsr[2] = 96;
    app_settings.adsr[3] = 32;
    app_settings.fx_enabled = 0;
    app_settings.fx_threshold = 16384u;
    memcpy(app_settings.wave, INSTRUMENT_WAVE_DEFAULT, sizeof app_settings.wave);
    app_settings.wave_loaded = 0;
    app_prev = app_t_change = app_dirty = 0;
    app_lit_drawn = app_note_drawn = ~0u;
    app_oct_drawn = 99;
    app_instrument_drawn = 0xFFFFFFFFu;
    app_controls_drawn = 0xFFFFFFFFu;
    app_controls_module_drawn = 0xFFFFu;
    app_key_down = 0;
    memset(app_held, 0, sizeof app_held);
    if (!settings_load(&app_settings, sizeof app_settings) && app_settings.schema == 0xB7u) {
        loaded = 1;
    } else if (!settings_load(&old_b6, sizeof old_b6) && old_b6.schema == 0xB6u) {
        app_settings.schema = 0xB7u;
        app_settings.oct = old_b6.oct;
        app_settings.instrument = old_b6.instrument;
        app_control_defaults(app_settings.instrument, app_settings.controls);
        memcpy(app_settings.controls, old_b6.controls, sizeof old_b6.controls);
        memcpy(app_settings.adsr, old_b6.adsr, sizeof app_settings.adsr);
        app_settings.fx_enabled = old_b6.fx_enabled;
        app_settings.fx_threshold = old_b6.fx_threshold;
        memcpy(app_settings.wave, old_b6.wave, sizeof app_settings.wave);
        app_settings.wave_loaded = old_b6.wave_loaded;
        loaded = 1;
        app_dirty = 1;
    } else if (!settings_load(&old_b5, sizeof old_b5) && old_b5.schema == 0xB5u) {
        app_settings.schema = 0xB7u;
        app_settings.oct = old_b5.oct;
        app_settings.instrument = old_b5.instrument;
        app_control_defaults(app_settings.instrument, app_settings.controls);
        memcpy(app_settings.controls, old_b5.controls, sizeof old_b5.controls);
        app_settings.fx_enabled = old_b5.fx_enabled;
        app_settings.fx_threshold = old_b5.fx_threshold;
        memcpy(app_settings.wave, old_b5.wave, sizeof app_settings.wave);
        app_settings.wave_loaded = old_b5.wave_loaded;
        loaded = 1;
        app_dirty = 1;
    } else if (!settings_load(&old_b4, sizeof old_b4) && old_b4.schema == 0xB4u) {
        app_settings.schema = 0xB7u;
        app_settings.oct = old_b4.oct;
        app_settings.instrument = old_b4.instrument;
        app_control_defaults(app_settings.instrument, app_settings.controls);
        memcpy(app_settings.controls, old_b4.controls, sizeof old_b4.controls);
        app_settings.fx_enabled = old_b4.fx_enabled;
        app_settings.fx_threshold = old_b4.fx_threshold;
        memcpy(app_settings.wave, old_b4.wave, sizeof app_settings.wave);
        app_settings.wave_loaded = old_b4.wave_loaded;
        loaded = 1;
        app_dirty = 1;
    } else if (!settings_load(&old_b3, sizeof old_b3) && old_b3.schema == 0xB3u) {
        app_settings.schema = 0xB7u;
        app_settings.oct = old_b3.oct;
        app_settings.instrument = old_b3.instrument;
        app_control_defaults(app_settings.instrument, app_settings.controls);
        memcpy(app_settings.controls, old_b3.controls, sizeof old_b3.controls);
        app_settings.fx_enabled = old_b3.fx_enabled;
        app_settings.fx_threshold = old_b3.fx_threshold;
        app_settings.wave_loaded = 0;
        loaded = 1;
        app_dirty = 1;
    } else if (!settings_load(&old_b2, sizeof old_b2) && old_b2.schema == 0xB2u) {
        app_settings.schema = 0xB7u;
        app_settings.oct = old_b2.oct;
        app_settings.instrument = old_b2.instrument;
        app_control_defaults(app_settings.instrument, app_settings.controls);
        memcpy(app_settings.controls, old_b2.controls, sizeof old_b2.controls);
        loaded = 1;
        app_dirty = 1;
    }
    if (!loaded) {
        legacy_settings.oct = 0;
        if (!settings_load(&legacy_settings, sizeof legacy_settings)) app_settings.oct = legacy_settings.oct;
        app_settings.schema = 0xB7u;
        app_settings.instrument = instrument_fallback_id();
        app_control_defaults(app_settings.instrument, app_settings.controls);
        app_settings.fx_enabled = 0;
        app_settings.fx_threshold = 16384u;
        app_settings.wave_loaded = 0;
    }
    if (app_settings.oct < -2 || app_settings.oct > 2) app_settings.oct = 0;
    if (app_settings.fx_enabled > 1u) app_settings.fx_enabled = 0;
    if (app_settings.fx_threshold < 1024u || app_settings.fx_threshold > 32767u)
        app_settings.fx_threshold = 16384u;
    if (!fx_compiled()) app_settings.fx_enabled = 0;
    instrument_init(app_settings.instrument);
    if (instrument_active_id() != app_settings.instrument) {
        app_settings.instrument = instrument_active_id();
        app_control_defaults(app_settings.instrument, app_settings.controls);
        app_dirty = 1;
        app_t_change = fm1_ms;
    }
    if (app_settings.wave_loaded && instrument_wavetable_compiled())
        (void)instrument_wave_upload(app_settings.wave, sizeof app_settings.wave);
    fx_init(app_settings.fx_enabled, app_settings.fx_threshold);
    for (uint32_t i = 0; i < 4u; i++)
        app_adsr_control(i, app_settings.adsr[i]);
    for (uint32_t i = 0; i < sizeof app_settings.controls; i++)
        app_control(i, app_settings.controls[i]);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
}

/* One bounded application pass. Hardware and the browser simulator each supply their own clock/poll loop. */
static void app_step(void)
{
        uint32_t keys, down, up, rel, b, k, lit = 0, base = (uint32_t)(KEY0 + 12 * app_settings.oct);
        uint8_t st, d1, d2;

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~app_prev;
        up = app_prev & ~keys;
        app_prev = keys;
        for (k = 0; k < 27u; k++) {                 /* (a key stops the note it started: the octave may change meanwhile) */
            if (down >> k & 1u) {
                app_held[k] = (uint8_t)(base + k);
                app_key_down |= 1u << k;
                app_note(INSTRUMENT_EVENT_NOTE_ON, 0, app_held[k], 100);
                midi_send(0x90, app_held[k], 100);
            }
            if (up >> k & 1u) {
                app_key_down &= ~(1u << k);
                app_note(INSTRUMENT_EVENT_NOTE_OFF, 0, app_held[k], 0);
                midi_send(0x80, app_held[k], 0);
            }
        }

        b = fm1_input_edges(&rel);              /* buttons pressed since the last call (bit = button id) */
        if (b & (1u << BTN_OCT_DOWN) && app_settings.oct > -2) { app_settings.oct--; app_dirty = 1; app_t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && app_settings.oct < 2) { app_settings.oct++; app_dirty = 1; app_t_change = fm1_ms; }
        if (b & (1u << BTN_SEL)) {
            uint16_t next = instrument_next_id(app_settings.instrument);
            if (next != app_settings.instrument && instrument_select(next)) {
                app_settings.instrument = next;
                app_control_defaults(next, app_settings.controls);
                app_edit_page = 0;
                app_env_edit = 0;
                for (uint32_t i = 0; i < sizeof app_settings.controls; i++)
                    app_control(i, app_settings.controls[i]);
                app_dirty = 1;
                app_t_change = fm1_ms;
            }
        }
        if (b & (1u << BTN_FX)) {
            if (fx_compiled()) {
                app_settings.fx_enabled = (uint8_t)!app_settings.fx_enabled;
                fx_set_enabled(app_settings.fx_enabled);
                app_dirty = 1;
                app_t_change = fm1_ms;
            }
        }

        if (b & (1u << BTN_ENV)) {
            app_env_edit = (uint8_t)!app_env_edit;
            app_edit_page = 0;
        }
        if ((b & (1u << BTN_EDIT)) && !app_env_edit) {
            uint32_t pages = app_module_page_count(instrument_active_id());
            if (pages > 1u) app_edit_page = (uint8_t)((app_edit_page + 1u) % pages);
        }

        if (app_env_edit) {
            for (k = 0; k < 4u; k++) {
                int32_t delta = fm1_enc_take(ENC_KNOB1 + k);
                if (delta) {
                    app_adsr_control(k, (int32_t)app_settings.adsr[k] + delta);
                    app_dirty = 1;
                    app_t_change = fm1_ms;
                }
            }
        } else if (app_module_parameter_count(instrument_active_id())) {
            uint32_t start = (uint32_t)app_edit_page * 4u;
            uint32_t ncontrols = app_module_parameter_count(instrument_active_id()) - start;
            uint32_t available = app_settings.fx_enabled && !app_env_edit ? 3u : 4u;
            if (ncontrols > available) ncontrols = available;
            for (k = 0; k < ncontrols; k++) {
                int32_t delta = fm1_enc_take(ENC_KNOB1 + k);
                if (delta) {
                    int32_t next = (int32_t)app_settings.controls[start + k] + delta;
                    app_control(start + k, next);
                    app_dirty = 1;
                    app_t_change = fm1_ms;
                }
            }
        }
        if (app_settings.fx_enabled && fx_compiled() && !app_env_edit) {
            int32_t delta = fm1_enc_take(ENC_KNOB4);
            if (delta) {
                int32_t threshold = (int32_t)app_settings.fx_threshold + delta * 128;
                if (threshold < 1024) threshold = 1024;
                if (threshold > 32767) threshold = 32767;
                app_settings.fx_threshold = (uint16_t)threshold;
                fx_set_threshold(app_settings.fx_threshold);
                app_dirty = 1;
                app_t_change = fm1_ms;
            }
        }

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI in, any channel */
            uint32_t channel = st & 15u;
            if ((st & 0xF0u) == 0x90u && d2)
                app_note(INSTRUMENT_EVENT_NOTE_ON, channel, d1, d2);
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
                app_note(INSTRUMENT_EVENT_NOTE_OFF, channel, d1, 0);
        }

        lit = app_key_down;
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, app_settings.oct < 0);
        fm1_led_key(BTN_OCT_UP, app_settings.oct > 0);

        if (lit != app_lit_drawn) { app_lit_drawn = lit; draw_keys(lit); }   /* redraw only what changed */
        if (last_note != app_note_drawn) { app_note_drawn = last_note; draw_note(); }
        if (app_settings.oct != app_oct_drawn) { app_oct_drawn = app_settings.oct; draw_status(app_settings.oct); }
        if (instrument_active_id() != app_instrument_drawn) {
            char title[40];
            char *p = title;
            app_instrument_drawn = instrument_active_id();
            app_text(&p, instrument_name(instrument_active_id()));
            app_text(&p, "  v" APP_VER);
            draw_text_box(0, 52, 240, &FONT_S, title, C_OFF, 1);
        }
        {
            uint32_t controls_key = 2166136261u ^ app_edit_page ^ ((uint32_t)app_env_edit << 8);
            for (k = 0; k < sizeof app_settings.controls; k++)
                controls_key = (controls_key ^ app_settings.controls[k]) * 16777619u;
            for (k = 0; k < 4u; k++)
                controls_key = (controls_key ^ app_settings.adsr[k]) * 16777619u;
            uint16_t controls_module = instrument_active_id();
            if (app_settings.fx_enabled && fx_compiled() && !app_env_edit)
                controls_key ^= ((uint32_t)app_settings.fx_threshold << 3) ^ 0x80000000u;
            if (controls_key != app_controls_drawn || controls_module != app_controls_module_drawn) {
                char controls[36] = "";
                char *p = controls;
                app_controls_drawn = controls_key;
                app_controls_module_drawn = controls_module;
                if (app_settings.fx_enabled && fx_compiled() && !app_env_edit) {
                    app_text(&p, "FX:CLIP ON T:");
                    app_number(&p, app_settings.fx_threshold);
                    app_text(&p, " K4");
                } else if (app_env_edit) {
                    static const char *const labels[4] = {"A:", "D:", "S:", "R:"};
                    app_text(&p, "ADSR ");
                    for (k = 0; k < 4u; k++) {
                        if (k) app_text(&p, " ");
                        app_text(&p, labels[k]); app_number2(&p, app_settings.adsr[k]);
                    }
                } else if (controls_module == INSTRUMENT_ID_ANALOG) {
                    static const char *const waves[] = {"SAW", "SQR", "TRI", "SIN", "PWM", "SYNC", "SUB"};
                    static const char *const mod_waves[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
                    if (app_edit_page == 0u) {
                        app_text(&p, "W:"); app_text(&p, waves[app_settings.controls[0] % 7u]);
                        app_text(&p, " D:"); app_number2(&p, app_settings.controls[1]);
                        app_text(&p, " M:"); app_number2(&p, app_settings.controls[2]);
                        app_text(&p, " N:"); app_number2(&p, app_settings.controls[3]);
                    } else if (app_edit_page == 1u) {
                        app_text(&p, "C:"); app_number2(&p, app_settings.controls[4]);
                        app_text(&p, " R:"); app_number2(&p, app_settings.controls[5]);
                        app_text(&p, " V:"); app_number2(&p, app_settings.controls[6]);
                        app_text(&p, " K:"); app_number2(&p, app_settings.controls[7]);
                    } else if (app_edit_page == 2u) {
                        app_text(&p, "TYPE:"); app_number(&p, app_settings.controls[8] % 3u);
                        app_text(&p, " SHP:"); app_number2(&p, app_settings.controls[9]);
                        app_text(&p, " ENV:"); app_number2(&p, app_settings.controls[10]);
                    } else if (app_edit_page == 3u) {
                        static const char *const routes[] = {"CUT", "SHP", "PWM"};
                        app_text(&p, "LFO R:"); app_number2(&p, app_settings.controls[11]);
                        app_text(&p, " W:"); app_text(&p, mod_waves[app_settings.controls[12] % 5u]);
                        app_text(&p, " D:"); app_number2(&p, app_settings.controls[13]);
                        app_text(&p, " "); app_text(&p, routes[app_settings.controls[14] % 3u]);
                    } else {
                        app_text(&p, app_settings.controls[15] ? "LFO MODE:RETRIG" : "LFO MODE:FREE");
                        app_text(&p, app_settings.controls[16] ? " SRC:ENV" : " SRC:LFO");
                    }
                } else if (controls_module == INSTRUMENT_ID_PHASE) {
                    static const char *const waves[] = {"SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};
                    static const char *const mod_waves[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
                    if (app_edit_page == 0u) {
                        app_text(&p, "W:"); app_text(&p, waves[app_settings.controls[0] % 8u]);
                        app_text(&p, " W2:"); app_number2(&p, app_settings.controls[1]);
                        app_text(&p, " DCW:"); app_number2(&p, app_settings.controls[2]);
                        app_text(&p, " ENV:"); app_number2(&p, app_settings.controls[3]);
                    } else if (app_edit_page == 1u) {
                        app_text(&p, "DTN:"); app_number2(&p, app_settings.controls[4]);
                        app_text(&p, " LN:"); app_number(&p, app_settings.controls[5]);
                        app_text(&p, " SUB:"); app_number2(&p, app_settings.controls[6]);
                        app_text(&p, " F:"); app_number2(&p, app_settings.controls[7]);
                    } else if (app_edit_page == 2u) {
                        static const char *const routes[] = {"DCW", "PIT", "SUB", "LINE"};
                        app_text(&p, "LFO R:"); app_number2(&p, app_settings.controls[8]);
                        app_text(&p, " W:"); app_text(&p, mod_waves[app_settings.controls[9] % 5u]);
                        app_text(&p, " D:"); app_number2(&p, app_settings.controls[10]);
                        app_text(&p, " "); app_text(&p, routes[app_settings.controls[11] % 4u]);
                    } else {
                        app_text(&p, app_settings.controls[12] ? "LFO MODE:RETRIG" : "LFO MODE:FREE");
                        app_text(&p, app_settings.controls[13] ? " SRC:ENV" : " SRC:LFO");
                    }
                } else if (controls_module == INSTRUMENT_ID_LOFI) {
                    static const char *const waves[] = {"PLS", "TRI", "SAW", "NOI"};
                    app_text(&p, "W:"); app_text(&p, waves[app_settings.controls[0] & 3u]);
                    app_text(&p, " D:"); app_number2(&p, app_settings.controls[1]);
                    app_text(&p, " C:"); app_number2(&p, app_settings.controls[2]);
                    app_text(&p, " T:"); app_number2(&p, app_settings.controls[3]);
                } else if (controls_module == INSTRUMENT_ID_NOISE) {
                    app_text(&p, "M:"); app_text(&p, app_settings.controls[0] ? "DUST" : "ANLG");
                    app_text(&p, " T:"); app_number2(&p, app_settings.controls[1]);
                    app_text(&p, " D:"); app_number2(&p, app_settings.controls[2]);
                    app_text(&p, " L:"); app_number2(&p, app_settings.controls[3]);
                }
                draw_text_box(0, 190, 240, &FONT_S, controls, C_OFF, 1);
            }
        }

        if (app_dirty && instrument_is_quiet() && fm1_ms - app_t_change > 2000u) {
            settings_save(&app_settings, sizeof app_settings);
            app_dirty = 0;
        }
}

static void app_main(void)
{
    app_start();
    for (;;)
        { platform_poll(); app_step(); }
}
