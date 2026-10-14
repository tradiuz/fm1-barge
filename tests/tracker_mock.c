/* SPDX-License-Identifier: GPL-3.0-only */
/* Mock: tracker sequencer routing N tracks to M instrument slots. Not wired into the firmware build.
 * cc -I../firmware/src tracker_mock.c -o /tmp/tracker_mock && /tmp/tracker_mock */
#include <stdio.h>
#include <string.h>
#include "instrument.h"

enum { TRACKS = 4, SLOTS = 4, ROWS = 16, SONG_LEN = 4, SLOT_STATE_WORDS = INSTRUMENT_STATE_BYTES / 4 };
enum { NOTE_NONE = 0, NOTE_OFF = 255, SLOT_KEEP = 255 };

typedef struct { uint8_t note, slot, vel; } step_t;      /* slot picks the instrument per step */
typedef struct { step_t step[ROWS]; } pattern_t;

typedef struct {
    const instrument_desc_t *desc;
    uint32_t state[SLOT_STATE_WORDS];                    /* one private state block per instance */
} slot_t;

typedef struct { uint8_t slot, note, mute; } track_t;   /* what this track is currently sounding */

typedef struct {
    pattern_t pat[8];
    uint8_t order[TRACKS][SONG_LEN];                     /* per-track pattern order: tracks can run different patterns */
    uint8_t default_slot[TRACKS];
    slot_t slot[SLOTS];
    track_t trk[TRACKS];
    uint32_t frames_per_row, frames_left;
    uint8_t row, pos;
} seq_t;

static void slot_event(seq_t *s, uint8_t slot, uint8_t type, uint8_t trk, uint8_t note, uint8_t vel)
{
    instrument_event_t e = { type, trk, note, vel };
    if (s->slot[slot].desc && s->slot[slot].desc->event)
        s->slot[slot].desc->event(s->slot[slot].state, &e);
}

static void seq_row(seq_t *s)
{
    for (uint8_t t = 0; t < TRACKS; t++) {
        const step_t *st = &s->pat[s->order[t][s->pos]].step[s->row];
        track_t *tr = &s->trk[t];
        if (st->note == NOTE_NONE || tr->mute) continue;
        if (tr->note) slot_event(s, tr->slot, INSTRUMENT_EVENT_NOTE_OFF, t, tr->note, 0); /* tracks are monophonic */
        if (st->note == NOTE_OFF) { tr->note = 0; continue; }
        tr->slot = st->slot == SLOT_KEEP ? s->default_slot[t] : st->slot;
        tr->note = st->note;
        slot_event(s, tr->slot, INSTRUMENT_EVENT_NOTE_ON, t, st->note, st->vel);
    }
    if (++s->row == ROWS) { s->row = 0; s->pos = (uint8_t)((s->pos + 1) % SONG_LEN); }
}

/* Audio-callback entry: split the block at row boundaries so timing is sample-accurate, then render each slot once per chunk. */
static void seq_render(seq_t *s, int32_t *out, uint32_t frames)
{
    while (frames) {
        uint32_t n = frames < s->frames_left ? frames : s->frames_left;
        for (uint8_t i = 0; i < SLOTS; i++)
            if (s->slot[i].desc) s->slot[i].desc->render(s->slot[i].state, out, n);
        out += 2u * n; frames -= n; s->frames_left -= n;
        if (!s->frames_left) { seq_row(s); s->frames_left = s->frames_per_row; }
    }
}

/* ---- host demo with a fake instrument that logs events ---- */
static uint32_t now;
static void fake_event(void *st, const instrument_event_t *e)
{
    printf("frame %7u slot %u: %s trk %u note %u\n", now, *(uint32_t *)st,
           e->type == INSTRUMENT_EVENT_NOTE_ON ? "ON " : "OFF", e->channel, e->note);
}
static void fake_render(void *st, int32_t *o, uint32_t n) { (void)o; if (*(uint32_t *)st == 0) now += n; }
static const instrument_desc_t FAKE = { 0x9999u, INSTRUMENT_ABI_VERSION, "FAKE", 4, 4, 0, fake_event, fake_render, 0 };

int main(void)
{
    static seq_t s;
    static int32_t buf[2 * INSTRUMENT_MAX_FRAMES];
    for (uint8_t i = 0; i < SLOTS; i++) { s.slot[i].desc = &FAKE; s.slot[i].state[0] = i; }
    s.frames_per_row = 44100u * 60u / (120u * 4u);       /* 120 BPM, 16th notes */
    s.frames_left = 1;                                   /* first row fires after one frame */
    s.order[1][0] = 1; s.order[2][0] = 7; s.order[3][0] = 7; /* pattern 7 stays empty */
    s.default_slot[0] = 0; s.default_slot[1] = 1; s.default_slot[2] = 2; s.default_slot[3] = 3;
    s.pat[0].step[0] = (step_t){ 36, SLOT_KEEP, 100 };
    s.pat[0].step[4] = (step_t){ 36, 2, 100 };           /* track 0 borrows slot 2 for one step */
    s.pat[1].step[2] = (step_t){ 60, SLOT_KEEP, 90 };
    for (uint32_t f = 0; f < 44100u; f += INSTRUMENT_MAX_FRAMES) seq_render(&s, buf, INSTRUMENT_MAX_FRAMES);
    return 0;
}
