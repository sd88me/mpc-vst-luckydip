#!/usr/bin/env bash
# Builds mpc-3.9.1.2.patch (our bytes only) from helper.S/names.S/colours*.S and checks it against the user's own stock MPC binary.
#   make_patch.sh <stock MPC copied from the device>     (never committed or packaged)
# Patch line format:  <hex offset> <new hex>
set -euo pipefail
cd "$(dirname "$0")"
STOCK=${1:?usage: make_patch.sh <stock /usr/bin/MPC>}
STOCK_MD5=592eebc8e1ce0797dc8c98e7002143b8
[ "$(md5sum < "$STOCK" | cut -d' ' -f1)" = $STOCK_MD5 ] || { echo "not the stock 3.9.1.2 MPC"; exit 1; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
asm() { arm-linux-gnueabihf-as -march=armv7-a "$1.S" -o "$T/$1.o"
        arm-linux-gnueabihf-ld -Ttext="$2" --no-dynamic-linker -e _start "$T/$1.o" -o "$T/$1.elf" 2>/dev/null
        arm-linux-gnueabihf-objcopy -O binary "$T/$1.elf" "$T/$1.bin"; }
asm helper 0x2494954; asm names 0x6872980; asm colours 0x6872900; asm colours_jump 0x2494bb4
[ "$(stat -c%s "$T/helper.bin")" = 16 ] && [ "$(stat -c%s "$T/colours.bin")" -le 128 ] && [ "$(stat -c%s "$T/names.bin")" -le 1024 ] || { echo "size"; exit 1; }
rd() { dd if="$STOCK" bs=1 skip=$((0x$1)) count=$2 2>/dev/null | xxd -p | tr -d '\n'; }
# the page tail we write into must be zero in the stock file (colours at 0x6872900, names at 0x6872980)
[ "$(rd 6872900 1024 | tr -d 0)" = "" ] || { echo "page tail 0x6872900 is not zero in this MPC"; exit 1; }
{ echo "# MPC OS 3.9.1.2 drum-layout patch: <hex offset> <new bytes>  (no Akai bytes; the device saves its own originals)"
  echo "stock_md5 $STOCK_MD5"
  echo "2494954 $(xxd -p "$T/helper.bin" | tr -d '\n')"
  echo "2494bb4 $(xxd -p "$T/colours_jump.bin" | tr -d '\n')"
  echo "6872900 $(xxd -p "$T/colours.bin" | tr -d '\n')"
  echo "6872980 $(xxd -p "$T/names.bin" | tr -d '\n')"
  for a in 18ff260 18ffbd8 18ffc84 1900e04 1900e98 19016fc; do
    [ "$(rd $a 4)" = 0810a013 ] || { echo "site $a unexpected"; exit 1; }
    echo "$a 10"; done
} > patch.body
cp "$STOCK" "$T/MPC.new"
grep -v '^[#s]' patch.body | while read o new; do
  printf '%s' "$new" | xxd -r -p | dd of="$T/MPC.new" bs=1 seek=$((0x$o)) conv=notrunc 2>/dev/null; done
PM=$(md5sum < "$T/MPC.new" | cut -d' ' -f1)
{ head -n 2 patch.body; echo "patched_md5 $PM"; tail -n +3 patch.body; } > mpc-3.9.1.2.patch; rm patch.body
echo "patched md5 $PM"; cp "$T/MPC.new" "${PATCHED_OUT:-/dev/null}" 2>/dev/null || true
