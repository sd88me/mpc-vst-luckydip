#!/usr/bin/env python3
"""Writes vst/images/*.svg: Lucky Dip's skin artwork (background, slot-machine banner, candy tile panels).
Hand-drawn SVG, generated so the palette lives in one place. Run by vst/build.sh."""
import math, os, random
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "images")
os.makedirs(OUT, exist_ok=True)
# candy palette: (light, dark)
CANDY = [("#ff6fb0", "#ff2e7e"), ("#ffb347", "#ff7a00"), ("#ffe066", "#ffb800"), ("#9cf06a", "#3cc41e"),
         ("#5ef0e0", "#10b8b0"), ("#6cbcff", "#2677f0"), ("#c98cff", "#8a3df0"), ("#ff8a8a", "#e8323c")]
PURPLE_D, PURPLE_L = "#1d0a3d", "#4b1f8f"

def w(name, body):
    open(os.path.join(OUT, name), "w").write(body)

def lolly(cx, cy, r, c1, c2, stick=True):
    o = ""
    if stick:
        o += '<rect x="%g" y="%g" width="%g" height="%g" rx="%g" fill="#fff" opacity=".9"/>' % (cx - r * .09, cy + r * .6, r * .18, r * 1.1, r * .09)
    o += '<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx, cy, r, c2)
    for k in range(3):   # swirl: three arcs
        a = k * 2.094
        o += '<path d="M%g %g A%g %g 0 0 1 %g %g" fill="none" stroke="%s" stroke-width="%g" stroke-linecap="round"/>' % (
            cx + r * .15 * math.cos(a), cy + r * .15 * math.sin(a), r * .62, r * .62,
            cx + r * .85 * math.cos(a + 2.2), cy + r * .85 * math.sin(a + 2.2), c1, r * .26)
    o += '<circle cx="%g" cy="%g" r="%g" fill="none" stroke="#fff" stroke-opacity=".8" stroke-width="%g"/>' % (cx, cy, r, max(1.5, r * .08))
    o += '<ellipse cx="%g" cy="%g" rx="%g" ry="%g" fill="#fff" opacity=".35"/>' % (cx - r * .35, cy - r * .45, r * .25, r * .14)
    return o

