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

- **PADS page**: per pad, the sample name, its category, PLAY, REROLL and LOCK. Q-Links 1-16 are the pad gains.
- **DETAIL page**: the category matrix for the selected pad (tap categories to build the pool it draws from; none =
  the default pool for that pad slot), the pad stepper, gain, lock, clear, reroll, favourite/reject, and the kit
  actions: **Generate All**, **Clear All**, **Normalise** (match levels), **Rescan Library**, **Export Kit**.
- Locked pads keep their sample through Generate/Clear/Reroll. Favourites are drawn about twice as often; rejects are
  never drawn again (library-wide, kept in `prefs.txt`).
- The kit is saved with the project.

### Sample folders and export folder
Edit `luckydip/luckydip.conf` inside the plugin folder (created on first run next to the plugin):
```
root=/media/<card>/My Samples      # repeatable; without any root= the card layout is searched
export_dir=/media/<card>/Expansions/Kits & Patterns
skip_loops=1                       # skip files that look like loops (default)
max_mb=5                           # skip files bigger than this (default: no cap)
```
Without a `root=`, every `/media/<card>/Expansions` and `/media/<card>/Samples` (and `/sdcard/Samples`) is scanned.
Press Rescan after changing it. Exports go to `<export_dir>/LuckyDip-MMDD-HHMMSS/` (the `.xpm`, the samples beside it
and a `MANIFEST.txt`); the Force's own browser loads them from `Expansions/Kits & Patterns`.

## Build
Self-contained: the plugin toolchain from mpc-vst-plugins is vendored in `vendor/mpc-vst/` (`VENDORED.md`).
Needs Docker (arm32v7 via QEMU).
```
vst/test.sh     # offline x86 tests under ASan/UBSan: core logic, engine end to end, plugin wrapper
vst/build.sh    # vst/build/lucky_dip.so + skin/<vendor> - VST - Lucky Dip/ + pluginlist-entry.xml
```
Install and register as any mpc-vst-plugins port (see `vendor/mpc-vst/tools` and that project's docs).

## 16-pad drum layout (advanced, optional)
`release/mpc_patch/` patches the factory MPC OS so Lucky Dip (and Machinemodule) get the 16-pad drum layout instead of
the melodic one. It edits `/usr/bin/MPC`, works only on MPC OS **3.9.1.2** (checked by md5, refuses anything else), is
undone by `uninstall.sh`, and a firmware update removes it. Pad *n* sends note *n-1*, which the plugin accepts. See
`release/mpc_patch/README.md`.

## Credits
The kit-building engine originated in [schwung-kit-builder](https://github.com/sd88me/schwung-kit-builder) and was
ported to [force-kit-builder](https://github.com/sd88me/force-kit-builder), which this follows.

MIT, see `LICENSE`.
