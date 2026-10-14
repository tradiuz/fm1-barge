# Barge instrument and FX plugin architecture plan

## Goal

Build a Barge-owned, extensible sound architecture that can host instruments and effects adapted from the other FM-1 firmware projects (initial examples: Felucca's ANALOG and FM6 engines, and its effect algorithms). Users should be able to choose among installed modules while keeping the platform, audio output, display, settings, and recovery code independent of any one instrument firmware.

The plan supports two meanings of “load a plugin” and keeps them distinct:

1. **Runtime-select a statically linked module:** module code was selected at build time and is in the firmware image; a setting or UI selection activates it at runtime. This is the recommended first implementation.
2. **Install/load module code after the firmware build:** module code is delivered separately and discovered/loaded later. This is a separate loader, memory-map, packaging, update and recovery project, not a property provided by the current `.fwsc` build.

The first version should implement the first meaning. Preserve a future path to the second without claiming that arbitrary native code can already be loaded safely.

## Baseline and constraints

- The current application is the instrument host in `firmware/src/barge.c`; the platform audio callback is called from the CPU0 ALNK0 audio interrupt in `firmware/src/platform.c`.
- The callback supplies 256 stereo frames at 44.1 kHz, about 5.8 ms per DMA half before accounting for higher-priority interrupts and output processing. Keep a measured deadline budget, not just a mean CPU figure.
- The app image is fixed-address XIP, with separate `XIP`, `RAM`, `POOL`, `RAMTEXT`, and reserved/no-init areas described in `firmware/app.ld`. `tools/build.py` and `tools/fm1pkg_make.py` produce one firmware package and enforce image and memory constraints. There is no plugin resource partition, relocation loader, or plugin ABI today.
- Target DSP must follow `docs/RULES.md`: bounded work, no `double` or 64-bit division, no hardware access from module code, and no I/O, screen drawing, flash operations, heap allocation, or waiting in audio render callbacks. The AC79 has a single-precision FPU; Barge enables JieLi's `-mcpu=r3 -mfprev1` flags. Choose `float` or fixed point per algorithm, then measure the target worst case.
- Host simulation is useful for functional and bit-exact checks, but it does not establish AC79 interrupt, cache, DMA, flash, or CPU1 correctness.

## Decisions to make before importing engines

1. **Sound topology:** begin with one selected instrument instance and a fixed voice budget; only introduce multiple parts once the ownership and mixing model is stable.
2. **Selection semantics:** build options choose which modules exist; a stable module ID in settings selects the active module. A stale/unavailable ID falls back safely to the baseline module.
3. **Code lifetime:** statically link module code for the first releases. Runtime selection does not imply dynamic code loading.
4. **DSP ownership:** Barge owns voice/event scheduling, output buffers, routing and parameter plumbing. A ported instrument owns its synthesis algorithm and private state, not Felucca's full track/project/UI stack.
5. **FX graph:** start with a deterministic, acyclic chain. Instrument-local inserts and shared send/master buses are different plugin roles and have different state ownership.
6. **CPU ownership:** keep DSP on CPU0 until measurement justifies CPU1. Keep audio DMA servicing and final buffer handoff under the existing platform owner; later use CPU1 for bounded rendering jobs only if its behavior is validated on the target FM-1.

## Proposed module boundary

Create Barge-owned interfaces in a small header (planned `firmware/src/plugin.h`) and a registry/manager (planned `firmware/src/plugins.c`). Exact types should be settled with the first two real ports rather than copying Felucca's `engine_t` wholesale.

### Shared callback rules

Every module descriptor should provide:

- a stable string/integer ID, ABI version, display name, capability flags, maximum state size/alignment, and declared worst-case work budget;
- an initialization/reset callback and an optional release/shutdown callback;
- a bounded control/event callback, called at a defined block boundary;
- a render/process callback with a fixed sample rate, bounded frame count, explicit input/output format and accumulation rules;
- optional parameter metadata and defaults, separate from hardware/UI implementation;
- optional state migration/version hooks for persisted module settings.

Callbacks receive an explicit host context and caller-owned instance state. No mutable singleton engine state, hidden global scratch buffers, heap allocation, flash writes, display calls, or hardware register access are allowed in render paths. Scratch memory and latency must be declared; unknown latency is rejected in the initial graph.

### Instrument plugin

The instrument API should receive timestamped note-on/off and controller events, then render a bounded block into a caller-owned stereo or mono buffer. Define whether instruments receive 256-frame blocks or smaller sub-blocks. A possible compromise is a 256-frame DMA half with event offsets, allowing a module to process smaller internal chunks without multiplying scheduler/IPC overhead.

The first ABI should support one instance. Design the state and routing types so multiple instances/parts can be added later without sharing module globals. Keep voice allocation generic only if the chosen first engines can share a useful contract; otherwise let an instrument own its voice allocation and state rather than forcing all engines into Felucca's `voice_t`.

### FX plugin

Use a separate effect descriptor; an instrument is not an effect. Each FX has an explicit role and routing point:

- **Insert:** processes one instrument's signal (and owns its history/state) before the mix or send tap.
- **Send/bus effect:** consumes an explicit accumulated send buffer and has one shared history/state owner.
- **Master effect:** processes the final mix after bus returns.

The initial graph should have fixed slots/order, no cycles, no arbitrary user-built graph, and no latency-bearing plugins. Each effect declares state size, maximum CPU cost, channel/format, in-place versus separate-buffer behavior, bypass semantics, and what happens to tails on preset change/disable. Parameter changes are snapshotted at block boundaries; the UI never mutates DSP state concurrently.

A practical initial signal order is: instrument render → optional insert → level/pan → dry mix and named sends → shared send effects → master output. Preserve deterministic addition order for reproducible regression renders. Do not assume Felucca's `fx.c` is an engine-independent library: it includes track parameters, mix state, inserts, sends, master processing and Felucca-specific parameter/global structures. Port algorithms and state incrementally behind Barge's FX ABI.

### Event/control crossing

Main-loop/UI input must not write complex plugin state while the audio renderer is using it. Add a bounded single-producer/single-consumer command queue (main loop producer, audio owner consumer) with defined overflow behavior. Note-off/all-notes-off and panic events need priority or a safe recovery policy so overflow cannot leave stuck notes. Include sample offsets if sample-accurate event placement is required; otherwise define one-block-boundary timing explicitly.

## Build selection versus post-build loading

### First: linked module registry

- Add build flags for module inclusion and retain one known-good recovery instrument (ANALOG by default; WAVE in reduced builds) in every image.
- Generate the registry at build time; avoid dynamic registration order determining persisted IDs.
- Link selected modules into the XIP image with the existing dead-code elimination and memory checks.
- Runtime menus/settings select a stable ID from that build's registry. If a stored ID is absent, start with the baseline and preserve the user's data.
- Report per-module code/image size, state bytes, pool bytes, and measured worst-case render cost. A build that excludes modules should reclaim code and engine-state storage where possible.

This yields modules that can be “loaded as needed” at runtime in the sense of initialized/selected as needed, while their code is already present in the firmware image.

### Later: separately delivered code modules (research only)

A native plugin installed after the main firmware would need a documented executable storage slot, linker/relocation or PIC rules for the pi32v2 toolchain, a versioned host ABI, bounds/integrity validation, safe enable/disable and rollback, cache/XIP rules, compatibility checks, packaging/install/update behavior, and a recovery path that survives a partial plugin update. The current flash map and updater do not define these guarantees. A normal data file or a function-pointer table is not sufficient to make an arbitrary post-build binary executable.

Do not write plugin code into guessed “free” flash or let the module manager execute unvalidated addresses. A future feasibility study can compare fixed-address XIP slots, a fully position-independent format if toolchain support exists, and data-only custom content. Defer implementation until the statically linked system demonstrates a concrete distribution need and a recoverable design can be proven.

## CPU1 strategy

CPU1 is a potential DSP worker, not the audio-output/display core by default. Barge should keep its present CPU0-only path as the reference and fallback.

### Why not split engines immediately?

- Current instrument adapters are not yet a measured target bottleneck.
- Felucca PR #247 parallelizes Felucca's independent tracks and required state isolation for its modulation matrix, SPREAD, envelope sync, random seeds and FM6 scratch state; that split is tied to its track/voice architecture.
- X0X has a guarded CPU1 mailbox and render partition, but it too is application-specific. Both report FM-1 units with unreliable CPU1 RAM reads; this must be treated as a hardware qualification risk, not solved by copying the API.
- A CPU0 audio ISR that dispatches CPU1 work and waits for it is still blocked on CPU1, reducing but not eliminating CPU0 work. It does not by itself leave CPU0 free for UI, and UI/display must remain in the main loop rather than the audio interrupt.

### Candidate architecture after CPU0 plugin rendering works

1. Keep a CPU0-only reference renderer that owns event consumption, module state transitions, and recovery.
2. Add optional CPU1 start/hold, private stack, exception reporting, mailbox/check word, bounded timeout, flash/XIP coordination, and automatic core-0 fallback as a complete HAL/linker/startup/fault/build change.
3. Partition only independent module instances with disjoint state and output buffers. Use separate per-core mix sums; never let both cores write the same samples, voice/module state, FX history, or control structures.
4. Prefer CPU1 to produce the next audio block into a ping-pong/pre-render buffer while CPU0 handles the currently due output buffer, final format conversion/master limit/UAC tap and DMA. This can free CPU0 during most of DSP rendering, but adds at least one block of latency, requires explicit producer/consumer sequence numbers and underrun handling, and may be invalid on units whose CPU1 reads RAM incorrectly.
5. Initially keep shared send/bus/master effects on one core after instrument sums are ready. Moving FX to CPU1 is a later measured option; stateful reverb/delay must never run concurrently for the same history.
6. On a missing, invalid, late or faulted result, use a documented policy: core-0 serial render if enough time remains, otherwise emit a bounded safe fallback (silence/last-safe block) and count the underrun. Never spin indefinitely in the audio ISR.
7. CPU1's idle/wait path must be safe during flash operations. Before any flash erase/write, finish or cancel outstanding work and park the second core in RAM as required by validated hardware behavior.

Do not enable CPU1 by default until the target device passes startup/reset tests, RAM/mailbox integrity checks, sustained worst-case audio, stack high-water measurement, flash/settings/update/rescue tests, and automatic fallback checks. Keep a build/runtime OFF switch while hardware diversity remains uncertain.

## Porting sequence

### Implementation status (2026-10-14)

- The CPU0 plugin skeleton has a statically linked registry, ANALOG recovery fallback (WAVE is fallback when ANALOG is excluded), bounded main-loop-to-audio SPSC event queue with panic-on-overflow, block-boundary switching, and versioned settings selection. The old SINE ID is reserved for settings compatibility, but SINE is no longer selectable.
- ANALOG, PHASE, LOFI, NOISE and WAVE each have reduced four-voice adapters and stable IDs. Their deliberate feature omissions are listed in README.md; none imports Felucca's framework, presets, sequencer or UI.
- Common pitch increments, the Barge/Dinghy sine approximation, bounded fixed-point helpers, xorshift32 and voice-allocation scoring now live once in `firmware/src/instrument_dsp.c`; plugin-specific wave/filter state stays module-local. Numeric choices may use the AC79 single-precision FPU when the callback budget permits; see `docs/DSP-SHARED-LIBRARY.md`.
- A separate FX manager and stateless zero-latency hard clipper are integrated after instrument mixing and before platform MASTER gain. It is bypassed by default; BTN_FX enables it and KNOB 4 sets its threshold.
- A first runtime **data-only module** path is documented and implemented for one 32-point WAVE record via Web-MIDI SysEx. It selects the statically linked wavetable renderer; it does not load native code. See `docs/RUNTIME-DATA-MODULES.md` and `web/editor.html`.
- `BARGE_ENABLE_ANALOG`, `BARGE_ENABLE_PHASE`, `BARGE_ENABLE_LOFI`, `BARGE_ENABLE_NOISE`, `BARGE_ENABLE_WAVETABLE` and `BARGE_ENABLE_FX_CLIPPER` can each be set to 0 or 1. Target image/state deltas and FM-1 timing remain unverified until the JieLi toolchain/SDK and target hardware are available.

### Phase 0 — establish measurements and reference behavior

- Preserve deterministic host WAV/screen output for the selected fallback as a regression baseline.
- Add a deterministic audio-render test and record peak/clipping, note release, maximum block time, and current image/RAM/pool use.
- Compare FPU-backed single-precision and fixed-point approaches where relevant; keep host/target comparison flags and tolerances explicit, and profile on the FM-1 before adopting a float-heavy effect.
- Define Barge's one-instance instrument and one-chain FX contracts on paper before porting any Felucca framework types.

### Phase 1 — plugin skeleton on CPU0

- Use ANALOG as the normal recovery renderer; builds that omit it must keep WAVE or another designated recovery renderer compiled in.
- Keep the retired SINE ID reserved and migrate old settings that selected it to the designated recovery renderer.
- Test fallback startup, stable IDs, runtime selection, defaults, reset, panic/all-notes-off, and deterministic output.

### Phase 2 — small, self-contained instrument ports

- Port one engine at a time, beginning with an oscillator/filter engine such as ANALOG after evaluating its dependencies, then FM6 once the parameter/patch model exists.
- Prefer direct DSP adaptation over copying `core.h`, `voice.c`, track sequencer, project storage, UI pages, or Felucca's complete engine registry.
- FM6 requires additional planning for voice/operator state, algorithm and patch representation, factory data/user patch storage, licensing text for msfa (Apache-2.0), and target cost. It should not be treated as a small source-file copy.
- Add builds/tests with each module on/off and bounded-state/worst-case render tests.

### Phase 3 — FX contract and graph

- **Initial proof implemented:** a separate fixed-order FX descriptor/manager and stateless hard clipper validate bounded in-place stereo processing, block-snapshot parameters and bit-exact bypass. The route is instrument mix → clipper → platform MASTER gain/output clamp.
- Add a stateful modulation effect, then delay/reverb only after state sizing, history ownership, tail handling and pool budgets are stable.
- Add a stateful modulation effect, then delay/reverb only after state sizing, history ownership, tail handling and pool budgets are stable.
- Build a fixed graph that lets instruments route through selectable inserts and named sends to FX modules. Test dry/wet sums, bypass, parameter changes at boundaries, state reset, clipping and accumulated tails.

### Phase 4 — extension and resource policy

- Add a module manifest/build configuration, stable IDs, versioned parameter serialization and compatibility fallback.
- Maintain per-module image/ROM, BSS/state, POOL, CPU and latency budgets; provide a minimal build and an all-supported-modules build.
- Document attribution/license requirements for every port. Felucca is GPL-3.0-only; FM6's msfa core has Apache-2.0 notice requirements. Check each source file and upstream license before copying.

### Phase 5 — CPU1 experiment after profiling

- First make the plugin render API's state ownership and output-buffer ownership parallel-safe, even though it initially runs serially.
- Implement/test the CPU1 HAL/startup/mailbox/fault/flash-parking chain as an optional platform feature, using X0X and Felucca PR #247 as references rather than drop-in code.
- Prove bit-identical or explicitly bounded equivalent output, no races under a threaded host harness, safe event handoff, no torn settings, no flash overlap, and safe underrun/fallback behavior.
- Test actual Barge builds on each target hardware unit before enabling CPU1 by default. Record load/underrun/stack/misread counters and retain CPU0-only mode.

## Acceptance criteria

- The sine baseline remains available and a bad/unavailable module ID recovers to it.
- Runtime switching happens at a safe block boundary; no stuck notes, unbounded tails, or partially updated parameter structures.
- A module cannot exceed declared state or render budgets without a test/build failure or explicit documented exception.
- Every module can be built in and out; the image and memory reports make the cost visible.
- Instrument→insert→send→bus/master routing is deterministic and has tested bypass, reset, tail and clipping behavior.
- CPU1 is opt-in until verified; CPU0-only output remains a tested recovery/fallback path.
- Post-build native code loading remains out of scope until a separate recoverable module-update/relocation design is validated.

## Parallel agent work harness

Workspace custom agents in `.github/agents/` are split by ownership so independent research/ports can proceed in
parallel without concurrently editing shared files:

| Agent | Owns / returns | Depends on |
| --- | --- | --- |
| `barge-plugin-coordinator` | Decomposition, task ownership, API freeze, integration order and final review | This plan |
| `barge-plugin-contract` | Plugin ABI, registry/manager, stable IDs, settings fallback and event queue | Phase 0 decisions; must publish/freeze the ABI before engine or FX code work |
| `barge-analog-port` | ANALOG algorithm adapter and its private tests/metadata | Frozen instrument ABI; assigned instrument-only files |
| `barge-fm6-port` | FM6/msfa adapter, patch-data strategy, notices and tests | Frozen instrument ABI and patch-storage decision; assigned instrument-only files |
| `barge-fx-port` | FX descriptor, fixed graph adapter and assigned effect algorithms/tests | Frozen FX ABI/routing ownership; no simultaneous edits to the instrument registry |
| `barge-cpu1` | CPU1 feasibility, HAL/scheduler/pipeline and hardware validation plan or isolated implementation | Measured DSP need and explicit buffer/state ownership; may research in parallel but implementation is gated |
| `barge-plugin-validation` | Independent regression, budget, sanitizer, resource and licensing audit | Candidate commits/builds; normally read-only |

Recommended parallel cadence:

1. Run contract, ANALOG, FM6, FX and CPU1 discovery as independent **read-only** tasks; validation reviews the plan.
2. Have the coordinator reconcile proposals and freeze instrument, FX and event/buffer ownership contracts.
3. Implement the contract/registry first. After it is stable, ANALOG, FM6 and FX can work in disjoint module-owned
	files in parallel; each must not edit the shared manager, common types or build manifest without coordinator
	assignment.
4. Run integration and tests serially after merge. CPU1 implementation remains gated on profiling; validation
	reviews results and hardware requirements independently.

Custom agents are role/context boundaries, not autonomous concurrent workers or proof of correctness. The
coordinator must explicitly launch/delegate parallel work, resolve conflicting findings, integrate changes and run
the verification matrix.
