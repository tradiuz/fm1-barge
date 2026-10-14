# Shared instrument/FX DSP library policy

Barge currently compiles the app as one translation unit: `barge.c` includes the platform, instrument registry and FX manager, which include module implementations. The common instrument routines therefore live in `firmware/src/instrument_dsp.c`, included once before the module adapters. Keep module-private state and algorithms in their modules; share only routines whose semantics and numeric bounds are genuinely common.

## Shared primitives now

`instrument_dsp.c` owns the code/table already needed by multiple instruments:

- the read-only 12-entry C9 phase-increment table and note-to-increment helper;
- the original Barge/Dinghy refined-parabola Q15 sine approximation (used by PHASE's sub and internal PD cosine approximation);
- a split Q15 multiply for bounded intermediate values, and a common integer clamp;
- deterministic xorshift32 state advancement for LOFI and NOISE;
- the common voice-allocation score used by each four-voice module.

ANALOG and WAVE additionally share `instrument_osc_voice_t`, note allocation/release events, the fixed amp ramp,
and 32-point table interpolation. ANALOG selects one of three small immutable ROM lookup tables (saw/square/triangle)
or the shared default sine table, then drives two detuned table oscillators through its low-pass. WAVE uses the same
voice/render core with one editable table uploaded as data. This removes the separate simple SINE instrument while
preserving sine as a waveform in both engines. The retired SINE ID stays reserved for settings compatibility.

The adapters retain their distinct waveform, filter and state behavior. Do not force unrelated algorithms into one generic DSP helper merely to reduce line count. If two modules need the same generated table (e.g. sine, pitch, filter coefficients), keep one authoritative table/generator, record units and interpolation/range semantics, and cite the data's source/license. Avoid per-plugin copies that can diverge silently.

## Numeric implementation policy

The AC791N has a limited **single-precision** FPU. The target app build uses JieLi's `-mcpu=r3 -mfprev1` flags; without them, the compiler may emit unresolved soft-float helper calls. `double` remains unsupported. This correction is based on [Dinghy issue #1](https://github.com/zvenson/dinghy/issues/1), which includes a JieLi clang compile/link example.

Choose the numeric representation by algorithm and measured target cost:

- Prefer `float` when coefficients/state are naturally floating-point or when a faithful port is materially simpler and still meets the worst-case callback deadline.
- Prefer fixed point for bit-exact compatibility, low-cost bounded oscillators/levels, or where target measurement favors it. Do not mechanically convert a sound algorithm to Q-format if that creates fragile scaling or overflow hazards.
- Do not mix host and target comparisons without controlling FP contraction. Target and host build comparisons use `-ffp-contract=off` for a consistent multiply/add model; tests should still allow explicit tolerances when FPU and host rounding differ.
- Measure complete 256-frame worst-case render time on the target. FPU availability does not imply that every float-heavy algorithm is fast enough, deterministic, or safe in the audio interrupt.

Other target constraints remain: no `double`, no 64-bit integer division, bounded work, no allocation/I/O/flash/UI or blocking in render paths, and no module hardware access. See [RULES.md](RULES.md) and [PLUGIN-ARCHITECTURE-PLAN.md](PLUGIN-ARCHITECTURE-PLAN.md).

## Reuse gate for new modules

Before adding a helper or table, compare existing module and FX code for identical behavior and bounds. A shared primitive must specify:

1. input/output format, units, legal ranges and edge behavior;
2. integer overflow or floating-point precision assumptions;
3. whether its state is immutable, per-instance or shared;
4. deterministic host vectors and target timing coverage;
5. source/table provenance and required notices.

Only put truly shared stateless code and immutable lookup data in the common layer. Stateful filters, envelopes, random streams, delay histories and plugin controls remain instance-owned unless an explicit host-owned ABI says otherwise. Keep FX helpers in a shared DSP layer only when instruments and multiple effects actually use the same algorithm; FX registry/routing remains separate from the instrument registry.
