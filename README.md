# Lucky Dip

**A random drum-kit builder and inspiration machine for Akai MPC and Force.**

Your sample library is bigger than you remember. Lucky Dip reaches in, pulls out sixteen sounds, drops them on the pads and
lets you play them straight away. Don't like one? Reroll it. Love one? Lock it and dip again. When the kit feels right,
export it as a normal MPC drum program.

It's a way to **find the samples you forgot you had**: the pack you downloaded and never opened, the odd hit buried three
folders deep, the old recording you made years ago. Every dip is a surprise, and some of them are good.
<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/773cbf1b-a9f9-4d57-8bbe-dfca866def60" />
<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/b4d44e0b-d017-4bec-8e28-928150d2f30d" />
Lucky Dip is a native plugin (VST2) for MPC OS standalone devices. It was built and tested on an **Akai Force** (MPC OS
3.9.1.x); it should load on any MPC OS standalone unit with the same plugin host, but those haven't been tested yet.
Since 0.9.1 the skin is also written in the MPC OS 2.x shape (checked against MPC OS 2.15.1's own skins, library needs glibc 2.30),
so the catalog lists it as "2.x + 3.x", but it has **not been tried on a 2.x unit yet**; reports from MPC Live / One / X users are welcome.

## What it does
- **Scans your sample folders** and sorts every sample into one of 23 categories (kick, snare, rim, clap, hat, closed hat,
  open hat, tom, conga, percussion, crash, ride, cymbal, fx, glitch, vox, bass, synth, stab, chord, lead, pad, other) from the
  folder and file names. No tagging by hand.
- **Fills 16 pads at random.** Pad 1 draws a kick, pad 2 a snare, pad 5 a hat and so on, like a real drum kit. Pads 12-15 draw
  from everything else (vocals, bass, synths, chords, glitches...) for the surprises.
- **Plays the kit.** Hit the pads, or play it from the sequencer, and hear it before you commit to anything.
- **Lets you steer.** Lock the pads you like, reroll the ones you don't, change what a pad is allowed to pick from, mark
  favourites (more likely to come up) and rejects (never again).
- **Matches levels** so a loud sample doesn't swamp a quiet one.
- **Exports an MPC drum program** (`.xpm`) that you can load like any other kit.

## Requirements
- An MPC OS standalone device (Akai Force tested), and samples on its internal drive or a card.
- Root SSH access to install (the installer edits the device's plugin list and restarts MPC).
- Samples as **WAV** (8/16/24-bit, 32-bit, 32-bit float; mono or stereo; any sample rate) or **AIFF**. Compressed WAV isn't
  supported. Very long files are cut to 15 seconds on the pad (Lucky Dip is for one-shots).

## Install
Needs root SSH access to the device (the installer edits the plugin list and restarts MPC; installing plugins this way is
unofficial, so back up first and use it at your own risk). First-generation MPC OS standalone devices (32-bit ARM: Force,
MPC Live / Live II, One, X, Key 61); tested on a Force.
1. Download `Lucky-Dip-<version>-mpc-armv7.zip` from this repo's **Releases** page and unzip it.
2. Copy the folder to the device and run the installer: `scp -r Lucky-Dip-<version> root@<device-ip>:/tmp/`, then
   `ssh root@<device-ip> sh /tmp/Lucky-Dip-<version>/install.sh`. It stops MPC (**save your project first**), copies the plugin
   into `/sdcard/Synths`, backs up `MPC.settings`, adds the plugin and starts MPC again. Running it again upgrades in place and
   keeps your own files (`luckydip/`). `uninstall.sh` removes it.
3. Add a track and choose **Lucky Dip** as its plugin.

After updating to a new version, remove Lucky Dip from every track (or reopen the project) before judging the new one:
MPC keeps a plugin loaded while any instance of it exists.

## Quick start
1. Add a **Lucky Dip** track. The first time, it scans your drive in the background. The banner shows the sample count when it's
   done.
2. Press **GENERATE ALL**. Sixteen samples land on the pads, and the names appear on the tiles.
3. Play the pads (or press **PLAY** on a tile).
4. Don't like a pad? **REROLL** it. Like one? **LOCK** it, then **GENERATE ALL** again: locked pads keep their sample.
5. Happy with the kit? Go to **PAD EDIT** and press **EXPORT KIT**. Your new program appears in the Program browser.

Pads answer MIDI notes 0-15 (the 16-pad drum layout) and 36-51, so they work from the pads, a keyboard or a sequence.

## The pages

### PADS 1-8 and PADS 9-16
One tile per pad: the sample name, its category, a **LOCK** key, **PLAY**, **REROLL** and a **GAIN** knob (Q-Links 1-8 on each
page turn the eight gains). **GENERATE ALL** is in the banner, next to the status line.

### PAD EDIT
Everything about one pad. Choose the pad with the arrows (or just play it: with **FOLLOW** on, the page follows the last pad you
played; turn it off to edit one pad while playing others).
- **LOCK**, **CLEAR**, **REROLL**, **PLAY**, and the **GAIN** knob for that pad.
- **FAV**: mark the sample as a favourite. Favourites are twice as likely to come up in future dips, on any kit.
- **REJECT**: never pick this sample again (until you change your mind in `prefs.txt`, see below). Then **REROLL** for a
  new one.
- **CATEGORY**: which kinds of sample this pad may draw from. The lit keys show the pad's current pool, which starts as that
  pad's usual categories. Tap to add or remove categories, for example make pad 1 draw kicks *and* bass.
- **KIT**: **GENERATE ALL** (fill every unlocked pad), **CLEAR ALL** (empty every unlocked pad), **NORMALISE** (match
  levels), **RESCAN LIBRARY** and **EXPORT KIT**.

### SETTINGS
Where Lucky Dip looks, where it saves, and how it behaves. Changes are saved with your project.

| Setting | What it does |
|---|---|
| **SAMPLE SOURCE: FOLDER** | The place to draw samples from. `DEFAULT (AUTO)` means the `Expansions` and `Samples` folders on every card (and `/sdcard/Samples`). Or pick any folder directly under a card. |
| **SAMPLE SOURCE: INSIDE** | Optionally narrow it to one folder inside that location, for example a single pack. `-10` / `+10` skip along a long list. |
| **EXPORT FOLDER** | Where exported kits are saved, chosen the same way. The default is the Force's own `Expansions/Kits & Patterns` folder (where it browses for kits) if that exists, else a `kits` folder inside the plugin's `luckydip` folder. |
| **SAMPLES** | `COPY SAMPLES` puts a copy of every sample next to the exported program, so the kit is self-contained. `LINK TO ORIGINALS` makes links instead, so nothing is duplicated, but the originals must stay where they are (and the card must support links, otherwise it copies). |
| **RESCAN** | Rebuilds the sample list. Press it after changing the source, `SKIP LOOPS` or `MAX FILE SIZE`. The list is also saved between sessions, so startup is quick. |
| **NO DUPLICATES** | A sample is used on only one pad of a kit. If a pad's pool runs out of unused samples it may repeat one rather than stay empty. Turn it off to allow repeats. |
| **UNLOCK ALL** | Unlocks every pad. |
| **SKIP LOOPS** | Leaves out files whose names look like loops ("loop", "Lp", a BPM tag such as `120 bpm` or `[120]`) and whole folders named like loops ("Clips & Loops"). On by default. |
| **MAX FILE SIZE** | Leaves out files bigger than this (Off, 1, 2, 5, 10 or 20 MB). Keeps long recordings and loops out of a drum kit. |
| **Library count** | How many samples were found. |

## Good to know
- **Your kit is saved with the project**: the samples (by location), locks, gains, category choices and settings. Samples
  aren't copied into the project, so if you move or delete one, its tile shows `!` before the name.
- **Gain and NORMALISE** change how the pads sound in the plugin. An exported program uses each sample at its own level.
- **Exported programs** are standard 128-pad MPC drum programs with your 16 samples on pads 1-16, named
  `LuckyDip-MMDD-HHMMSS`. Pad colours follow the category.
- **Pads are one-shots**: no pitch, envelope or choke groups; hitting a pad again restarts it.
- **Several Lucky Dip tracks share one sample list.** If they use different sources, the last rescan wins.
- **How samples are sorted**: by the folder names above the file first (the deepest matching one wins), then the file name. A
  folder called `Kicks`, or a file called `punchy_kick_01.wav`, is a kick. Unnamed folders end up in "other", which only the
  catch-all pads draw from.

## Files and the config file
On first load, Lucky Dip creates a `luckydip` folder inside its plugin folder (`Synths/sd88me - VST - Lucky Dip/luckydip/`):
- `index.cache`: the saved sample list (delete it to force a fresh scan).
- `prefs.txt`: your favourites (`F`) and rejects (`R`), one path per line. Edit it to undo a reject.
- `luckydip.conf` (optional, create it yourself): sets what `DEFAULT (AUTO)` means, for places the SETTINGS page can't reach.
  ```
  root=/media/<card>/My Samples      # repeatable: every root= line is scanned
  export_dir=/media/<card>/My Kits
  skip_loops=1
  max_mb=5
  ```
- `kits/`: only used when there's no `Kits & Patterns` folder.

## 16-pad drum layout (optional, advanced)
By default MPC treats a plugin track as a keyboard. A separate, optional patch from
[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (`tools/mpc_patch/`) makes MPC show the proper 16-pad drum layout
for Lucky Dip (and a few other drum plugins). It modifies the device's `MPC` program, works only on MPC OS **3.9.1.2**, is
undone by its own uninstall, and a firmware update removes it. Read its README first. Lucky Dip works fine without it.

## Building from source
Self-contained: the plugin toolchain is vendored in `vendor/mpc-vst/` (see `VENDORED.md` there). Needs Docker (arm32v7 via QEMU).
```
vst/test.sh     # offline tests under ASan/UBSan: core logic, the engine end to end, the plugin wrapper
vst/build.sh    # vst/build/lucky_dip.so, the skin folder and the plugin-list entry
```
More detail, design decisions and open issues are in `docs/NOTES.md`; planned work is in `docs/ROADMAP.md`.

## Credits
The kit-building engine (sample classification, seeded random assignment, loudness matching, MPC program export) originated in
[schwung-kit-builder](https://github.com/sd88me/schwung-kit-builder) and was ported to
[force-kit-builder](https://github.com/sd88me/force-kit-builder), which Lucky Dip follows. Key text uses Titillium Web (SIL
Open Font License). Built on [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins).

MIT licensed, see `LICENSE`.
