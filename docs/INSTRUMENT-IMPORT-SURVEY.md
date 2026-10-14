# FM-1 instrument import survey

This survey inventories sound-producing engines in the sibling repositories and recommends candidates for Barge. It separates DSP modules from their original sequencers, project formats, editors, and platform code. Engine lists reflect the checked-in firmware; optional and retired entries are called out separately.

## Inventory by repository

| Repository | Sound-producing instruments / engines | Drum and workflow notes |
| --- | --- | --- |
| **Felucca** | ANALOG, PHASE, LOFI, SAMPLE, VOICE (formant), TRIO, WHEEL, GRAIN, PHYS, DRUM, NOISE, FM6, and SLICE when its build option is enabled. DIGITAL is a retired/reserved slot; an optional legacy build can include it. Sources and stable IDs are in [engines.c](../../Felucca/firmware/src/engines.c). | DRUM is a real engine, not an effect or track mode. It has eight synthesized lanes, kit choices and GM-note mapping ([eng_drum.c](../../Felucca/firmware/src/eng_drum.c), [drum_voice.c](../../Felucca/firmware/src/drum_voice.c)). Its sequencer uses a dedicated 8-lane × 16-step grid with lane-select and hit editing, plus KIT/HIT and LANES pages; melodic tracks use the piano-roll graph ([ui.c](../../Felucca/firmware/src/ui.c), [ui_graph.c](../../Felucca/firmware/src/ui_graph.c)). |
| **Ghoulbox** | ANALOG, FM6, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, PHYS, GURDY, NOISE, DRUM. GURDY is the project-specific hurdy-gurdy engine. DIGITAL and SLICE are not offered in its normal engine chooser; legacy IDs/data are retained. Registry: [engines.c](../../ghoulbox-fm1-dungeon-synth/firmware/src/engines.c). | Inherits Felucca's dedicated DRUM-grid workflow and adds themed kits. GURDY has a performance-oriented crank mode in which SELECT acts as the wheel control; this needs a suitable Barge controller event, not just note-on/off. |
| **JIANT** | ANALOG, FM6, PHASE, LOFI, VOICE, WHEEL, DRUM-X, and NOISE are the active engines. DRUM-X replaces the Felucca DRUM implementation at the same stored engine index. SAMPLE, TRIO, GRAIN, PHYS and SLICE are retired; DIGITAL is optional/legacy. Registry: [engines.c](../../JIANT/firmware/src/engines.c). | DRUM-X synthesizes eight kit sounds, each with A/B patches and a live MORPH. It has dedicated SOUND/SOUND 2 editing and grouped live mutes; the drum-step grid remains a separate workflow. See [TONIC-DRUMX.md](../../JIANT/docs/TONIC-DRUMX.md) and [ui_dx.c](../../JIANT/firmware/src/ui_dx.c). |
| **SLOOP** | ANALOG, DIGITAL, PHASE, LOFI, SAMPLE, VOICE, TRIO, DRAWBAR (tonewheel organ), GRAIN, FM6, PHYS and NOISE; SLICE is optional. The engine list is in [engines.c](../../sloop-fm1/firmware/src/engines.c). | Also has a dedicated drum-track subsystem, separate from the synth engine table: sampled and synthesized kits, 16 drum sounds/lanes, user kits and a drum-synth kit editor. See [README.md](../../sloop-fm1/README.md) and [editor_dsyn.c](../../sloop-fm1/firmware/src/editor_dsyn.c). This is substantially more than a generic synth parameter page. |
| **fm1-x0x** | A TR-909-style drum part, TR-808-style drum part, two TB-303-style bass parts (303 A/B), and a break/slice player. Implementations are in [app/engine.c](../../fm1-x0x/firmware/src/app/engine.c) and [dsp/](../../fm1-x0x/firmware/src/dsp/); [engine.h](../../fm1-x0x/firmware/src/app/engine.h) documents the part/parameter boundary. | The 808 and 909 are distinct drum voices/engines. The breaks player requires loop assets and pattern-driven slice control. 303 accent, slide and tie behavior is coupled to its sequencer model; none of these are drop-in modules. |
| **Dinghy** | A minimal four-voice sine instrument in [dinghy.c](../../dinghy/firmware/src/dinghy.c). | Smoke-test app with keys and MIDI; no engine selector, drum instrument or piano-roll sequencer. |
| **Barge** | Reduced ANALOG, PHASE, LOFI and NOISE modules plus a data-editable WAVE instrument; ANALOG is the default recovery module. The old sine ID is reserved but not selectable. | Barge is the destination platform. Its static plugin boundary is described in [PLUGIN-ARCHITECTURE-PLAN.md](PLUGIN-ARCHITECTURE-PLAN.md). It does not currently provide Felucca's piano roll or drum grid, so neither workflow should be assumed to exist. |
| **fm1-to-smk37** | No sound engines; this is a control-mapping/porting guide with examples, not firmware DSP. | Useful for future alternate-controller mappings only. |
| **fm1-custom-fw** | No sound engines; repository contains firmware discovery, SysEx and flashing tools/documentation rather than playable DSP. | Its project inventory may point to upstream engine sources, but those sources are in the other repositories above. |

