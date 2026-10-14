# Barge instrument modulation

ANALOG and PHASE include a shared, Barge-native modulation source. Each module owns its modulation settings, phase,
held sample, PRNG state, and one bounded 256-sample block snapshot inside its instrument state. There is no mutable
shared LFO, heap use, I/O, hardware access, flash operation, lock, or wait in rendering. The block's rate/wave/depth/
route/source controls are snapshots; phase advances once per rendered sample. Destination DSP updates are bounded to
16-sample slices where coefficients or phase-distortion shapes need rebuilding.

## Controls

Press `BTN_EDIT` to move between the existing parameter pages and the added modulation pages. `BTN_ENV` retains its
existing four-knob ADSR editor; FX and other button behavior are unchanged.

| Module | Existing parameters | Added modulation controls (in order) |
| --- | --- | --- |
| ANALOG | stored indexes 0–10 | 11 RATE, 12 WAVE, 13 DEPTH, 14 ROUTE, 15 MODE, 16 SOURCE |
| PHASE | stored indexes 0–7 | 8 RATE, 9 WAVE, 10 DEPTH, 11 ROUTE, 12 MODE, 13 SOURCE |

RATE is a bounded linear 0.01–12 Hz range. WAVE is SINE, TRIANGLE, rising SAW, SQUARE, or S&H. DEPTH is 0–127
destination units. MODE 0 is FREE (note-ons do not reset phase); MODE 1 retriggers the module LFO at phase zero. A
retriggered S&H is seeded deterministically from the note; free S&H advances its private generator only at cycle wraps.
SOURCE selects LFO or the current voice's ADSR level (0 to DEPTH). Several voices share one LFO phase, while each
voice's ADSR source is read independently. In retrigger mode, each accepted note-on resets that shared phase, so the
last note-on in an event block determines the phase all voices use.

Routes are ANALOG CUTOFF, SHAPE, or PWM, and PHASE DCW, PITCH, SUB, or LINE. LFO routes are bipolar; envelope routes
rise from zero to the selected depth. Values are clamped at their destination. Existing ANALOG ENV FILTER and PHASE
ENV DCW controls remain active and are summed with routed modulation. The `SUB` route modulates PHASE's sub level; the
`LINE` route modulates the PHASE second-line blend (its existing detune/line settings remain in effect).

The persistent settings record is schema B7. B6 and earlier settings migrate by preserving existing engine controls,
ADSR, FX, and WAVE data while seeding the added controls to neutral defaults; migrated records are rewritten only by
the normal quiet-time settings save. Preset banks stay external JSON data. SysEx preset protocol v2 still accepts the
legacy ANALOG 11 / PHASE 8 parameter counts and additionally accepts the extended 17 / 14 counts. No WAVE runtime data
frame or SysEx v2 field order changed.

## Upstream scope and provenance

The design follows Felucca's LFO ideas (rate and common wave choices, a note-retrigger option, sample-and-hold, and
using the amp envelope as a modulation source), but `instrument_modulation.c` is a new Barge implementation. Its bounded
integer rate conversion and per-sample phase stepping, retrigger logic, and module-owned S&H generator are not copied
Felucca routines. SINE uses Barge's existing `instrument_sine_q15` approximation; S&H uses Barge's existing
`instrument_xorshift32` helper.

Intentionally omitted from Felucca: tempo/note-value synchronization and clock following; unipolar/fade/phase controls;
Felucca's multi-slot source/destination/amount modulation matrix; velocity, key, random-per-note, mod-wheel, aftertouch,
expression, S&H slew, and sequencer-time sources; modulation of FX sends, pan, LFO rate/depth, arbitrary engine
parameters, or note timing; polyphonic independent LFOs; and the Felucca track/editor/automation framework. Barge has no
track model or general modulation matrix in this feature.

The new module helper and route glue are GPL-3.0-only Barge code. The existing sine approximation is credited to the
Dinghy-derived Barge DSP baseline; see [Licensing](../LICENSING.md). The upstream reference for design concepts is
Felucca `firmware/src/voice.c` and `firmware/src/mod.c`, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), GPL-3.0-only.
No Felucca LFO or modulation routine was copied into this feature.
