#!/usr/bin/env bash
# Build Lucky Dip 1.0 with the MPC plugin builder (tools/build.py, zig): native test build + arm32 (Force, MPC Gen1) + arm64 (Live III and
# other 64-bit units), into build/"sd88me - VST - Lucky Dip"/. The 0.9 wrapper build is still vst/build.sh.
#   ./build-sdk.sh          build
#   ./build-sdk.sh test     build, the offline ASan/UBSan host test (tests/run.sh) and the builder's host_sim
#   ./build-sdk.sh beta     a "Lucky Dip 2 Beta" (uid LkDB) build that installs next to the release: build/beta-project/build/...
# MPC_SDK: the plugin builder checkout.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
SDK="${MPC_SDK:-$HOME/mpc-310-private/mpc-plugin-builder-sdk}"
[ -f "$SDK/tools/build.py" ] || { echo "need the plugin builder (MPC_SDK)" >&2; exit 1; }
python3 "$here/tools/gen_params.py"
[ -f "$here/tools/make_tui.py" ] && python3 "$here/tools/make_tui.py"

# The arm32 .so must load on the Force (MPC OS 3.9, glibc 2.29 or older): fail if it asks for a newer glibc symbol.
glibc_check() {
  local so="$1" max
  max="$(readelf -V "$so" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -n 1)"
  echo "  $(basename "$so"): newest glibc symbol $max"
  [ "$(printf '%s\n2.29\n' "$max" | sort -V | tail -n 1)" = 2.29 ] || { echo "$so needs glibc $max > 2.29 (the Force)" >&2; exit 1; }
}

if [ "${1:-}" = beta ]; then
  stage="$here/build/beta-project"; rm -rf "$stage"; mkdir -p "$stage"
  ln -s "$here/src" "$stage/src"; [ -d "$here/skins" ] && ln -s "$here/skins" "$stage/skins"
  python3 - "$here/plugin.json" "$stage/plugin.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
m.update(name="Lucky Dip 2 Beta", uid="LkDB", file32="VST/luckydip2beta-arm32.so", file64="VST/luckydip2beta-arm64.so")
json.dump(m, open(sys.argv[2], "w"), indent=2)
PY
  python3 "$SDK/tools/build.py" "$stage" --no-ui --define 'LD_NAME="Lucky Dip 2 Beta"' --define "LD_UID='L','k','D','B'"
  glibc_check "$stage/build/sd88me - VST - Lucky Dip 2 Beta/VST/luckydip2beta-arm32.so"
  exit
fi
python3 "$SDK/tools/build.py" "$here" --no-ui
glibc_check "$here/build/sd88me - VST - Lucky Dip/VST/luckydip-arm32.so"
if [ "${1:-}" = test ]; then
  "$here/tests/run.sh"
  scratch="$(mktemp -d)"; trap 'rm -rf "$scratch"' EXIT
  cp "$here/plugin.json" "$scratch/"
  MPC_PLUGIN_FOLDER="$scratch" "$SDK/.sdk-tools/host_sim" "$here/build/native/lucky-dip.so" test --notes 36,37,38 --wav "$here/build/test.wav"
fi
