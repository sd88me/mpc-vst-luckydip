#!/usr/bin/env bash
# Lucky Dip: build the VST2 plugin (armhf .so + skin + plugin-list entry) with mpc-vst-plugins' tools.
#   MPC_VST=/path/to/mpc-vst-plugins vst/build.sh       (default: the vendored copy in vendor/mpc-vst)
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="$(cd "${MPC_VST:-../vendor/mpc-vst}" && pwd)"   # vendored toolchain (vendor/mpc-vst/VENDORED.md)
python3 gen_params.py; python3 gen_art.py; python3 gen_layout.py
mkdir -p build
cp "$MPC_VST/wrapper/engine.h" build/        # the engine ABI; build_port.sh puts vst/build on the include path
"$MPC_VST/tools/build_port.sh" "$PWD/vst.json"
