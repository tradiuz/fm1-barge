#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deterministic regression checks for Barge's built-in instruments and FX."""

from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import wave


ROOT = Path(__file__).resolve().parents[1]
FRAMES = 286_464
RATE = 44_100
TAIL_SECONDS = 0.5


def main() -> None:
    for module, expected_count in (("analog", 17), ("phase", 14)):
        bank = json.loads((ROOT / "web" / "presets" / f"{module}.json").read_text())
        if bank.get("format") != "barge-preset-bank-v1" or bank.get("module") != module.upper():
            raise AssertionError(f"invalid {module.upper()} JSON preset bank header")
        for preset in bank.get("presets", []):
            values = preset.get("params", []) + preset.get("adsr", [])
            if len(preset.get("params", [])) != expected_count or len(preset.get("adsr", [])) != 4:
                raise AssertionError(f"{module.upper()} preset {preset.get('name')} has the wrong parameter count")
            if any(not isinstance(value, int) or value < 0 or value > 127 for value in values):
                raise AssertionError(f"{module.upper()} preset {preset.get('name')} contains invalid values")

    with tempfile.TemporaryDirectory(prefix="barge-host-test-") as tmp:
        temp = Path(tmp)
        binary = temp / "barge"
        cc = shlex.split(os.environ.get("CC", "cc"))
        command = cc + [
            "-O2",
            "-ffp-contract=off",
            "-Wall",
            "-Wno-unused-function",
            "-DBARGE_HOST",
            '-DAPP_VER="dev"',
            "-Ifirmware/src",
            "-Ifirmware/gen",
            "-o",
            str(binary),
            "firmware/src/barge.c",
        ]
        subprocess.run(command, cwd=ROOT, check=True)

        renders = []
        for label in ("first", "repeat"):
            output = temp / label
            subprocess.run(
                [str(binary), str(output)],
                cwd=ROOT,
                check=True,
                stdout=subprocess.DEVNULL,
            )
            renders.append(Path(f"{output}.wav").read_bytes())

        first, repeated = renders
        if first != repeated:
            raise AssertionError("identical scripted host renders produced different WAV files")

        wav_path = temp / "first.wav"
        with wave.open(str(wav_path), "rb") as wav:
            if wav.getnchannels() != 2:
                raise AssertionError("expected stereo host output")
            if wav.getsampwidth() != 2:
                raise AssertionError("expected 16-bit host output")
            if wav.getframerate() != RATE:
                raise AssertionError("unexpected host sample rate")
            if wav.getnframes() != FRAMES:
                raise AssertionError("unexpected scripted render length")
            pcm = wav.readframes(wav.getnframes())

        samples = struct.unpack("<" + "h" * (len(pcm) // 2), pcm)
        peak = max(abs(sample) for sample in samples)
        rms = math.sqrt(sum(sample * sample for sample in samples) / len(samples))
        if peak == 0:
            raise AssertionError("scripted render is silent")
        if peak >= 32_767:
            raise AssertionError(f"host output reaches the 16-bit clamp: peak={peak}")

        tail_bytes = int(RATE * TAIL_SECONDS) * 2 * 2
        if any(pcm[-tail_bytes:]):
            raise AssertionError("release did not reach silence in the final 500 ms")

        digest = hashlib.sha256(first).hexdigest()
        print(
            f"host render OK: {FRAMES} frames @ {RATE} Hz, "
            f"peak={peak}, rms={rms:.2f}, silent tail={TAIL_SECONDS:.1f}s, "
            f"repeat SHA-256={digest}"
        )

        script = temp / "analog.script"
        script.write_text("200 keys 128\n700 keys 0\n2500 end\n")
        analog_renders = []
        for label in ("analog-first", "analog-repeat"):
            output = temp / label
            subprocess.run(
                [str(binary), str(output), str(script)],
                cwd=ROOT,
                check=True,
                stdout=subprocess.DEVNULL,
            )
            analog_renders.append(Path(f"{output}.wav").read_bytes())
        if analog_renders[0] != analog_renders[1]:
            raise AssertionError("ANALOG module render was not deterministic")
        with wave.open(str(temp / "analog-first.wav"), "rb") as wav:
            if wav.getnframes() == 0:
                raise AssertionError("ANALOG script produced no audio frames")
            analog_pcm = wav.readframes(wav.getnframes())
        if not any(analog_pcm):
            raise AssertionError("selecting ANALOG and playing a key produced silence")
        print(f"ANALOG module OK: deterministic SHA-256={hashlib.sha256(analog_renders[0]).hexdigest()}")

        control_script = temp / "analog-control.script"
        control_script.write_text("110 enc 2 1\n200 keys 128\n700 keys 0\n2500 end\n")
        control_output = temp / "analog-control"
        subprocess.run(
            [str(binary), str(control_output), str(control_script)],
            cwd=ROOT,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        if Path(f"{control_output}.wav").read_bytes() == analog_renders[0]:
            raise AssertionError("ANALOG encoder control did not affect rendered output")

        adsr_script = temp / "adsr.script"
        adsr_script.write_text("100 btn 4\n110 enc 2 90\n120 btn 4\n200 keys 128\n900 keys 0\n3500 end\n")
        adsr_output = temp / "adsr"
        subprocess.run([str(binary), str(adsr_output), str(adsr_script)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
        if Path(f"{adsr_output}.wav").read_bytes() == analog_renders[0]:
            raise AssertionError("BTN_ENV ADSR controls did not change the Analog render")

        stress_script = temp / "analog-stress.script"
        stress_script.write_text(
            "110 enc 2 1\n111 enc 3 119\n112 enc 4 63\n113 enc 5 37\n"
            "200 keys 18562\n700 keys 0\n3000 end\n"
        )
        stress_output = temp / "analog-stress"
        subprocess.run(
            [str(binary), str(stress_output), str(stress_script)],
            cwd=ROOT,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        stress_wav = Path(f"{stress_output}.wav")
        with wave.open(str(stress_wav), "rb") as wav:
            stress_pcm = wav.readframes(wav.getnframes())
        if not any(stress_pcm):
            raise AssertionError("four-voice ANALOG stress script produced silence")

        analog_full_script = temp / "analog-full.script"
        analog_full_script.write_text(
            "100 enc 2 5\n110 enc 3 110\n120 enc 4 64\n130 enc 5 64\n"
            "140 btn 6\n150 enc 2 96\n160 enc 3 127\n170 enc 4 127\n180 enc 5 127\n"
            "190 btn 6\n200 enc 2 2\n210 enc 3 127\n220 enc 4 64\n200 keys 18562\n"
            "900 keys 0\n3500 end\n"
        )
        analog_full_output = temp / "analog-full"
        subprocess.run([str(binary), str(analog_full_output), str(analog_full_script)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
        if not any(Path(f"{analog_full_output}.wav").read_bytes()):
            raise AssertionError("expanded Analog SYNC/SUB/PWM/noise/SVF/drive feature path was silent")

        switch_script = temp / "analog-switch.script"
        switch_script.write_text("200 keys 128\n400 btn 3\n500 keys 0\n2400 end\n")
        switch_output = temp / "analog-switch"
        subprocess.run(
            [str(binary), str(switch_output), str(switch_script)],
            cwd=ROOT,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        with wave.open(f"{switch_output}.wav", "rb") as wav:
            switch_pcm = wav.readframes(wav.getnframes())
        if not any(switch_pcm):
            raise AssertionError("runtime instrument switch produced no audio")
        tail_bytes = int(RATE * TAIL_SECONDS) * 2 * 2
        if any(switch_pcm[-tail_bytes:]):
            raise AssertionError("runtime switch left audio in the final 500 ms")

        for ordinal, module_name in ((1, "PHASE"), (2, "LOFI"), (3, "NOISE")):
            selected_at = [100 * i for i in range(1, ordinal + 1)]
            note_on = selected_at[-1] + 100
            note_off = note_on + 500
            end = note_off + 1800
            base_lines = [*(f"{when} btn 3" for when in selected_at),
                          f"{note_on} keys 128", f"{note_off} keys 0", f"{end} end"]
            base_script = temp / f"{module_name.lower()}.script"
            base_script.write_text("\n".join(base_lines) + "\n")
            module_renders = []
            for label in ("first", "repeat"):
                output = temp / f"{module_name.lower()}-{label}"
                subprocess.run([str(binary), str(output), str(base_script)], cwd=ROOT,
                               check=True, stdout=subprocess.DEVNULL)
                module_renders.append(Path(f"{output}.wav").read_bytes())
            if module_renders[0] != module_renders[1] or not any(module_renders[0]):
                raise AssertionError(f"{module_name} selection/render was silent or nondeterministic")

            control_lines = [*(f"{when} btn 3" for when in selected_at),
                             f"{selected_at[-1] + 50} enc 2 1", f"{note_on} keys 128",
                             f"{note_off} keys 0", f"{end} end"]
            control_script = temp / f"{module_name.lower()}-control.script"
            control_script.write_text("\n".join(control_lines) + "\n")
            controlled = temp / f"{module_name.lower()}-control"
            subprocess.run([str(binary), str(controlled), str(control_script)], cwd=ROOT,
                           check=True, stdout=subprocess.DEVNULL)
            if Path(f"{controlled}.wav").read_bytes() == module_renders[0]:
                raise AssertionError(f"{module_name} encoder control did not change output")
            print(f"{module_name} module OK")

        phase_full_script = temp / "phase-full.script"
        phase_full_script.write_text(
            "100 btn 3\n110 enc 2 7\n120 enc 3 4\n130 enc 4 90\n140 enc 5 100\n"
            "150 btn 6\n160 enc 2 80\n170 enc 3 1\n180 enc 4 100\n190 enc 5 96\n"
            "200 keys 18562\n900 keys 0\n3500 end\n"
        )
        phase_full_output = temp / "phase-full"
        subprocess.run([str(binary), str(phase_full_output), str(phase_full_script)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
        if not any(Path(f"{phase_full_output}.wav").read_bytes()):
            raise AssertionError("expanded PHASE WAVE2/RING/SUB/FOLD path was silent")

        data_module_script = temp / "wave-module.script"
        data_module_script.write_text("100 wave 0\n200 keys 128\n700 keys 0\n4000 end\n")
        data_module_output = temp / "wave-module"
        data_reply = subprocess.run(
            [str(binary), str(data_module_output), str(data_module_script)],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        wave_render = Path(f"{data_module_output}.wav").read_bytes()
        if "data module reply status=0" not in data_reply.stdout or not any(wave_render):
            raise AssertionError("valid runtime WAVE data module was not accepted and rendered")
        if "settings saved (" not in data_reply.stdout:
            raise AssertionError("runtime WAVE data module was not persisted after silence")

        bad_data_script = temp / "wave-module-bad.script"
        bad_data_script.write_text("100 badwave 0\n200 keys 128\n700 keys 0\n3000 end\n")
        bad_data_output = temp / "wave-module-bad"
        bad_reply = subprocess.run(
            [str(binary), str(bad_data_output), str(bad_data_script)],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        if "data module reply status=2" not in bad_reply.stdout:
            raise AssertionError("bad WAVE checksum was not rejected")
        print("runtime WAVE data module upload, checksum rejection and persistence OK")

        preset_script = temp / "preset.script"
        preset_script.write_text("100 patch 0\n200 keys 128\n900 keys 0\n4000 end\n")
        preset_output = temp / "preset"
        preset_reply = subprocess.run([str(binary), str(preset_output), str(preset_script)], cwd=ROOT,
                                      check=True, capture_output=True, text=True)
        if "preset module reply status=0" not in preset_reply.stdout or not any(Path(f"{preset_output}.wav").read_bytes()):
            raise AssertionError("valid runtime Analog preset was not accepted and rendered")

        bad_preset_script = temp / "preset-bad.script"
        bad_preset_script.write_text("100 badpatch 1\n200 keys 128\n900 keys 0\n2500 end\n")
        bad_preset_output = temp / "preset-bad"
        bad_preset_reply = subprocess.run([str(binary), str(bad_preset_output), str(bad_preset_script)], cwd=ROOT,
                                          check=True, capture_output=True, text=True)
        if "preset module reply status=2" not in bad_preset_reply.stdout:
            raise AssertionError("corrupted runtime Phase preset checksum was not rejected")
        print("runtime Analog/Phase preset data import and checksum validation OK")

        preset_compat_audio = {}
        for module_index, module_name, count_kind in ((0, "ANALOG", "legacy"), (1, "PHASE", "legacy"),
                                   (0, "ANALOG", "extended"), (1, "PHASE", "extended")):
            command_name = "patch" if count_kind == "legacy" else "patchfull"
            preset_compat_script = temp / f"preset-{module_name.lower()}-{count_kind}.script"
            preset_compat_script.write_text(
                f"100 {command_name} {module_index}\n200 keys 128\n700 keys 0\n3500 end\n"
            )
            compat_output = temp / f"preset-{module_name.lower()}-{count_kind}"
            compat_reply = subprocess.run(
                [str(binary), str(compat_output), str(preset_compat_script)], cwd=ROOT,
                check=True, capture_output=True, text=True,
            )
            compat_audio = Path(f"{compat_output}.wav").read_bytes()
            if "preset module reply status=0" not in compat_reply.stdout or not any(compat_audio):
                raise AssertionError(f"{module_name} {count_kind} SysEx v2 preset failed")
            preset_compat_audio[(module_name, count_kind)] = compat_audio
        for module_name in ("ANALOG", "PHASE"):
            if preset_compat_audio[(module_name, "legacy")] == preset_compat_audio[(module_name, "extended")]:
                raise AssertionError(f"{module_name} extended SysEx v2 modulation parameters were not applied")
        print("legacy and extended ANALOG/PHASE SysEx v2 parameter counts OK")

        multivoice_script = temp / "multivoice.script"
        multivoice_script.write_text("200 keys 18562\n700 keys 0\n2500 end\n")
        multivoice_output = temp / "multivoice"
        subprocess.run([str(binary), str(multivoice_output), str(multivoice_script)], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)
        multivoice_render = Path(f"{multivoice_output}.wav").read_bytes()

        fx_script = temp / "fx.script"
        fx_script.write_text("100 btn 2\n110 enc 5 -120\n200 keys 18562\n700 keys 0\n2500 end\n")
        fx_output = temp / "fx"
        subprocess.run([str(binary), str(fx_output), str(fx_script)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
        fx_render = Path(f"{fx_output}.wav").read_bytes()
        if fx_render == multivoice_render or not any(fx_render):
            raise AssertionError("enabled FX did not alter the multivoice render")

        fx_test = temp / "test-fx"
        subprocess.run(cc + ["-O2", "-Wall", "-Wextra", "-Werror", "-Ifirmware/src",
                             "tests/test_fx.c", "-o", str(fx_test)], cwd=ROOT, check=True)
        subprocess.run([str(fx_test)], cwd=ROOT, check=True)

        registry_test = temp / "test-instrument-registry"
        subprocess.run(cc + ["-O2", "-Wall", "-Wno-unused-function", "-Ifirmware/src",
                             "tests/test_instrument_registry.c", "-o", str(registry_test)],
                       cwd=ROOT, check=True)
        subprocess.run([str(registry_test)], cwd=ROOT, check=True)

        adsr_test = temp / "test-adsr"
        subprocess.run(cc + ["-O2", "-Wall", "-Wno-unused-function", "-Ifirmware/src",
                             "tests/test_adsr.c", "-o", str(adsr_test)], cwd=ROOT, check=True)
        subprocess.run([str(adsr_test)], cwd=ROOT, check=True)

        modulation_test = temp / "test-instrument-modulation"
        subprocess.run(cc + ["-O2", "-Wall", "-Wno-unused-function", "-Ifirmware/src",
                             "tests/test_instrument_modulation.c", "-o", str(modulation_test)],
                       cwd=ROOT, check=True)
        subprocess.run([str(modulation_test)], cwd=ROOT, check=True)

        analog_only = temp / "barge-analog-only"
        subprocess.run(
            cc + [
                "-O2", "-ffp-contract=off", "-Wall", "-Wno-unused-function", "-DBARGE_HOST", "-DBARGE_ENABLE_ANALOG=1",
                "-DBARGE_ENABLE_PHASE=0", "-DBARGE_ENABLE_LOFI=0", "-DBARGE_ENABLE_NOISE=0",
                "-DBARGE_ENABLE_WAVETABLE=0",
                '-DAPP_VER="dev"', "-Ifirmware/src", "-Ifirmware/gen", "-o", str(analog_only),
                "firmware/src/barge.c",
            ],
            cwd=ROOT,
            check=True,
        )
        analog_only_output = temp / "analog-only"
        subprocess.run(
            [str(analog_only), str(analog_only_output), str(script)],
            cwd=ROOT,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        if not any(Path(f"{analog_only_output}.wav").read_bytes()):
            raise AssertionError("ANALOG-only build failed to render the fallback instrument")
        unsupported_output = temp / "wave-not-compiled"
        unsupported_reply = subprocess.run(
            [str(analog_only), str(unsupported_output), str(data_module_script)],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        if "data module reply status=3" not in unsupported_reply.stdout:
            raise AssertionError("WAVE-disabled firmware did not reject a WAVE data upload clearly")

        wave_only = temp / "barge-wave-only"
        subprocess.run(
            cc + [
                "-O2", "-ffp-contract=off", "-Wall", "-Wno-unused-function", "-DBARGE_HOST",
                "-DBARGE_ENABLE_ANALOG=0", "-DBARGE_ENABLE_PHASE=0", "-DBARGE_ENABLE_LOFI=0",
                "-DBARGE_ENABLE_NOISE=0", "-DBARGE_ENABLE_WAVETABLE=1",
                '-DAPP_VER="dev"', "-Ifirmware/src", "-Ifirmware/gen", "-o", str(wave_only),
                "firmware/src/barge.c",
            ],
            cwd=ROOT,
            check=True,
        )
        wave_only_output = temp / "wave-only"
        subprocess.run([str(wave_only), str(wave_only_output), str(data_module_script)],
                       cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        if not any(Path(f"{wave_only_output}.wav").read_bytes()):
            raise AssertionError("WAVE-only build failed to serve as the recovery instrument")
        print("ANALOG fallback and WAVE-only recovery builds OK")


if __name__ == "__main__":
    main()