#!/usr/bin/env python3
"""Writes vst/glyphs.json: the outlines of the characters the skin's key labels use, from Titillium Web Bold (SIL OFL 1.1,
shipped in mpc-vst-plugins' tools/html_art/fonts). An SVG used as an image cannot load a page font, so gen_art.py draws the
key text as paths; this keeps those keys in the same typeface as the plugin's other buttons. Needs fontTools:
  docker run --rm -v "$PWD":/w -w /w python:3.11-slim sh -c "pip install -q fonttools && python3 tools/gen_glyphs.py"
"""
import json
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
f = TTFont("vendor/mpc-vst/tools/html_art/fonts/TitilliumWeb-Bold.ttf")
gs, cmap = f.getGlyphSet(), f.getBestCmap()
out = {"upm": f["head"].unitsPerEm, "glyphs": {}}
for ch in " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789()-/.'":
    g = gs[cmap[ord(ch)]]
    pen = SVGPathPen(gs)
    g.draw(pen)
    out["glyphs"][ch] = {"adv": g.width, "d": pen.getCommands()}
json.dump(out, open("vst/glyphs.json", "w"), separators=(",", ":"))
print(len(out["glyphs"]), "glyphs")
