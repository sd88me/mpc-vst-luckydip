# Vendored: mpc-vst-plugins build toolchain

Source: https://github.com/sd88me/mpc-vst-plugins, commit `a4b8f861d07153a2b387d7520331f532f7247502`
(`wrapper/`, `tools/`, `adapters/` as committed; nothing changed locally).

Why it is here: Lucky Dip builds with no sibling checkout and no network fetch. `vst/build.sh` and `vst/test.sh` use this
copy by default. To try a newer toolchain, set `MPC_VST=/path/to/mpc-vst-plugins`. To re-vendor, replace these three
folders from a new commit and update the hash above.
