cmake_minimum_required(VERSION 3.16)
project(arctic_wasm)

set(CMAKE_CXX_STANDARD 17)

# Core WASM bindings. The engine is single-threaded (step_frame runs on the
# browser's requestAnimationFrame), so we deliberately do NOT use pthreads or
# shared memory. That keeps the binary small AND removes the need for
# SharedArrayBuffer / cross-origin isolation (COOP+COEP) headers, so the build
# is a trivially hostable static site.
add_executable(arctic_wasm ../src/wasm_core.cpp)

target_compile_options(arctic_wasm PRIVATE -O3 -flto)

# Emscripten link flags: optimized, single-threaded, ES6 module.
target_link_options(arctic_wasm PRIVATE
    "-O3"
    "-flto"
    "-lembind"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sENVIRONMENT=web"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=16MB"
    "-sWASM_BIGINT=1"
    "-sMALLOC=emmalloc"
    "-sFILESYSTEM=0"
    "-sASSERTIONS=0"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['wasmMemory']"
)

# Output where the Vite app imports it from.
set_target_properties(arctic_wasm PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
