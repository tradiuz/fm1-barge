# Licensing

Barge is distributed under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The Dinghy-derived app now at `firmware/src/barge.c`, the Barge wiring, host-mode adaptation and project docs | zvenson and Barge contributors |
| Barge instrument host/registry and shared DSP primitives (`firmware/src/instrument.c`, `instrument.h`, `instrument_dsp.c`) | Barge contributors; the remaining sine approximation derives from the Dinghy baseline and is used by PHASE |
| ANALOG/PHASE LFO and routed modulation (`firmware/src/instrument_modulation.c`, module route glue) | New Barge implementation, GPL-3.0-only; inspired by Felucca LFO/modulation concepts but does not copy upstream routines. Sine uses Barge's existing Dinghy-derived approximation; S&H uses Barge's existing xorshift helper. Details/provenance: [MODULATION.md](docs/MODULATION.md). |
| Built-in WAVE data renderer and runtime SysEx/settings path (`firmware/src/instrument_wavetable.c`, `web/editor.html`) | Barge contributors; user waveform data is user-provided and is not executable code |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |
| Browser simulator structure follows Felucca's WebAssembly/AudioWorklet emulator and its X0X reference | GPL-3.0-only | `web/emu/` (Barge-specific adapter and UI) |
| Initial ANALOG adapter's fixed-point oscillator/filter approach | GPL-3.0-only | `firmware/src/instrument_analog.c`; adapted from Felucca `firmware/src/eng_analog.c` and DSP routines, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments. This reduced adapter does not include Felucca framework code. |
| ANALOG and PHASE runtime factory patch data | GPL-3.0-only | `web/presets/analog.json` and `web/presets/phase.json`; parameter/ADSR records adapted from Felucca `eng_analog.c` / `eng_phase.c`, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments. The records are editor-loaded data, not compiled C presets. |
| PHASE phase-distortion oscillator routines | GPL-3.0-only | `firmware/src/instrument_phase.c`; adapted from Felucca `firmware/src/eng_phase.c`, itself credited there as the author's GPL-3.0 C port of CrispyZebra. |
| LOFI and NOISE instrument concepts | GPL-3.0-only | `firmware/src/instrument_lofi.c` and `firmware/src/instrument_noise.c`; Barge-specific reduced adapters informed by Felucca `firmware/src/eng_lofi.c` and `firmware/src/eng_noise.c`, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments. |
| Stateless hard-clipper FX and manager | GPL-3.0-only | `firmware/src/fx.h`, `fx.c`, and `fx_clipper.c`; Barge contributors. |

## The logo

`assets/logo.png` is a legacy Dinghy/designburg signet asset (Copyright (C) designburg, all rights reserved) and is
not used as Barge branding. `assets/logo.svg` is Barge's original mark.

## Trademarks

"Dinghy", "Felucca" and "Hügelton Instruments" are names of their respective owners; "M-VAVE" and "FM-1" belong
to their owners. Barge is not made or endorsed by them.