def star(cx, cy, r, fill):
    pts = []
    for i in range(10):
        rr = r if i % 2 == 0 else r * .45
        a = -math.pi / 2 + i * math.pi / 5
        pts.append("%g,%g" % (cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return '<polygon points="%s" fill="%s" stroke="#fff" stroke-width="2" stroke-linejoin="round"/>' % (" ".join(pts), fill)

def cherry(cx, cy, s):
    return ('<path d="M%g %g Q%g %g %g %g M%g %g Q%g %g %g %g" fill="none" stroke="#3cc41e" stroke-width="%g" stroke-linecap="round"/>' % (
        cx - s * .35, cy + s * .25, cx - s * .2, cy - s * .5, cx + s * .15, cy - s * .75,
        cx + s * .4, cy + s * .3, cx + s * .35, cy - s * .4, cx + s * .15, cy - s * .75, s * .09) +
        '<circle cx="%g" cy="%g" r="%g" fill="#ff2e4d" stroke="#fff" stroke-width="2"/>' % (cx - s * .35, cy + s * .35, s * .32) +
        '<circle cx="%g" cy="%g" r="%g" fill="#ff2e4d" stroke="#fff" stroke-width="2"/>' % (cx + s * .4, cy + s * .4, s * .32) +
        '<circle cx="%g" cy="%g" r="%g" fill="#fff" opacity=".6"/>' % (cx - s * .45, cy + s * .25, s * .08))

def seven(cx, cy, s):
    return '<text x="%g" y="%g" text-anchor="middle" dominant-baseline="central" font-family="Titillium Web, DejaVu Sans, sans-serif" font-weight="700" font-size="%g" fill="#ff2e4d" stroke="#fff" stroke-width="3" paint-order="stroke">7</text>' % (cx, cy + 2, s * 1.5)

DEFS = ('<defs>'
        '<filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="3" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>'
        '<linearGradient id="bgg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#3a1478"/><stop offset=".55" stop-color="#2a0e5c"/><stop offset="1" stop-color="#17083a"/></linearGradient>'
        '<linearGradient id="ban" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#ff2e7e"/><stop offset=".5" stop-color="#b13dff"/><stop offset="1" stop-color="#2677f0"/></linearGradient>'
        '<linearGradient id="title" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff3a0"/><stop offset="1" stop-color="#ffb800"/></linearGradient>'
        '</defs>')

# page background 1280 x 634 (the plugin area): grape gradient, sprinkles, big faint lollies in the corners
rnd = random.Random(7)
bg = '<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="634" viewBox="0 0 1280 634">' + DEFS
bg += '<rect width="1280" height="634" fill="url(#bgg)"/>'
bg += '<g opacity=".16">' + lolly(1215, 560, 120, "#ffffff", "#ff6fb0") + lolly(40, 40, 90, "#ffffff", "#5ef0e0") + '</g>'
cols = [c[0] for c in CANDY]
for _ in range(110):
    x, y = rnd.uniform(0, 1280), rnd.uniform(0, 634)
    ang = rnd.uniform(0, 180)
    bg += '<rect x="%.0f" y="%.0f" width="14" height="5" rx="2.5" fill="%s" opacity="%.2f" transform="rotate(%.0f %.0f %.0f)"/>' % (
        x, y, rnd.choice(cols), rnd.uniform(.18, .5), ang, x + 7, y + 2.5)
bg += '</svg>'
w("bg.svg", bg)

# slot-machine banner 1280 x 70: marquee bulbs top and bottom, wordmark, three reels
b = '<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="70" viewBox="0 0 1280 70">' + DEFS
b += '<rect x="6" y="3" width="1268" height="64" rx="18" fill="url(#ban)" stroke="#fff" stroke-width="3"/>'
b += '<rect x="6" y="3" width="1268" height="28" rx="14" fill="#fff" opacity=".14"/>'
for i in range(32):
    for yy in (8, 62):
        on = (i % 2 == 0)
        b += '<circle cx="%g" cy="%g" r="4.5" fill="%s" filter="url(#glow)"/>' % (34 + i * 38, yy, "#fff3a0" if on else "#ff9fd0")
b += ('<text x="40" y="36" dominant-baseline="central" font-family="Titillium Web, DejaVu Sans, sans-serif" font-weight="700" '
      'font-size="46" letter-spacing="4" fill="url(#title)" stroke="#2a0e5c" stroke-width="7" paint-order="stroke" stroke-linejoin="round">LUCKY DIP</text>')
b += lolly(318, 36, 17, "#ffffff", "#ff2e7e", stick=False)
# the reels: a window each, with a cherry, a seven and a star
for k, (cx, sym) in enumerate(((410, "cherry"), (480, "seven"), (550, "star"))):
    b += '<rect x="%g" y="9" width="58" height="52" rx="9" fill="#fff8e0" stroke="#2a0e5c" stroke-width="3"/>' % (cx - 29)
    b += '<rect x="%g" y="9" width="58" height="14" rx="7" fill="#000" opacity=".12"/>' % (cx - 29)
    b += {"cherry": cherry(cx, 34, 30), "seven": seven(cx, 35, 28), "star": star(cx, 36, 20, "#ffc400")}[sym]
b += '</svg>'
w("banner.svg", b)

# one candy tile panel per pad colour, 295 x 272: gradient body, glossy header, dark well, a lolly in the corner
for k, (lt, dk) in enumerate(CANDY):
    t = '<svg xmlns="http://www.w3.org/2000/svg" width="295" height="272" viewBox="0 0 295 272">'
    t += ('<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient></defs>' % (lt, dk))
    t += '<rect x="2" y="2" width="291" height="268" rx="24" fill="url(#g)" stroke="#fff" stroke-width="3"/>'
    t += '<rect x="9" y="42" width="277" height="221" rx="16" fill="#1d0a3d" opacity=".34"/>'
    t += '<ellipse cx="110" cy="14" rx="96" ry="9" fill="#fff" opacity=".35"/>'
    t += lolly(264, 22, 13, "#ffffff", PURPLE_L, stick=False)
    r = random.Random(k)
    for _ in range(7):
        x, y = r.uniform(14, 270), r.uniform(246, 262)
        t += '<rect x="%.0f" y="%.0f" width="12" height="4" rx="2" fill="#fff" opacity=".45" transform="rotate(%.0f %.0f %.0f)"/>' % (x, y, r.uniform(0, 180), x + 6, y + 2)
    t += '</svg>'
    w("tile_%d.svg" % k, t)

# DETAIL panels: wide candy panels (same recipe), three colours; the header is 42 px so the title fits
def panel(name, lt, dk, wd, ht):
    p = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (wd, ht, wd, ht)
    p += '<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient></defs>' % (lt, dk)
    p += '<rect x="2" y="2" width="%d" height="%d" rx="24" fill="url(#g)" stroke="#fff" stroke-width="3"/>' % (wd - 4, ht - 4)
    p += '<rect x="9" y="42" width="%d" height="%d" rx="16" fill="#1d0a3d" opacity=".34"/>' % (wd - 18, ht - 51)
    p += '<ellipse cx="%d" cy="14" rx="%d" ry="9" fill="#fff" opacity=".35"/></svg>' % (wd * .3, wd * .28)
    w(name, p)
panel("panel_cat.svg", *CANDY[0], 860, 380)
panel("panel_kit.svg", *CANDY[4], 340, 380)
panel("panel_pad.svg", *CANDY[2], 1240, 246)
print("art ->", OUT)
