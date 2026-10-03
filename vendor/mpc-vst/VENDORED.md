# Vendored: mpc-vst-plugins build toolchain

Source: https://github.com/sd88me/mpc-vst-plugins, commit `6b71b29b7926118eefc7893ae9b191b860b16ab3`
(`wrapper/`, `tools/`, `adapters/` as committed; nothing changed locally).

Why it is here: Lucky Dip builds with no sibling checkout and no network fetch. `vst/build.sh` and `vst/test.sh` use this
copy by default. To try a newer toolchain, set `MPC_VST=/path/to/mpc-vst-plugins`. To re-vendor, replace these three
folders from a new commit and update the hash above.

## Local change
`wrapper/vst2_wrap.c` has one change over the pinned commit: the `"_refresh"` counter poll in `housekeeping()` (an engine can
ask for a display refresh). Proposed upstream as mpc-vst-plugins PR "wrapper: engines can request a display refresh"; when it
is merged, re-vendor and delete this note.
