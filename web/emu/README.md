# Barge browser simulator

A WebAssembly/AudioWorklet simulator for Barge's current app baseline. Its clock advances when the worklet requests
sound; the app frame, audio callback, panel inputs, display, LED state and settings persistence are driven through a
small fake-platform adapter. The simulator reuses the Felucca emulator's WebAssembly/AudioWorklet approach and its
X0X reference, but it does not compile Felucca's synth engines.

## Build and run

From the Barge repository root:

```sh
sh web/emu/build.sh
python3 -m http.server -d build/emu 8790
```

Open `http://localhost:8790/` in a modern browser, then press **Power on Barge**. The build script uses a local
`emcc` when available; otherwise, it runs the `emscripten/emsdk` Docker image (Docker must be running). A checked-in
generated font header is required, so no JieLi toolchain or AC79 SDK is needed for this simulator build.

## Controls

- Hold the virtual keyboard keys to send key presses.
- Hold the Barge panel buttons; OCT-/OCT+ change octave and the other buttons are available to future app modules.
- Use encoder +/− controls to send detents. The current sine smoke-test app does not use the encoders.
- MASTER scales the simulated audio output.
- The app's one-byte octave setting is saved in browser local storage.

## Scope and limits

This compiles Barge's application callback code for WebAssembly. It is not an AC79 CPU/peripheral emulator and does
not exercise actual DMA, interrupt priority, USB, MIDI transport, the updater, ROM, flash controller or recovery
path. MIDI input is injected into the app-level queue; settings use a small host-side record. Hardware timing,
flash safety and CPU1 behavior must be validated on the FM-1/SMK37 separately.
