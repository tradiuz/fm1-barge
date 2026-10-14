# Barge architecture and porting notes

Barge is a Dinghy-derived FM-1 firmware, not a wholesale Felucca/SLOOP fork. The first objective is to keep the
small, recoverable hardware platform independent from instrument code; the later objective is selecting instrument
modules without pulling in an entire firmware's UI and workflow.

## Current architecture

- `firmware/src/platform.c` owns reset/boot guard flow, USB, the OTA session, rescue mode, settings, the audio DMA
  callback and the common panel/display facilities.
- `firmware/hal/` contains hardware register access, interrupt vectors, input scanning, audio/timer control and
  hardware safety guards. Preserve that separation: instrument code must not access MMIO directly.
- `firmware/src/barge.c` is the app/UI and event host. `instrument.c` owns statically linked modules; ANALOG is the
  recovery fallback, with WAVE used when an ANALOG-excluded build needs a fallback.
- `tests/host.c` runs the application callbacks on a desktop. It is useful for logic and repeatability but does not
  validate AC79 cache, interrupt, USB, flash or second-core behavior on hardware.

The baseline already carries the practical shared services Barge needs from the Felucca/SLOOP line: USB-MIDI and
USB audio, update/rescue, persistent settings, crash/boot guarding, a hardware HAL boundary, build-time image/RAM
checks, and host simulation. Barge should port *specific* generic improvements as justified, not sync all files from
Felucca: the projects have different app entry points, configuration, UI and resource budgets.

The target-build check now derives the `RAM` and `POOL` sizes from `firmware/app.ld` and enforces spare headroom.
This is a generic safety improvement; it does not import any synth engine or change the runtime image layout.

## Instrument-module direction

The initial module system should be statically linked and build-selectable. Runtime loading of arbitrary binaries is
not assumed: it would require a validated loader, relocation/linking model, compatibility checks, and a safe memory
and update format, none of which the current FM-1 platform provides.

A module contract should be defined before importing engines. At minimum, it needs to cover:

- module metadata/version and a build-time selection registry;
- initialization/reset and module-owned state/memory requirements;
- note-on, note-off and other time-sensitive input events;
- bounded audio rendering in the interrupt budget, with no I/O, allocation, flash access or blocking;
- optional parameter descriptions and UI hooks that do not make the platform depend on a particular instrument;
- per-module CPU, RAM, pool and flash budgets, plus host-render and target validation.

Selected modules should be replaceable without changing platform code. Any reused engine must retain its origin,
license, notices and attribution. Current ANALOG/WAVE/PHASE/LOFI/NOISE modules are deliberately reduced adapters,
not complete upstream engines. The old SINE module ID remains reserved only for saved-settings compatibility.

The proposed module ABI, FX graph/routing model, dynamic-loading feasibility gates, CPU1 pipeline, porting phases,
and acceptance tests are specified in [PLUGIN-ARCHITECTURE-PLAN.md](PLUGIN-ARCHITECTURE-PLAN.md).

## CPU1 research and integration gate

Felucca [PR #247](https://github.com/hugelton/Felucca/pull/247) adds second-core rendering of independent parts.
Its general hardware lessons are valuable: start core 1 before audio, provide a RAM-resident idle loop for flash
operations, use bounded job handoff and a checked mailbox, fall back to core 0 on failures, account for core-1
faults, and compare threaded host renders bit-for-bit under ThreadSanitizer. However, the actual split and its
state-isolation changes target Felucca's four-track renderer; engine-specific changes to modulation, voices,
formant/physical-model seeds, SPREAD and FM6 are not generic Barge platform code.

The local X0X reference is `fm1-x0x/firmware/hal/fm1_cpu1.h`, `fm1-x0x/firmware/hal/fm1_cpu1.S`, and
`fm1-x0x/firmware/src/app/plat_fm1.c`. It confirms a second-core start/hold API, guarded single-job mailbox and
fallback path, but its audio dispatch is also application-specific. Both designs warn that some FM-1 units can
misread core-1 RAM. The PR's device measurements are contributor-reported; they do not establish behavior on
Barge's target unit.

Barge's built-in modules currently have four-voice budgets, so adding CPU1 now would add startup, stack, linker, flash
parking, synchronization and recovery risks without a measured audio bottleneck. Revisit CPU1 once modules have
independent bounded render jobs. Before enabling it, port the complete HAL/assembly/linker/startup/fault/build/test
chain together, retain one-core operation, prove serial/parallel output equivalence and thread-race freedom, measure
target deadlines and stack use, and validate startup/reset/flash/rescue on Barge hardware. Do not copy only the
Felucca `fx.c` split or the X0X mailbox and call that CPU1 support.

## Reference comparison

- The Felucca PR changes CPU1-specific platform/linker/build paths and refactors Felucca DSP state so concurrently
  rendered parts do not share mutable data. Its menu setting, console telemetry and DSP partition logic are not
  needed until Barge has that workload.
- X0X demonstrates the low-level AC79 CPU1 interface locally, including chip-specific checks and bounded handoff;
  its sequencer/engine partition is not portable as-is.
- Use each repository's own docs/build/tests as authority for its implementation. Confirm chip/board assumptions in
  the JieLi SDK and on the intended hardware before porting register behavior.
