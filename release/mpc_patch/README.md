# MPC OS drum-layout patch (advanced, optional)

Makes MPC show the **16-pad drum layout** for **Lucky Dip** and **Machinemodule** (one table of plugin names; add a
line to `names.S` for another drum plugin) instead of the melodic layout. All 16 pads light red.

**It modifies the factory MPC OS** (`/usr/bin/MPC`). It works on MPC OS **3.9.1.2 only** (one exact binary, md5-checked;
anything else is refused and nothing is changed). A firmware update replaces the binary and removes the patch. Nothing
of Akai's is shipped: `mpc-3.9.1.2.patch` holds only our bytes, offsets and md5s, applied on the device to its own file,
after backing up the original (`/sdcard/MPC-backup/`).

    sh install.sh        # asks you to type PATCH first
    sh uninstall.sh      # restores the original bytes

If MPC does not start after patching, SSH still works: run `uninstall.sh`, or copy the backup in `/sdcard/MPC-backup/`
over `/usr/bin/MPC`. This patch is not byte-identical to Machinemodule's own patch (that one knows one name): a device
patched with the other one must be uninstalled with its own `.patch` file first.

`make_patch.sh <stock MPC copied from your device>` regenerates `mpc-3.9.1.2.patch` from the `.S` sources
(`arm-linux-gnueabihf-as`/`ld`). How the patch works, and what was tried: the Machinemodule repo's
`docs/HANDOFF-mpc-drum-pads.md`.

Status: `names.S` and `helper.S` are assembled and disassembled offline; **the generated patch has not yet been built or
tested on a device.**
