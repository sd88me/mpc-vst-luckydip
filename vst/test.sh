#!/usr/bin/env bash
# Lucky Dip: offline x86 tests under ASan/UBSan: the core logic, then the wrapper host test (two instances, params,
# MIDI -> audio, chunk round trip) built from the real engine.
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="$(cd "${MPC_VST:-../vendor/mpc-vst}" && pwd)"   # vendored toolchain (vendor/mpc-vst/VENDORED.md)
python3 gen_params.py
mkdir -p build
cp "$MPC_VST/wrapper/engine.h" build/
g++ -std=gnu++11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -g -I../src ../tests/core_test.cpp -o build/core_test
./build/core_test
"$MPC_VST/tools/test_port.sh" "$PWD/vst.json"
g++ -std=gnu++11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -g -pthread -Ibuild -I../src ../tests/engine_test.cpp ../src/engine.cpp -o build/engine_test
./build/engine_test
