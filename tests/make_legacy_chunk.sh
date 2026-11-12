#!/usr/bin/env bash
# tests/data/legacy-0.9.2.chunk: the chunk a project saved with the released 0.9.2 build holds, made by that build's own engine
# (git tag v0.9.2) driven by tests/legacy_dump.cpp against a small library at a fixed path that tests/host_test.cpp recreates.
set -euo pipefail
cd "$(dirname "$0")/.."
LIB=/tmp/luckydip-legacy-lib
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
git archive v0.9.2 src vendor/mpc-vst/wrapper/engine.h | tar -x -C "$w"
cp "$w/vendor/mpc-vst/wrapper/engine.h" "$w/src/"
g++ -std=gnu++11 -O1 -w -pthread -I"$w/src" -o "$w/dump" tests/legacy_dump.cpp "$w/src/engine.cpp"
g++ -std=c++17 -O1 -o "$w/mklib" -DMKLIB_ONLY tests/host_test.cpp
"$w/mklib" "$LIB"
mkdir -p "$w/data" tests/data
printf 'root=%s\n' "$LIB" > "$w/data/luckydip.conf"
"$w/dump" "$LIB" "$w/data" > tests/data/legacy-0.9.2.chunk
echo "tests/data/legacy-0.9.2.chunk: $(wc -c < tests/data/legacy-0.9.2.chunk) bytes"