### Felucca engine tests and important distinctions

Felucca has focused tests for ANALOG, PHASE, SAMPLE, PHYS, DRUM, NOISE, FM6 and SLICE, plus an engine-wide preset regression. See [tests/](../../Felucca/tests/), including [drum_test.c](../../Felucca/tests/drum_test.c), [fm6_test.c](../../Felucca/tests/fm6_test.c), and [regress.c](../../Felucca/tests/regress.c). Some other engines are covered by the broad regression rather than a dedicated test.

Do not count these as additional instruments: Felucca's DIST/INSERT/reverb/delay/chorus are effects; POLY/MONO/LEGATO/UNISON are voice-allocation modes; SLICE is a sample/slice player, not the DRUM grid. Retired DIGITAL, optional FM4, and stored legacy sounds should be listed separately from the current default chooser.

## Drum-specific UI is a first-class import cost

A drum DSP callback alone does not provide a usable drum instrument. Felucca's eight-lane kit expects drum-note/trigger semantics, kit and lane controls, and a dedicated step grid rather than the melodic piano roll. Other projects reinforce that this is not one interchangeable UI:

| Reference | Sound model | UI / persistence that is coupled to it |
| --- | --- | --- |
| Felucca DRUM | Eight synthesized lanes, kit variants, GM drum map, lane levels and closed/open-hat choke. | 8 × 16 lane grid; select a lane, then edit its hits; KIT/HIT and lane-level pages. This is the smallest credible full-feature drum port if the Felucca-family sound is desired. |
| Ghoulbox DRUM | Felucca-family kit with dungeon-themed content. | Same broad lane/grid workflow; themed kit content is not a new engine. |
| JIANT DRUM-X | Eight fully synthesized sounds, two patches per sound and block-updated morph. | Per-lane SOUND pages, grouped mutes and section-owned kit data; greater sound-design/editor scope than Felucca DRUM. |
| SLOOP drum subsystem | Sampled and synthesized kits, including user kits. | Dedicated kit/sample editor, storage, backup/transfer, live mutes and 16-lane drum track. This is a subsystem, not one engine entry. |
| X0X 808 / 909 | Separate drum machines with different voice models; the 909 includes sampled hats/cymbals. | Groovebox-specific part selection, step patterns and kit controls. Source licenses/assets and sequencer assumptions need individual review. |

**Barge implication:** keep a drum module distinct from melodic instruments in the product plan. At minimum, a useful port needs drum trigger/note mapping, lane and kit identity, choke rules, and a drum-specific control/step-editing experience. A first DSP-only prototype may use fixed factory kits, but it should be labelled as a prototype rather than implying piano-roll parity. Barge currently has no sequencer UI, so the grid is new work, not an existing page to reuse.

## Recommended import shortlist

