# Lucky Dip: roadmap

Parked ideas, roughly in the order they'd be worth doing.

## Pad tiles that change colour with the category (parked)
The pad tiles on the PADS pages could take the colour of the category that was picked (kick red, snare/clap yellow, hats
orange, toms/percussion aqua, fx pink, and so on), so a kit reads at a glance. Needs:
- a per-pad choice (one option per colour family) that MPC draws as a background picture (`picture` widget, one image per
  option) in place of today's fixed tile art;
- the wrapper to push that choice to the host when the engine changes it by itself (after Generate or Reroll): MPC does not re-read
  an option parameter on its own, so this is a small change to mpc-vst-plugins' wrapper (the way `<key>_on` is polled for
  text params today);
- 16 more parameters polled by the wrapper (the bench has headroom, but watch it: worst block was 14.5% with the lit keys).
The pad panels' LED colours in exported programs already follow the category.

## Smaller things
- Link-to-originals export: verify on hardware that MPC loads kits whose samples are symbolic links (copy mode is the verified path).
- Write each pad's gain into the exported program (today only the plugin playback uses it).
- Choke groups and a simple pitch/decay per pad.
- Several instances with different sample sources (today they share one library).
- More devices: test on MPC One/X/Live/Key standalone.
