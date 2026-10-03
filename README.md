# Lucky Dip

A random 16-pad drum-kit builder that runs as a native VST2 plugin on Akai MPC OS standalone devices (built and
designed on a Force). It scans your sample library, sorts samples into 23 categories from folder and file names,
randomly fills the 16 pads, plays the kit, and exports it as an MPC `.xpm` drum program. It is a port of
[force-kit-builder](https://github.com/sd88me/force-kit-builder) (same classifier, assignment, loudness matching and
exporter logic, in C++).

Status: first port. The logic and the offline tests are done; the skin and the device behaviour still need a hardware
pass (see `docs/NOTES.md`).

## Using it
Insert **Lucky Dip** as a track instrument. Pads answer MIDI notes 0-15 (the 16-pad drum layout) and 36-51.

- **PADS 1-8 / PADS 9-16**: per pad, the sample name, its category, PLAY, REROLL, LOCK and a GAIN knob (Q-Links 1-8).
- **DETAIL page**: the category matrix for the selected pad (tap categories to build the pool it draws from; none =
  the default pool for that pad slot), the pad stepper, gain, lock, clear, reroll, favourite/reject, and the kit
  actions: **Generate All**, **Clear All**, **Normalise** (match levels), **Rescan Library**, **Export Kit**.
- Locked pads keep their sample through Generate/Clear/Reroll. Favourites are drawn about twice as often; rejects are
  never drawn again (library-wide, kept in `prefs.txt`).
- The kit is saved with the project.

### Sample folders and export folder (SETTINGS page)
- **Sample source**: two steppers. **Folder** is *Default (auto)* or a folder directly under a card (or on `/sdcard`);
  **Inside** is the whole folder or one folder in it, such as a single pack in `Expansions` (`-10`/`+10` skip along a
  long list). Press **Rescan** to apply. Choosing a category folder (say a pack's `Kicks`) works: the folder's own
  name is used to classify.
- **Export folder**: the same two steppers. Kits are saved as `<folder>/LuckyDip-MMDD-HHMMSS/` (the `.xpm` and a
  `MANIFEST.txt`). **Samples**: *Copy samples* puts the audio in the kit folder (always works); *Link to originals*
  makes symbolic links there instead, so nothing is duplicated, but the originals must stay where they are, and the
  card must support links (otherwise it copies). Lucky Dip's own exported kits are never scanned as source material.
- **Library**: the sample count, **Rescan**, **No Duplicates** (a sample is used on only one pad of a kit; if a pad's
  pool runs out it may repeat one), **Unlock All**, **Skip Loops** (files that look like loops are left out) and
  **Max File Size** (bigger files are left out). Loop and size changes apply on the next Rescan.
- All of these are saved with the project.

*Default (auto)* or one folder found on your cards: the folders directly under each
  `/media/<card>/`, every pack inside an `Expansions` folder, and the top level of `/sdcard`. Press **Rescan** after
  changing it. Choosing a category folder (say a pack's `Kicks`) works: the folder's own name is used to classify.
- **Export folder** (stepper): same list. Kits are saved as `<folder>/LuckyDip-MMDD-HHMMSS/` (the `.xpm`, the samples
  beside it and a `MANIFEST.txt`). Lucky Dip's own exported kits are never scanned as source material.
- **Library** shows the sample count; **No Duplicates** and **Unlock All** live here too.
- Both choices are saved with the project.

*Default (auto)*: scan `/media/<card>/Expansions` and `/media/<card>/Samples` (and `/sdcard/Samples`) and export to
`/media/<card>/Expansions/Kits & Patterns` (the folder a Force browses kits from), else `<plugin folder>/luckydip/kits`.
For anything the stepper can't reach, edit `luckydip/luckydip.conf` in the plugin folder (it sets what *Default* means):
```
root=/media/<card>/My Samples      # repeatable
export_dir=/media/<card>/My Kits
skip_loops=1                       # skip files that look like loops (default)
max_mb=5                           # skip files bigger than this (default: no cap)
```

## Build
Self-contained: the plugin toolchain from mpc-vst-plugins is vendored in `vendor/mpc-vst/` (`VENDORED.md`).
Needs Docker (arm32v7 via QEMU).
```
vst/test.sh     # offline x86 tests under ASan/UBSan: core logic, engine end to end, plugin wrapper
vst/build.sh    # vst/build/lucky_dip.so + skin/<vendor> - VST - Lucky Dip/ + pluginlist-entry.xml
```
Install and register as any mpc-vst-plugins port (see `vendor/mpc-vst/tools` and that project's docs).

## 16-pad drum layout (advanced, optional)
The shared, optional MPC OS patch in mpc-vst-plugins (`tools/mpc_patch/`) makes MPC show the 16-pad drum layout for the
plugins in its name table, **Lucky Dip** included. It edits `/usr/bin/MPC`, works only on MPC OS **3.9.1.2** (checked by
md5, refuses anything else), is undone by its `uninstall`, and a firmware update removes it. Pad *n* sends note *n-1*,
which this plugin accepts. Lucky Dip's name is in the table from mpc-vst-plugins PR #142; use a copy of the script from
that PR or later.

## Credits
The kit-building engine originated in [schwung-kit-builder](https://github.com/sd88me/schwung-kit-builder) and was
ported to [force-kit-builder](https://github.com/sd88me/force-kit-builder), which this follows.

MIT, see `LICENSE`.
