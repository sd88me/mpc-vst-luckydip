#!/usr/bin/env bash
# The catalog-shaped release zips, from the release build (build/"sd88me - VST - Lucky Dip", made by ./build.sh):
#   dist/Lucky-Dip-<ver>-mpc-armv7.zip, dist/Lucky-Dip-<ver>-mpc-aarch64.zip   one per CPU, made by mpc-vst-plugins' tools/release.py
# As 0.9, the .so is lucky_dip.so at the folder root, so the installer upgrades a 0.9 install in place; luckydip/ (sample index,
# favourites and rejects, luckydip.conf) is user data, kept on upgrade and uninstall. MPC_VST: an mpc-vst-plugins checkout.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
MPC_VST="${MPC_VST:?set MPC_VST to an mpc-vst-plugins checkout with tools/release.py}"
[ -f "$MPC_VST/tools/release.py" ] || { echo "need mpc-vst-plugins with tools/release.py (MPC_VST)" >&2; exit 1; }
DIR="sd88me - VST - Lucky Dip"; SRC="$here/build/$DIR"
[ -f "$SRC/VST/luckydip-arm32.so" ] || { echo "missing $SRC: run ./build.sh first" >&2; exit 1; }
VER=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$SRC/plugin.json")
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); sys.exit(0 if m["uid"]=="LkDp" and "Beta" not in m["name"] else "not the release identity")' "$SRC/plugin.json"
out="$here/dist"; mkdir -p "$out"
for pair in armv7:arm32:file32 aarch64:arm64:file64; do
  IFS=: read -r cpu so key <<<"$pair"
  st="$here/build/catalog/$cpu"; rm -rf "$st"; mkdir -p "$st/$DIR"
  cp -r "$SRC/Plugin Skins" "$SRC/version.xml" "$st/$DIR/"
  python3 - "$SRC/plugin.json" "$st/$DIR/plugin.json" "$key" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
m.pop("file32", None); m.pop("file64", None)
m[sys.argv[3]] = "lucky_dip.so"
json.dump(m, open(sys.argv[2], "w"), indent=2)
PY
  cp "$SRC/VST/luckydip-$so.so" "$st/lucky_dip.so"
  printf '<PLUGIN name="Lucky Dip" descriptiveName="Lucky Dip" format="VST" category="Synth" manufacturer="sd88me" version="%s" file="lucky_dip.so" uid="4c6b4470" isInstrument="1" fileTime="0" infoUpdateTime="0" numInputs="0" numOutputs="2" isShell="0"/>\n' "$VER" > "$st/entry.xml"
  python3 "$MPC_VST/tools/release.py" --so "$st/lucky_dip.so" --skin "$st/$DIR" --entry "$st/entry.xml" --version "$VER" \
    --user-data luckydip \
    --repo sd88me/mpc-vst-luckydip --license MIT --id lucky-dip \
    --about "Random drum-kit builder: scans your sample folders, fills 16 pads at random, lock / reroll / export the kit as an MPC drum program; native MPC screen." \
    -o "$st/dist"
  z=$(ls "$st/dist"/*.zip); cp "$z" "$out/Lucky-Dip-$VER-mpc-$cpu.zip"
done
ls -la "$out"/Lucky-Dip-"$VER"-mpc-*.zip
