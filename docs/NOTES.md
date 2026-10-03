# Lucky Dip: notes

## Verified (offline, 2026-10-03)
- `vst/test.sh` passes under ASan/UBSan: core (classifier examples from the JS doc comments, scan filters, seeded
  assignment, locks, rejects, loudness, WAV/WAVE_FORMAT_EXTENSIBLE decode + resample, XPM export), the real engine
  against a scratch library (scan, generate, decode, pad notes sound, state round trip, reroll, pool override, clear,
  match levels, export), and the mpc-vst wrapper host test (140 params).
- `vst/build.sh` builds `lucky_dip.so` (armhf, highest glibc 2.34; the device has 2.39).
- Skin previewed offline (`studio.py preview`): both pages fit.

## Not verified on a device yet
Load, skin, Q-Links, project save/reload, real library scan time, CPU (`tools/bench.sh`), the drum-layout patch.

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
- The folder pickers of the web UI have no equivalent in a plugin: sample and export folders come from
  `luckydip.conf` (see README). A Library enum listing `/media/*` folders is an obvious next step.

## Open issues
- **Async text does not refresh.** The wrapper only calls `audioMasterUpdateDisplay` after a `setParameter`, so a
  background scan finishing ("Library: N samples") or an export finishing ("Exported OK") shows on the next
  interaction, not by itself. Generate/reroll/clear are synchronous and refresh at once. A fix belongs in the shared
  wrapper (poll an engine "refresh" key in `housekeeping`); not changed here because the wrapper is vendored.
- Decoding a pad is done on a worker thread, so a pad is silent for a moment after Generate.
- The host chunk buffer is 8 KiB: a kit with very long paths drops its later pads from the saved state.
- Per-category pad colours on the MPC pad LEDs are not reachable (see the Machinemodule patch notes): all red.
- The shadow page tinted each pad frame by category and tapping the pill jumped to DETAIL for that pad; neither has a
  plugin equivalent (static skin). Select a pad with the DETAIL stepper.

## Toolchain
`vendor/mpc-vst/` is a pinned copy of mpc-vst-plugins' wrapper/tools (see `VENDORED.md`). The engine includes
`engine.h`, which `vst/build.sh`/`vst/test.sh` copy into `vst/build/` (build_port.sh puts that on the include path).
Keep `uid` (`LkDp`) and `lucky_dip.so` fixed; append parameters only.
