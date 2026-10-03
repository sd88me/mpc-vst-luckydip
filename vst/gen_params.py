#!/usr/bin/env python3
"""Writes vst/params.json: Lucky Dip's parameter list. The order IS the VST parameter index (saved projects and
skins bind to it): append only, never reorder a shipped plugin."""
import json, os, sys
CATS = ["kick", "snare", "rim", "clap", "hat", "closed_hat", "open_hat", "tom", "conga", "percussion", "crash",
        "ride", "cymbal", "fx", "glitch", "vox", "bass", "synth", "stab", "chord", "lead", "pad", "other"]
P = []
def trig(key, name, **kw): P.append(dict(key=key, name=name, min=0, max=1, momentary=True, **kw))
def text(key, name): P.append(dict(key=key, name=name, min=0, max=0, display="string"))
def toggle(key, name, default=0): P.append(dict(key=key, name=name, options=["OFF", "ON"], default=default))

text("status", "Status")
P.append(dict(key="sel_gain", name="Gain", min=0, max=200, default=100, unit="%", display="int"))
trig("generate", "Generate All"); trig("clear_all", "Clear All"); trig("normalise", "Match Levels")
trig("export", "Export Kit"); trig("rescan", "Rescan Library"); trig("unlock_all", "Unlock All")
toggle("prevent_dup", "No Duplicates", 1)
# the selected pad (DETAIL page): every sel_* key acts on pad sel_pad
P.append(dict(key="sel_pad", name="Pad", min=1, max=16, default=1, display="int"))
trig("sel_prev", "Prev Pad", step_of="sel_pad", step_delta=-1)
trig("sel_next", "Next Pad", step_of="sel_pad", step_delta=1)
text("sel_name", "Sample"); text("sel_cat", "Category")
for c in CATS: toggle("sel_cat_" + c, c.replace("_", " ").title())   # the pad's pool: a set of categories (none = default)
toggle("sel_lock", "Lock")
trig("sel_reroll", "Reroll"); trig("sel_clear", "Clear"); trig("sel_play", "Play")
trig("sel_fav", "Favourite"); trig("sel_reject", "Reject")
text("export_name", "Last Export")
# the 16 pad tiles (PADS page)
for i in range(1, 17):
    text("pad%d_name" % i, "Pad %d" % i)
    text("pad%d_pill" % i, "Pad %d Pool" % i)
    P.append(dict(key="pad%d_gain" % i, name="Pad %d Gain" % i, min=0, max=200, default=100, unit="%", display="int"))
    toggle("pad%d_lock" % i, "Pad %d Lock" % i)
    trig("pad%d_reroll" % i, "Pad %d Reroll" % i)
    trig("pad%d_play" % i, "Pad %d Play" % i)
# settings (SETTINGS page). Not shipped yet, so this tail was reworked in place; once released: append only.
text("lib_info", "Library")
for pre, nm in (("src", "Source"), ("exp", "Export")):
    text(pre + "_loc_name", nm + " Folder"); trig(pre + "_loc_prev", nm + " Folder Prev"); trig(pre + "_loc_next", nm + " Folder Next")
    text(pre + "_sub_name", nm + " Inside")
    for v, lab in (("prev", "Prev"), ("next", "Next"), ("prev10", "Prev 10"), ("next10", "Next 10")):
        trig(pre + "_sub_" + v, nm + " Inside " + lab)
P.append(dict(key="export_mode", name="Export Samples", options=["Copy samples", "Link to originals"], default=0))
toggle("skip_loops", "Skip Loops", 1)
P.append(dict(key="max_size", name="Max File Size", options=["Off", "1 MB", "2 MB", "5 MB", "10 MB", "20 MB"], default=0))
toggle("follow", "Follow Played Pad", 1)    # PAD EDIT follows the last pad played
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "params.json")
json.dump({"name": "Lucky Dip", "params": P}, open(out, "w"), indent=1)
print("%d params -> %s" % (len(P), out))
