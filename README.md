<p align="center"><img src="assets/logo.svg" width="96" alt="Barge"></p>

# Barge

**A customizable firmware platform for the M-VAVE FM-1 / JieLi AC79.**

Barge starts from Dinghy's proven FM-1 platform: USB rescue and updates, USB-MIDI, USB audio, panel input,
display, settings, crash handling, and a host simulator. Its statically linked instrument modules include a
reduced ANALOG recovery instrument, PHASE/LOFI/NOISE adapters, and a WAVE renderer with a runtime-uploaded
32-point data table. Press SEL to switch; each engine uses the four encoders for its local controls. A stateless
CLIPPER insert is available from FX, with its threshold on KNOB 4. ANALOG and PHASE also have a compact Barge-native
LFO/ADSR modulation layer; these are still reduced Barge adapters, not full Felucca engine/framework ports.

```
firmware/src/barge.c      <- application UI, input/event translation and audio host
firmware/src/platform.c   <- boot, USB, updates, audio ISR, settings and rescue
firmware/src/instrument*.c <- static instrument registry, manager and instrument adapters
firmware/src/fx*.c          <- fixed FX manager and zero-latency CLIPPER
firmware/src/*.c, hal/    <- platform and hardware abstraction (HAL)
```

Barge is derived from [Dinghy](https://github.com/zvenson/dinghy), which carries platform work from
[Felucca](https://github.com/hugelton/Felucca) and [SLOOP](https://github.com/isod89/sloop-fm1). The sibling
repositories in this workspace are comparative references; Barge will keep its own platform and module
boundaries rather than importing whole firmware forks.

> **Status: baseline adopted; Barge-specific changes are in progress.** The Dinghy baseline was tested on FM-1
> hardware. Barge-specific builds and hardware behavior must be validated independently before release.

## What you get

| | |
| --- | --- |
| **Updates that always work** | `tools/fm1_install.py`, the web installers and the [FM-1 Firmware Hub](https://fm1.designburgapps.com) reach your firmware over USB, so you (and your users) can always install the next build, or go back to the official firmware. |
| **USB rescue** | Hold **OCT-** while switching on: no sound, only the updater. If your firmware crashes twice at boot, the FM-1 goes there by itself. |
| **USB-MIDI** | In and out, class compliant: `midi_get()`, `midi_send()`. |
| **USB audio** | The FM-1 shows up as a 16-bit stereo audio input (44.1 or 48 kHz): record it into any DAW or phone. Nothing to do. |
| **Panel** | 27 keys, 14 buttons, 7 encoders, the MASTER knob, the LEDs, by name (`BTN_PLAY`, `ENC_KNOB1`, ...). |
| **Screen** | 240 x 240, text in two sizes, rectangles, lines, pixels. |
| **Settings** | `settings_load()` / `settings_save()`: up to 256 bytes with a CRC in their own flash sector. |
| **A crash screen** | A fault shows where it happened (the PC), then the FM-1 restarts. |
| **Host mode** | The same `barge.c` on your computer: the screen as an image, the sound as a WAV. No FM-1 needed to try an idea. |
| **Checks** | The build fails on what bricks or breaks an FM-1 firmware: a 64-bit division, a call into the chip ROM, too much RAM, a register touched outside `hal/`. |

## Build your own

From nothing to your own firmware on the FM-1, step by step. Each step works before you go to the next.

### 1. Set up once

You need Linux on x86-64 (or macOS / Windows with Docker or WSL), Python 3 with Pillow, the JieLi toolchain
and three files of the JieLi AC79 SDK:

```sh
pip3 install Pillow mido python-rtmidi
tools/get_toolchain.sh                      # JieLi's clang for the chip, to ~/.jieli/toolchain
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
```

On macOS the build runs the toolchain in a `linux/amd64` Docker container by itself (have Docker running).
`JIELI_TOOLCHAIN` and `AC79_SDK` point elsewhere if your copies live somewhere else.

### 2. Hear it without an FM-1

```sh
tests/run_host.sh                           # -> build/host/barge.wav and screen PNGs
python3 tests/test_host_render.py            # instrument permutations, controls, FX, deterministic baseline
```

Host mode runs the same `barge.c` on your computer: the sound as a WAV, the screen as images. Most ideas can be
tried here first, in seconds.

### Browser simulator

The interactive browser simulator runs the same Barge application callbacks in WebAssembly. Build it with
Emscripten (or Docker, if available), then serve the generated page over HTTP:

```sh
sh web/emu/build.sh                         # -> build/emu/{barge.wasm,index.html,worklet.js}
python3 -m http.server -d build/emu 8790    # open http://localhost:8790/
```

It simulates the app, screen, panel, audio and small settings record; it does not emulate the AC79 CPU/peripherals,
USB, updates or flash hardware. For true target behavior, use the documented hardware build and tests.

### 3. Try the instruments

Power the host simulator with `tests/run_host.sh`. The app starts in ANALOG; press SEL to cycle PHASE, LOFI, NOISE,
and WAVE. ANALOG's four controls are waveform, detune, oscillator mix and cutoff. For the editable WAVE table,
use [web/editor.html](web/editor.html) from a localhost/HTTPS origin in a Web-MIDI-capable browser. The application
UI is in [firmware/src/barge.c](firmware/src/barge.c); the independent renderers and shared oscillator helpers are
in [firmware/src/instrument.c](firmware/src/instrument.c) and its module sources. See [docs/API.md](docs/API.md)
for platform calls.

### 4. Onto the FM-1

```sh
./build.sh                                       # -> build/barge.fwsc (requires the JieLi toolchain and SDK)
python3 tools/fm1_install.py build/barge.fwsc    # over USB; asks before it writes, restarts into your build
python3 tools/fm1_install.py --info              # what runs on the connected FM-1
```

Instrument build inclusion defaults on; set `BARGE_ENABLE_ANALOG`, `BARGE_ENABLE_PHASE`, `BARGE_ENABLE_LOFI`,
`BARGE_ENABLE_NOISE`, `BARGE_ENABLE_WAVETABLE` or `BARGE_ENABLE_FX_CLIPPER` to `0` or `1` before `./build.sh`
to choose a smaller build. Keep ANALOG or WAVE included for recovery; the former SINE ID is reserved for saved-data
compatibility but no longer appears in the instrument list. Target resource reports still need to be captured with the JieLi toolchain.

That is the whole loop: change, hear it on the host, build, install. Back to another firmware, or to the official
one, with the [FM-1 Firmware Hub](https://fm1.designburgapps.com) in Chrome or Edge: Barge
answers the hub's updater like every firmware there. Installing firmware is at your own risk.

### 5. When it goes wrong

- **It crashes:** the screen shows where (the PC), then the FM-1 restarts. Twice at boot and it goes to the
  USB rescue by itself.
- **It won't start at all:** hold **OCT-** while switching on: the USB rescue, no sound, only the updater. Install
  a working build (or another firmware from the hub) from there.
- **The build refuses:** it checks for a 64-bit division, a call into the chip ROM, too much RAM, a register touched
  outside `hal/`. [docs/RULES.md](docs/RULES.md) says why each one matters; read it before your first crash.

### 6. Make it yours, release it, get into the hub

The release guide in [docs/MAKE-IT-YOURS.md](docs/MAKE-IT-YOURS.md) covers Barge's identity, releases, licensing,
and the Firmware Hub.

## Direction: customizable instrument modules

Barge's goal is to make instrument types from the FM-1 firmware ecosystem selectable modules, without inheriting
all of any one instrument firmware. The intended order is:

1. Stabilize the shared platform and establish a small module contract (audio rendering, note/event delivery,
  configuration, memory needs, and optional UI hooks).
2. Add instruments behind that contract one at a time, preserving their original attribution and license.
3. Make module selection configurable in the build, with explicit image/RAM budgets and tests per selection.
4. Runtime editor imports are data-only and interpreted by linked renderers; arbitrary native code still requires a
  separate validated loader, memory-map, packaging and recovery design.

All instruments share a sample-rate ADSR; **BTN_ENV** selects its four encoder controls, while **BTN_EDIT** pages
through engine and modulation parameters. ANALOG now includes table-based saw/square/triangle/sine/PWM, SYNC and SUB, detune/mix,
noise, drive, resonance SVF LP/BP/HP, keytracking and envelope-to-filter. WAVE shares the oscillator voice/ADSR/table
interpolator with an editor-uploaded table. PHASE includes its eight waves, WAVE2, second line MIX/RING, SUB, ENV-to-DCW
and FOLD. ANALOG and PHASE add selectable LFO/ADSR modulation routes while retaining these engine controls. LOFI and
NOISE retain their reduced scopes. Factory presets are JSON data banks in web/presets/, loaded through the editor at
runtime; they are not compiled as C preset tables. Barge still omits Felucca's general modulation matrix, sequencing,
project, voice framework and UI. The CLIPPER is a
stateless hard clip after instrument mixing and before MASTER gain; it defaults bypassed. The drum kit remains a
separate planned milestone with its own lane/grid workflow, not the melodic piano roll. See the
[instrument import survey](docs/INSTRUMENT-IMPORT-SURVEY.md) and the [plugin architecture plan](docs/PLUGIN-ARCHITECTURE-PLAN.md).
The WAVE editor's file format, SysEx protocol, validation, and distinction between runtime data and executable code
are described in [docs/RUNTIME-DATA-MODULES.md](docs/RUNTIME-DATA-MODULES.md); open [web/editor.html](web/editor.html)
from localhost or HTTPS in a Web-MIDI-capable browser to test it with hardware. This is runtime data import, not
runtime native-code loading; the renderer is still statically linked.

The detailed API, FX routing, static-versus-dynamic loading decision, CPU1 strategy, porting phases and acceptance
criteria are in [docs/PLUGIN-ARCHITECTURE-PLAN.md](docs/PLUGIN-ARCHITECTURE-PLAN.md).
The shared DSP primitives, table-provenance rules and AC79 single-precision FPU policy are in
[docs/DSP-SHARED-LIBRARY.md](docs/DSP-SHARED-LIBRARY.md).

The LFO controls, routes, settings migration, SysEx compatibility and intentionally omitted upstream modulation
features are documented in [docs/MODULATION.md](docs/MODULATION.md).

## Core and second CPU

The Felucca CPU1 pull request and X0X's existing CPU1 code are being used as design references. Their useful
lessons include guarded startup, RAM-resident idle code during flash operations, bounded job handoff, safe fallback
to CPU0, and bit-identical/thread-sanitized host tests. The Felucca render split assumes its four independent
instrument parts and state ownership; it is not a drop-in optimization for Barge's current tiny demo. CPU1 runtime
support will be integrated when the module/render architecture can assign independent work and when Barge-specific
measurements justify its memory and deadline costs.

## Where to go next

- **[docs/API.md](docs/API.md)**: everything `platform.c` gives the application, with the panel's ids, the screen calls, the
  audio contract, MIDI, settings, the flash map, timing.
- **[docs/MAKE-IT-YOURS.md](docs/MAKE-IT-YOURS.md)**: Barge's package identity, releases, and getting into the
  FM-1 Firmware Hub.
- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)**: the module-system direction and CPU1 integration criteria.
- **[web/emu/README.md](web/emu/README.md)**: browser simulator build, controls and limitations.
- **[docs/RULES.md](docs/RULES.md)**: AC79 single-precision FPU build flags, remaining arithmetic constraints, the audio ISR's budget and what the build
  checks for you. Read it before your first crash, not after.

## License

Barge is GPL-3.0-only. Its inherited platform and baseline carry their original notices and attributions; see
[LICENSING.md](LICENSING.md) before copying or distributing code.
