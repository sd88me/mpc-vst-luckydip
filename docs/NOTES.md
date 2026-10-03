# Lucky Dip: notes

## Verified (offline, 2026-10-03)
- `vst/test.sh` passes under ASan/UBSan: core (classifier examples from the JS doc comments, scan filters, seeded
  assignment, locks, rejects, loudness, WAV/WAVE_FORMAT_EXTENSIBLE decode + resample, XPM export), the real engine
  against a scratch library (scan, generate, decode, pad notes sound, state round trip, reroll, pool override, clear,
  match levels, export), and the mpc-vst wrapper host test (140 params).
- `vst/build.sh` builds `lucky_dip.so` (armhf, highest glibc 2.34; the device has 2.39).
- Skin: candy look (slot-machine banner, per-pad candy tiles, pill buttons) on the browser renderer; pads split over PADS 1-8, PADS 9-16 and DETAIL; previewed offline.

## Device (Force, MPC OS 3.9.1.2, 2026-10-03)
- `tools/bench.sh`: PASS (worst p99 1.6%, worst block 2.9% of the 2902 us budget, with an empty kit).
- Deployed to `/sdcard/Synths/sd88me - VST - Lucky Dip/`, registered in `pluginList-arm`; force_shadow still in MPC's environ.
- Drum-layout patch: tested on this Force with a private copy (patched md5 31ef0968...). That copy was a mistake: the
  patch is shared in mpc-vst-plugins `tools/mpc_patch/`; Lucky Dip's name was added there (PR #142, patched md5
  7cf96599...) and the private copy removed from this repo. The device moves to the shared script next.

## Not verified on a device yet (user test pending)
Plugin loads, skin, Q-Links, 16-pad drum layout with pads sounding, library scan time on the real card, Generate/play,
project save/reload, export loading in the Force's browser.

## Decisions and differences from force-kit-builder
- **Per-pad pool** is a set of categories (a bitmask, the shadow DETAIL matrix) as in the shadow page; the web UI's
  single-category picker is the one-bit case.
- **XPM pad colours**: the JS exporter replaced the first `"value0": <digits>` in the template, which is the program
  *Type* block's `value0: 2`, not pad 1. Here only `pads.value0..15` change.
- **Gain is not written to the XPM** (neither was it in the JS exporter). Match Levels / pad gain affect playback in
  the plugin only. Worth a follow-up (the layer volume field).
- **AIFF** decodes for playback; the XPM still gets the `.aif` copy and a warning (the MPC loads `<name>.wav`).
- Reject does not reroll the pad by itself (as in the web UI); press Reroll.
- Pads are one-shots (note-off ignored), one voice per pad, retrigger restarts. At most 15 s per pad is held in memory.
- The web UI's folder pickers have no equivalent in a plugin (no text entry or file browser in a skin). The SETTINGS
  page has two-level steppers (Folder, then Inside, with +-10 skips) for the source and the export folder, over folders
  discovered on the scanner thread, saved in the chunk; `luckydip.conf` still sets what "Default" means. Flat lists
  were too long (every Expansions pack).
- Export "Link to originals" = symlinks beside the `.xpm`. Real Force `.xpm` files leave `<SampleFile>` empty and the
  MPC finds samples by name beside the file, so a path in `<SampleFile>` was not tried. **Untested on hardware: whether
  the MPC browser/loader follows symlinks.** Copy mode is the verified path.
- Skip Loops and Max File Size are settings (state + conf defaults), applied on Rescan.
- The scan classifies with the root folder's own name as a folder component (the JS only saw names below a root), so a
  chosen `Kicks` folder classifies as kick. Exported `LuckyDip-*` folders are skipped when scanning.
- A scan request while one of this instance's scans runs is queued (a project load sets the saved source right after
  create() started the first scan). Instances share one library: two instances with different sources fight over it.

## Text refresh (2026-10-04)
The engine exposes a lock-free `"_refresh"` counter (bumped by scan/export status changes, a failed sample decode and
FOLLOW moving the selection). The wrapper polls it and schedules `audioMasterUpdateDisplay`, so that text no longer waits for a touch.
This needs the wrapper change in mpc-vst-plugins PR #157 (also applied to `vendor/mpc-vst/`). Not yet verified on a device.

## Open issues
- ~~Async text does not refresh~~ (fixed by the `_refresh` counter, see above; pending device check).
- Decoding a pad is done on a worker thread, so a pad is silent for a moment after Generate.
- The host chunk buffer is 8 KiB: a kit with very long paths drops its later pads from the saved state.
- Per-category pad colours on the MPC pad LEDs are not reachable (see the Machinemodule patch notes): all red.
- The shadow page tinted each pad frame by category and tapping the pill jumped to DETAIL for that pad; neither has a
  plugin equivalent (static skin). Select a pad with the DETAIL stepper.

## Toolchain
`vendor/mpc-vst/` is a pinned copy of mpc-vst-plugins' wrapper/tools (see `VENDORED.md`). The engine includes
`engine.h`, which `vst/build.sh`/`vst/test.sh` copy into `vst/build/` (build_port.sh puts that on the include path).
Keep `uid` (`LkDp`) and `lucky_dip.so` fixed; append parameters only.
