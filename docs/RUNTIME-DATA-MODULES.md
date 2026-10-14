# Runtime data modules through the web editor

## Two meanings of “load a module”

Barge distinguishes executable plugins from runtime-loaded sound data:

1. **Instrument/FX implementation code** is compiled into the firmware image. The firmware registry may select those built-in algorithms at runtime. The web editor cannot install native code, relocations, callbacks, or new executable instructions.
2. **A data module** is a bounded record interpreted by an already-linked renderer. An editor can upload that record over USB-MIDI SysEx; firmware validates it and selects the built-in renderer. This is analogous to importing a sample or patch, but it does not add new code or arbitrary DSP behavior.

This test version implements a user wavetable data module called **WAVE** and data-only parameter presets for **ANALOG** and **PHASE**. The built-in renderers are compiled in; `instrument_wavetable.c`, `instrument_analog.c` and `instrument_phase.c` interpret their fixed-format records. The web editor provides 32 7-bit sample values for WAVE and reads Analog/Phase factory banks from JSON files under `web/presets/`. Those patch banks are data assets, not C arrays compiled into the firmware. Uploaded active parameters, ADSR and WAVE data are stored in the app's existing settings payload and committed with the settings CRC/header when audio becomes quiet. No new flash region is used.

## Editor workflow

1. Build/install a firmware that includes WAVE (`BARGE_ENABLE_WAVETABLE=1`, the default).
2. Open [web/editor.html](../web/editor.html) in a Web-MIDI-capable browser, grant SysEx permission, and select the FM-1's Barge MIDI input/output.
3. Draw, preset, or import a `barge-wave-v1` JSON waveform containing exactly 32 integers in the range 0–127.
4. Send **Upload WAVE module**. The editor transmits one validated SysEx packet. On success Barge queues the table for the next audio block, switches to WAVE, and persists the data once the voice has become quiet. The editor reports the device's acceptance status.
5. Export the WAVE JSON locally for backup/sharing. There is no sample-rate conversion or audio asset in this v1 format.

For an Analog or Phase preset, select the engine and an entry in the **Runtime Analog / Phase presets** panel, then choose **Load preset into Barge**. The editor validates the JSON record, sends its parameters plus ADSR with SysEx v2, and firmware applies the values at the next block boundary and selects that built-in renderer. The preset's name and entire factory/user bank remain in the editor's JSON data; Barge persists only the currently selected instrument's active values, not a named multi-preset library.

## SysEx protocol v1

SysEx payload excludes the `F0` and `F7` framing bytes:

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0–3 | `7D 42 4D 01` | Non-commercial SysEx ID, `BM` product marker, protocol version 1 |
| 4–35 | 32 values | One-cycle waveform samples, each 0–127; 64 is zero |
| 36 | 1 value | Checksum: sum of payload bytes 0–35 modulo 128 |

Reply payload: `7D 42 4D 01 status checksum`, where status 0 is accepted, 1 is unsupported size/version, 2 checksum failure, 3 WAVE was not compiled in, and 4 invalid samples or a prior update is still pending. The checksum is the sum of reply bytes 0–4 modulo 128. Unknown SysEx remains owned by the existing updater/handshake path.

### Preset/patch upload v2

Payload bytes (again excluding `F0`/`F7`):

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0–3 | `7D 42 4D 02` | Product marker and protocol version 2 |
| 4 | 1 or 2 | Module: 1 ANALOG, 2 PHASE |
| 5 | 11, 17, 8 or 14 | ANALOG legacy/full control count (11/17), or PHASE legacy/full count (8/14) |
| 6–9 | 4 values | Shared ADSR Attack, Decay, Sustain, Release, each 0–127 |
| 10… | count values | Module-local engine and, in the extended counts, modulation controls, each 0–127 |
| last | 1 value | Sum of all preceding payload bytes modulo 128 |

Reply payload is `7D 42 4D 02 module status checksum`. Status is 0 accepted, 1 module/count/length unsupported, 2 checksum failure, 3 module not included in this build, or 4 invalid parameter data. Legacy v2 counts (ANALOG 11, PHASE 8) remain accepted; extended v2 counts add the six shared modulation controls (ANALOG 17, PHASE 14). The protocol version and field order are unchanged. Named preset banks are standard JSON under `web/presets/`; only the chosen data record is transferred, never executable code.

The editor sends a single small frame, not a streamed asset, so v1 avoids partial-write protocols. For larger sample banks, adopt the sibling sample editors' begin/write/finish pattern with chunk sequence numbers, length limits, end-to-end CRC, inactive-slot staging, header-last publication, cancellation/recovery, and flash-busy audio policy. Do not extend this one-frame protocol to multi-kilobyte sample payloads.

## Safety, concurrency and limits

- The uploaded content is never cast to a pointer, executed, or interpreted as instructions. The only runtime behavior is the compiled wavetable oscillator.
- The firmware accepts exactly 32 7-bit samples and checks length, version, value range and checksum before queuing anything. A bad frame leaves the current waveform unchanged.
- Main-loop receipt copies the validated table into an inactive fixed-size staging buffer. The audio callback publishes it into its WAVE instance at a block boundary; render code never reads a buffer while the editor is modifying it.
- WAVE, up to 17 Analog / 14 Phase controls, 4 ADSR values and a loaded marker fit in Barge's versioned B7 settings structure under the existing 256-byte settings limit. B6 and older layouts migrate while preserving engine data; the existing flash writer/header CRC continues to protect persistence, and writes are deferred until silent because settings erase pauses audio.
- Named banks are external JSON; on-device persistence is one current patch per built-in instrument plus one WAVE. This does not provide sample playback, user FX, dynamic code, authentication, or safe untrusted native binaries.
- Host tests emulate the editor's packet and SysEx response. Actual USB-MIDI SysEx transfer, flash persistence across power loss, and browser/device interoperability still require a target FM-1 test.

## Next steps if the prototype is useful

Keep firmware ABI/version and editor JSON version independent. Add list/read/write commands and two-slot atomic storage only after the data types grow beyond the settings budget. For sampled assets, reuse the well-tested sibling-firmware chunked transfer/checksum design after auditing the Barge flash map and recovery story. For new algorithms, keep the renderer in a firmware release and upload only validated parameter/patch data; adding post-build executable modules remains a separate loader, memory-map, security, rollback and update project.
