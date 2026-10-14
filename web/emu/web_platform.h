/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Barge contributors
 * Browser-side fake FM-1 services for firmware/src/barge.c. The simulator keeps the app callbacks and graphics
 * renderer intact while replacing the hardware, flash and USB endpoints with a worklet-driven adapter. */
#pragma once
#include <stdint.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "BARGE"
#endif
#ifndef APP_VER
#define APP_VER "WEB"
#endif
#define FS 44100u
#define HALF_FRAMES 256u
#define CV_MAX (240u * 64u)

static volatile uint32_t fm1_ms;
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t web_button_down, web_button_up, web_note_down;
static int32_t web_encoder_steps[7];
static uint8_t web_leds[41];
static uint32_t web_fb_draws;
static uint16_t web_fb[240u * 240u];
static uint8_t web_settings[260];
static uint32_t web_settings_writes;
static uint8_t web_midi_q[64][3];
static uint32_t web_midi_w, web_midi_r;
static int32_t web_master_q12 = 2048;

static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = c;
    web_fb_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) {
            uint16_t c = p[j * w + i];
            web_fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
        }
    web_fb_draws++;
}
#include "../../firmware/src/gfx.c"

static int settings_load(void *p, uint32_t n)
{
    if (web_settings[0] != 1u || n > sizeof web_settings - 4u)
        return -1;
    memcpy(p, web_settings + 4u, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    if (n > sizeof web_settings - 4u)
        return -1;
    web_settings[0] = 1u;
    memcpy(web_settings + 4u, p, n);
    web_settings_writes++;
    return 0;
}
static int fm1_input_edges(uint32_t *released)
{
    uint32_t down = web_button_down;
    if (released)
        *released = web_button_up;
    web_button_down = web_button_up = 0;
    return (int)down;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t down = web_note_down;
    web_note_down = 0;
    return down;
}
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t n = web_encoder_steps[e % 7u];
    web_encoder_steps[e % 7u] = 0;
    return n;
}
static void fm1_led_key(uint32_t id, int on)
{
    if (id < sizeof web_leds)
        web_leds[id] = (uint8_t)(on != 0);
}
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    uint32_t i;
    if (web_midi_r == web_midi_w)
        return 0;
    i = web_midi_r++ % 64u;
    *st = web_midi_q[i][0];
    *d1 = web_midi_q[i][1];
    *d2 = web_midi_q[i][2];
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
static void platform_poll(void) { fm1_ms++; }

/* These are called by barge_web.c and the page's AudioWorklet. */
static void web_input_buttons(uint32_t mask)
{
    uint32_t old = fm1_in.buttons;
    fm1_in.buttons = mask & 0x3FFFu;
    web_button_down |= fm1_in.buttons & ~old;
    web_button_up |= old & ~fm1_in.buttons;
}
static void web_input_keys(uint32_t mask)
{
    uint32_t old = fm1_in.notes;
    fm1_in.notes = mask & ((1u << 27) - 1u);
    web_note_down |= fm1_in.notes & ~old;
}
static void web_input_encoder(uint32_t role, int32_t delta)
{
    if (role < 7u)
        web_encoder_steps[role] += delta;
}
static void web_input_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    uint32_t i;
    if (web_midi_w - web_midi_r >= 64u)
        return;
    i = web_midi_w++ % 64u;
    web_midi_q[i][0] = (uint8_t)status;
    web_midi_q[i][1] = (uint8_t)d1;
    web_midi_q[i][2] = (uint8_t)d2;
}
static void web_input_master(uint32_t value)
{
    int32_t a = (int32_t)(value > 1023u ? 1023u : value);
    web_master_q12 = (a * a) >> 8;
}
