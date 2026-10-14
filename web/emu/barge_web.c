/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Barge contributors
 *
 * Browser adapter for Barge's unchanged application callbacks. Its clock is advanced by audio demand, as in
 * Felucca's web/emu/felucca_web.c (GPL-3.0-only), following the AudioWorklet/WebAssembly model from X0X.
 * This simulates the app, screen, panel, settings and audio only; it is not a full AC79 emulator. */
#define BARGE_WEB 1
#include "../../firmware/src/barge.c"

#define EXPORT __attribute__((used, visibility("default")))
#define WEB_RING 8192u
#define WEB_MAX_RENDER 1024u
static float web_ring_l[WEB_RING], web_ring_r[WEB_RING];
static float web_out_left[WEB_MAX_RENDER], web_out_right[WEB_MAX_RENDER];
static uint32_t web_ring_w, web_ring_rp, web_audio_due, web_started;

EXPORT uint32_t web_sample_rate(void) { return FS; }
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
EXPORT uint16_t *web_screen(void) { return web_fb; }
EXPORT uint32_t web_screen_draws(void) { return web_fb_draws; }
EXPORT uint32_t web_led_buttons(void)
{
    uint32_t i, bits = 0;
    for (i = 0; i < 14u; i++)
        bits |= (uint32_t)(web_leds[i] != 0) << i;
    return bits;
}
EXPORT uint32_t web_led_keys(void)
{
    uint32_t i, bits = 0;
    for (i = 0; i < 27u; i++)
        bits |= (uint32_t)(web_leds[14u + i] != 0) << i;
    return bits;
}
EXPORT uint8_t *web_settings_data(void) { return web_settings; }
EXPORT uint32_t web_settings_size(void) { return sizeof web_settings; }
EXPORT uint32_t web_settings_writes_count(void) { return web_settings_writes; }
EXPORT float *web_audio_left(void) { return web_out_left; }
EXPORT float *web_audio_right(void) { return web_out_right; }

EXPORT void web_boot(void)
{
    if (!web_started) {
        app_start();
        web_started = 1;
    }
}
EXPORT void web_buttons(uint32_t mask) { web_input_buttons(mask); }
EXPORT void web_keys(uint32_t mask) { web_input_keys(mask); }
EXPORT void web_encoder(uint32_t role, int32_t delta) { web_input_encoder(role, delta); }
EXPORT void web_master(uint32_t value) { web_input_master(value); }
EXPORT void web_midi(uint32_t status, uint32_t d1, uint32_t d2) { web_input_midi(status, d1, d2); }

/* Render requested frames while advancing the simulated 1 ms main loop and 256-frame audio DMA halves. */
EXPORT void web_render(uint32_t requested)
{
    static int32_t block[2u * HALF_FRAMES];
    uint32_t i, n = requested > WEB_MAX_RENDER ? WEB_MAX_RENDER : requested;
    web_boot();
    while (web_ring_w - web_ring_rp < n) {
        uint32_t frame;
        platform_poll();
        app_step();
        web_audio_due += FS;
        while (web_audio_due >= 1000u * HALF_FRAMES) {
            web_audio_due -= 1000u * HALF_FRAMES;
            for (i = 0; i < 2u * HALF_FRAMES; i++)
                block[i] = 0;
            app_audio(block, HALF_FRAMES);
            for (frame = 0; frame < HALF_FRAMES; frame++) {
                int32_t l = (block[2u * frame] * web_master_q12) >> 12;
                int32_t r = (block[2u * frame + 1u] * web_master_q12) >> 12;
                uint32_t slot = web_ring_w++ % WEB_RING;
                if (l > 32767) l = 32767;
                if (l < -32767) l = -32767;
                if (r > 32767) r = 32767;
                if (r < -32767) r = -32767;
                web_ring_l[slot] = (float)l / 32768.0f;
                web_ring_r[slot] = (float)r / 32768.0f;
            }
        }
    }
    for (i = 0; i < n; i++) {
        uint32_t slot = web_ring_rp++ % WEB_RING;
        web_out_left[i] = web_ring_l[slot];
        web_out_right[i] = web_ring_r[slot];
    }
}