1. **ANALOG — first instrument port, reduced adapter implemented.** Barge has a four-wave/two-oscillator/low-pass prototype and deterministic host selection/control tests. This is not feature parity with Felucca: PWM, noise, drive, SVF resonance/modes and factory patches remain out of scope (the sub oscillator is a sine mix, not Felucca's SUB/SYNC engine modes).
2. **PHASE, LOFI and NOISE — reduced adapters implemented.** PHASE has three phase-distortion waves and sine sub; LOFI has four low-bit oscillator/noise forms with sample-hold/bit reduction; NOISE has white/dust sources and cascaded low-pass stages. These are useful Barge prototypes, not complete upstream engine ports: presets, framework, full feature sets and UI are omitted. Host selection/control/stress tests exist; target cost and upstream behavior comparisons remain pending. Preserve their GPL-3.0-only attributions.
3. **DRUM — separate, explicitly scoped instrument milestone.** Prefer Felucca DRUM as the first kit DSP candidate if the goal is a compact eight-lane instrument with existing focused tests. In the proposal, budget the drum grid/control pages separately from DSP. Consider JIANT DRUM-X later if morphable per-lane synthesis is a product requirement; consider SLOOP's drum subsystem only if user sample kits and the editor/storage workflow are in scope. X0X 808/909 are alternate sound references, not the first import target.
4. **FM6 — later, after state/parameter/patch policy.** It is valuable but has operator/patch state, factory/user patch strategy and an Apache-2.0 msfa notice requirement in addition to Felucca's GPL-3.0-only wrapper.
5. **GURDY or PHYS — later, if their distinctive sound/control warrants it.** GURDY needs a performance-control mapping; PHYS is stateful and includes third-party MIT-attributed DSP. Measure memory and render cost before choosing either.
6. **SAMPLE, GRAIN and SLICE — defer until data/storage exists.** They depend on sample data, decoder/source slots, pool memory and in some cases slicing/onset metadata. They are not small instrument-only ports.

Dinghy still has its standalone sine app. Barge removed SINE as a selectable instrument; its small sine approximation remains only as a PHASE helper, while ANALOG and WAVE cover basic oscillator tones. X0X's 303 and break player should be evaluated as separate, richer-event/asset projects rather than ordinary note-on/off instruments.

## Upstream ANALOG and PHASE features not yet in Barge

The current adapters share oscillator voice allocation, amplitude ramps and 32-point table interpolation where applicable, but are intentionally not feature-complete ports:

| Upstream engine | In the current Barge adapter | Still missing versus upstream |
| --- | --- | --- |
| Felucca ANALOG | Seven wave modes using compact table oscillators: saw/square/triangle/sine/PWM, SYNC and SUB; two detuned oscillators, noise, drive, trapezoidal SVF with resonance and LP/BP/HP, keytracking, PWM/sync shape, envelope-to-filter and shared ADSR. | Upstream polyBLEP anti-aliasing and the accurate fractional sync-restart correction are simplified; Barge adds its own compact LFO/ADSR routes but omits Felucca's general modulation matrix, sequencer automation and track framework. Its 12 upstream patch records are external JSON data, uploaded at runtime. |
| Felucca PHASE | All eight phase-distortion waves, WAVE2 alternation, second detuned line with MIX/RING, sine SUB, envelope-to-DCW, FOLD with DC blocker/gain compensation/block transition crossfade, and shared ADSR. | Felucca's LFO/modulation matrix, track/voice framework and sequencer automation are not imported. The six factory patches are external JSON data, uploaded at runtime. Some FOLD/DCW interpolation and sine-table numerics are Barge-specific rather than bit-identical Felucca code. |

Preset data is stored in [web/presets/analog.json](../web/presets/analog.json) and [web/presets/phase.json](../web/presets/phase.json), loaded by the editor and sent as data at runtime; no factory preset arrays are compiled into the instrument modules. Keep future presets in data files and add parameter validation/tests without bringing in Felucca's preset/track structs. The principal remaining fidelity gap is ANALOG's band-limiting and fractional sync-restart correction; validate aliasing, filter response, envelope timing, FOLD transitions and line modes before claiming bit-level parity.

## Licensing and evidence gates

The projects are primarily GPL-3.0-only, but individual modules carry additional obligations: FM6's msfa core is Apache-2.0; PHYS includes MIT-licensed DaisySP/Rings-derived components; X0X uses Open303 (MIT), third-party drum material/samples, and a separately permitted breakbeat port. Audit the exact source and asset notices before copying anything; do not treat repository-level license similarity as sufficient.

For every accepted module, require an isolated Barge adapter, deterministic host tests, build-in/build-out and resource deltas, and target worst-case timing. Host tests or another firmware's measurements do not establish Barge hardware behavior.
