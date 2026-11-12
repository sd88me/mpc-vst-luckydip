#!/usr/bin/env bash
# Offline x86 test under ASan/UBSan (no device): the native front end + the real engine, driven by tests/host_test.cpp. Must print OK.
# LD_NAME / LD_UIDS: a beta build's name and uid (default: the release's).
set -euo pipefail
cd "$(dirname "$0")/.."
SDK="${MPC_SDK:-$HOME/mpc-310-private/mpc-plugin-builder-sdk}"
python3 tools/gen_params.py >/dev/null
[ -f tests/data/legacy-0.9.2.chunk ] || tests/make_legacy_chunk.sh >/dev/null
T=build/test; mkdir -p "$T"
S="-fsanitize=address,undefined -fno-sanitize-recover=undefined -O1 -g -std=c++17 -pthread"
g++ $S -Wall -DLD_TEST -DLD_EXPECT_NAME="\"${LD_NAME:-Lucky Dip}\"" -DLD_EXPECT_UID="\"${LD_UIDS:-LkDp}\"" -I"$SDK/framework" -Isrc \
  -o "$T/ldtest" src/LuckyDipPlugin.cpp src/engine.cpp tests/host_test.cpp -lm
pf="$(mktemp -d)"; trap 'rm -rf "$pf"' EXIT
cp plugin.json "$pf/"
MPC_PLUGIN_FOLDER="$pf" ASAN_OPTIONS=detect_leaks=1 "$T/ldtest" tests/data/legacy-0.9.2.chunk
