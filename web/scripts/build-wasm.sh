#!/usr/bin/env bash
# Build the optimized, single-threaded ARENA WASM engine.
# Requires the Emscripten SDK on PATH (run: source ~/emsdk/emsdk_env.sh).
#
# The engine runs on requestAnimationFrame on the main thread, so we use NO
# pthreads / shared memory. This halves the binary and removes the need for
# SharedArrayBuffer + COOP/COEP cross-origin isolation — making the site a
# trivially hostable static bundle.
set -euo pipefail
cd "$(dirname "$0")/.."

emcc ../src/wasm_core.cpp -o src/arctic_wasm.js \
  -std=c++17 -O3 -flto -lembind \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16MB \
  -sWASM_BIGINT=1 -sMALLOC=emmalloc -sFILESYSTEM=0 -sASSERTIONS=0 \
  "-sEXPORTED_FUNCTIONS=['_malloc','_free']" \
  "-sEXPORTED_RUNTIME_METHODS=['wasmMemory']"

echo "Built src/arctic_wasm.{js,wasm}"
ls -la src/arctic_wasm.js src/arctic_wasm.wasm
