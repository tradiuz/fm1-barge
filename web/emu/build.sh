#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the Barge app and host-side hardware adapter to standalone WebAssembly.
# Run ./build.sh once first if the generated font header is not present.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/emu"
mkdir -p "$OUT"
if [ -f "$ROOT/firmware/gen/felucca_font.h" ]; then
    :
else
    echo "barge emu: missing firmware/gen/felucca_font.h" >&2
    echo "barge emu: generate the font header with the project's normal build setup first" >&2
    exit 1
fi
set -- -O2 -ffp-contract=off -std=gnu11 -w -DBARGE_WEB -I"$ROOT/firmware/src" -I"$ROOT/firmware/gen" \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=16777216 -sFILESYSTEM=0 \
    -sERROR_ON_UNDEFINED_SYMBOLS=1 -o "$OUT/barge.wasm" "$ROOT/web/emu/barge_web.c"
if command -v emcc >/dev/null 2>&1; then
    emcc "$@"
else
    IMAGE=${BARGE_EMSDK_IMAGE:-docker.io/emscripten/emsdk:latest}
    if command -v podman >/dev/null 2>&1; then
        podman --storage-opt overlay.ignore_chown_errors=true run --rm --platform linux/amd64 \
            -v "$ROOT:/work" -w /work "$IMAGE" emcc \
            -O2 -ffp-contract=off -std=gnu11 -w -DBARGE_WEB -Ifirmware/src -Ifirmware/gen \
            --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=16777216 -sFILESYSTEM=0 \
            -sERROR_ON_UNDEFINED_SYMBOLS=1 -o build/emu/barge.wasm web/emu/barge_web.c
    else
        docker info >/dev/null 2>&1 || { echo "barge emu: install Emscripten or start Docker" >&2; exit 1; }
        docker run --rm --platform linux/amd64 -v "$ROOT:/work" -w /work "$IMAGE" emcc \
            -O2 -ffp-contract=off -std=gnu11 -w -DBARGE_WEB -Ifirmware/src -Ifirmware/gen \
            --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=16777216 -sFILESYSTEM=0 \
            -sERROR_ON_UNDEFINED_SYMBOLS=1 -o build/emu/barge.wasm web/emu/barge_web.c
    fi
fi
cp "$ROOT/web/emu/index.html" "$OUT/index.html"
cp "$ROOT/web/emu/worklet.js" "$OUT/worklet.js"
printf 'barge emu: %s (%s bytes)\n' "$OUT" "$(wc -c < "$OUT/barge.wasm" | tr -d ' ')"
