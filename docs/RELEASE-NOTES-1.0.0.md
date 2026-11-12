# Lucky Dip 1.0.0

A new screen and support for the MPC Live III. Same plugin as 0.9: your projects, favourites and rejects carry over.

## New
- **New screen** in the familiar candy colours. Tap a sample's name to hear the pad; each tile shows the category it drew.
- **PAD EDIT**: pick the pad on an MPC-style 4x4 grid (a coloured bar under each pad shows the kit at a glance); the category
  keys are grouped as drums, metals, tonal and texture.
- **SETTINGS**: three clear cards (sample source, export folder, library rules); tap a folder field for a list of folders;
  one-tap choices for copy / link and the maximum file size.
- **MPC Live III** (64-bit) support. Drives are found automatically on every unit (internal drive, SD cards, USB sticks), and
  folders show where they are: `Internal / Expansions`, `MYSTICK / Samples`.
- On a Live III, kits export to `MPC Documents/Lucky Dip` by default.

## Fixed
- No sample folders were listed on the MPC Live III.
- Pads played sharp when the project ran at 48 kHz.
- Generating a kit could hold up audio for a moment.

## Note
- The new screen needs MPC OS 3.x. 0.9.1 was also listed for MPC OS 2.x (never tested there); 1.0 is not.

## Install
- **MPC Live III**: `Lucky-Dip-1.0.0-mpc-aarch64.zip`. **Force, MPC Live / Live II / One / X / Key 61**: `Lucky-Dip-1.0.0-mpc-armv7.zip`.
- With the Plugin Manager, or by hand: unzip on the device and run `sh install.sh` (stops MPC: save first). Upgrades 0.9 in place.

Tested on an MPC Live III and an Akai Force (MPC OS 3.9.1).
