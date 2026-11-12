#!/usr/bin/env python3
"""Lucky Dip's skin art, drawn from code with Pillow (no image files in, so a restyle is this one file).
Primitives only: the page layouts and the TUI widgets that sit on them are in tools/make_tui.py.

Look: the 0.9 "candy on grape" skin (vst/skin.css, vst/images/*.svg) redrawn for the native skin: a grape cabinet with confetti, a
slot-machine marquee banner (bulbs, gold LUCKY DIP, three reels), candy tiles with white rims, a dark inner panel and a swirl badge,
rounded dark wells with gold text, glossy pill keys with a white rim, dark toggles that light up candy or mint when on.
Fonts: Titillium Web (SIL OFL, tools/fonts/), baked into the art; the live text on the MPC is Titillium Web too.
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
S = 3   # supersampling: everything is drawn at S x and scaled down


def h2(s):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


# ---- palette (the 0.9 skin's) ------------------------------------------------------------------------------------------------
GRAPE = h2("2a0e5c")
GRAPE_HI = h2("3a1478")
GRAPE_LO = h2("17083a")
WELL = h2("1d0a3d")
VIOLET = h2("4b1f8f")
LAVENDER = h2("b9a3e8")
LAV_DIM = h2("8a6fc9")
OUTLINE = h2("3a0f66")
WHITE = (255, 255, 255)
CREAM = h2("fff4d6")
GOLD = h2("ffd23f")        # live text and value arcs
GOLD_HI = h2("fff3a0")
GOLD_LO = h2("ffb800")
PINK = h2("ff2e7e")
MINT = h2("3dffb0")
INK = WELL                 # dark text on lit keys

# candy pairs (top, bottom) from the 0.9 tiles and panels
CANDY = {
    "pink": (h2("ff6fb0"), h2("ff2e7e")), "orange": (h2("ffb347"), h2("ff7a00")), "yellow": (h2("ffe066"), h2("ffb800")),
    "green": (h2("9cf06a"), h2("3cc41e")), "teal": (h2("5ef0e0"), h2("10b8b0")), "blue": (h2("6cbcff"), h2("2677f0")),
    "purple": (h2("c98cff"), h2("8a3df0")), "red": (h2("ff8a8a"), h2("e8323c")), "mint": (h2("7affc8"), h2("10b880")),
    "violet": (h2("7a4ccc"), h2("4b1f8f")),
}
TILE_ORDER = ["pink", "orange", "yellow", "green", "teal", "blue", "purple", "red"]   # pad tiles 1-8 (and 9-16)
# key colours of the 0.9 skin
K_PLAY, K_REROLL, K_GEN, K_CLEAR, K_FAV, K_REJECT = "green", "orange", "pink", "purple", "pink", "red"

# category families: a candy each (the 0.9 category keys were candy too)
FAMILY = {"kick": "red", "snare": "orange", "metal": "yellow", "perc": "green", "tonal": "blue", "fx": "purple", "other": "violet",
          "missing": "red"}
CATS = [("KICK", "kick"), ("SNARE", "snare"), ("RIM", "snare"), ("CLAP", "snare"), ("HAT", "metal"), ("CLOSED HAT", "metal"),
        ("OPEN HAT", "metal"), ("TOM", "perc"), ("CONGA", "perc"), ("PERC", "perc"), ("CRASH", "metal"), ("RIDE", "metal"),
        ("CYMBAL", "metal"), ("FX", "fx"), ("GLITCH", "fx"), ("VOX", "fx"), ("BASS", "tonal"), ("SYNTH", "tonal"), ("STAB", "tonal"),
        ("CHORD", "tonal"), ("LEAD", "tonal"), ("PAD", "tonal"), ("OTHER", "other")]
CAT_KEYS = ["kick", "snare", "rim", "clap", "hat", "closed_hat", "open_hat", "tom", "conga", "percussion", "crash", "ride", "cymbal",
            "fx", "glitch", "vox", "bass", "synth", "stab", "chord", "lead", "pad", "other"]

_fonts = {}


def font(px, weight="SemiBold"):
    k = (int(px), weight)
    if k not in _fonts:
        _fonts[k] = ImageFont.truetype(os.path.join(HERE, "fonts", "TitilliumWeb-%s.ttf" % weight), max(1, int(px)))
    return _fonts[k]


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def rgba(c, a=255):
    return tuple(c[:3]) + (a,)


# ---- a supersampled canvas ----------------------------------------------------------------------------------------------------
class Canvas:
    def __init__(self, w, h, fill=(0, 0, 0, 0)):
        self.w, self.h = w, h
        self.im = Image.new("RGBA", (int(w * S), int(h * S)), fill)
        self.d = ImageDraw.Draw(self.im)
        self._layer = None

    def _b(self, box):
        return [v * S for v in box]

    def _dr(self, *cols):
        """The draw to use: translucent colours go through a layer that is composited (ImageDraw on an RGBA image overwrites
        alpha instead of blending, which turned the 34 % inner panels solid)."""
        if any(c is not None and len(c) > 3 and c[3] < 255 for c in cols):
            self._layer = Image.new("RGBA", self.im.size, (0, 0, 0, 0))
            return ImageDraw.Draw(self._layer)
        self._layer = None
        return self.d

    def _done(self):
        if self._layer is not None:
            self.im.alpha_composite(self._layer)
            self._layer = None

    def rect(self, box, r=0, fill=None, outline=None, width=1):
        self._dr(fill, outline).rounded_rectangle(self._b(box), r * S, fill=fill, outline=outline, width=int(width * S))
        self._done()

    def ellipse(self, box, fill=None, outline=None, width=1):
        self._dr(fill, outline).ellipse(self._b(box), fill=fill, outline=outline, width=int(width * S))
        self._done()

    def poly(self, pts, fill=None, outline=None):
        self._dr(fill, outline).polygon([(x * S, y * S) for x, y in pts], fill=fill, outline=outline)
        self._done()

    def line(self, pts, fill, width=1):
        self._dr(fill).line([(x * S, y * S) for x, y in pts], fill=fill, width=int(width * S), joint="curve")
        self._done()

    def text(self, xy, s, px, fill, anchor="mm", weight="Bold", spacing=0, stroke=None, sw=0):
        """Text; spacing = letter spacing in px; stroke = an outline colour (the 0.9 captions: white with a dark outline)."""
        f = font(px * S, weight)
        kw = dict(stroke_width=int(sw * S), stroke_fill=stroke) if stroke else {}
        if not spacing:
            self.d.text((xy[0] * S, xy[1] * S), s, font=f, fill=fill, anchor=anchor, **kw)
            return
        widths = [self.d.textlength(ch, font=f) for ch in s]
        total = sum(widths) + spacing * S * (len(s) - 1)
        x = xy[0] * S - (total / 2 if anchor[0] == "m" else total if anchor[0] == "r" else 0)
        for ch, w in zip(s, widths):
            self.d.text((x, xy[1] * S), ch, font=f, fill=fill, anchor="l" + anchor[1], **kw)
            x += w + spacing * S

    def textw(self, s, px, weight="Bold", spacing=0):
        return self.d.textlength(s, font=font(px * S, weight)) / S + spacing * max(0, len(s) - 1)

    def vgrad(self, box, top, bot, r=0, alpha=255):
        x0, y0, x1, y1 = box
        g = Image.new("RGBA", (max(1, int((x1 - x0) * S)), max(1, int((y1 - y0) * S))))
        gd = ImageDraw.Draw(g)
        for y in range(g.height):
            gd.line([(0, y), (g.width, y)], fill=rgba(mix(top, bot, y / max(1, g.height - 1)), alpha))
        m = Image.new("L", g.size, 0)
        ImageDraw.Draw(m).rounded_rectangle([0, 0, g.width - 1, g.height - 1], r * S, fill=255)
        layer = Image.new("RGBA", self.im.size, (0, 0, 0, 0))
        layer.paste(g, (int(x0 * S), int(y0 * S)), m)
        self.im.alpha_composite(layer)

    def blurred(self, draw, blur):
        """Draw onto a layer with draw(ImageDraw, S), blur it, composite it (shadows, glows)."""
        layer = Image.new("RGBA", self.im.size, (0, 0, 0, 0))
        draw(ImageDraw.Draw(layer), S)
        self.im.alpha_composite(layer.filter(ImageFilter.GaussianBlur(blur * S)))

    def image(self):
        return self.im.resize((int(self.w), int(self.h)), Image.LANCZOS)


def caption(c, xy, s, px, anchor="mm", spacing=1.2, fill=WHITE):
    """The 0.9 skin's captions: white bold with a dark grape outline, letter-spaced."""
    c.text(xy, s, px, rgba(fill), anchor=anchor, spacing=spacing, stroke=rgba(OUTLINE, 230), sw=max(1.5, px * 0.16))


# ---- icons (drawn in a box centred on cx, cy, size s) -------------------------------------------------------------------------
def icon(c, name, cx, cy, s, col):
    if name == "play":
        c.poly([(cx - s * 0.32, cy - s * 0.42), (cx + s * 0.44, cy), (cx - s * 0.32, cy + s * 0.42)], fill=col)
    elif name in ("lock", "unlock"):
        bw, bh = s * 0.72, s * 0.5
        c.rect((cx - bw / 2, cy - s * 0.02, cx + bw / 2, cy - s * 0.02 + bh), s * 0.1, fill=col)
        sx = 0 if name == "lock" else s * 0.22
        c.d.arc([(cx - s * 0.24 + sx) * S, (cy - s * 0.48) * S, (cx + s * 0.24 + sx) * S, (cy + s * 0.04) * S], 180, 360 if name == "lock" else 330,
                fill=col, width=int(s * 0.12 * S))
        if name == "lock":
            c.line([(cx - s * 0.24, cy - s * 0.24), (cx - s * 0.24, cy)], col, s * 0.12)
            c.line([(cx + s * 0.24, cy - s * 0.24), (cx + s * 0.24, cy)], col, s * 0.12)
        else:
            c.line([(cx - s * 0.02, cy - s * 0.24), (cx - s * 0.02, cy)], col, s * 0.12)
    elif name == "dice":
        h = s * 0.44
        c.rect((cx - h, cy - h, cx + h, cy + h), s * 0.16, outline=col, width=s * 0.1)
        for dx, dy in ((-1, -1), (1, 1), (0, 0), (1, -1), (-1, 1)):
            c.ellipse((cx + dx * h * 0.5 - s * 0.08, cy + dy * h * 0.5 - s * 0.08, cx + dx * h * 0.5 + s * 0.08, cy + dy * h * 0.5 + s * 0.08), fill=col)
    elif name == "star":
        pts = []
        for i in range(10):
            a = -math.pi / 2 + i * math.pi / 5
            rr = s * (0.5 if i % 2 == 0 else 0.21)
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        c.poly(pts, fill=col)
    elif name == "x":
        k = s * 0.32
        c.line([(cx - k, cy - k), (cx + k, cy + k)], col, s * 0.14)
        c.line([(cx - k, cy + k), (cx + k, cy - k)], col, s * 0.14)
    elif name == "clear":
        k = s * 0.36
        corners = [(cx - k, cy - k), (cx + k, cy - k), (cx + k, cy + k), (cx - k, cy + k)]
        for i in range(4):
            a, b = corners[i], corners[(i + 1) % 4]
            for t in (0.0, 0.55):
                p0 = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
                p1 = (a[0] + (b[0] - a[0]) * (t + 0.35), a[1] + (b[1] - a[1]) * (t + 0.35))
                c.line([p0, p1], col, s * 0.1)
    elif name == "level":
        for i in range(3):
            x = cx - s * 0.3 + i * s * 0.3
            c.rect((x - s * 0.08, cy - s * 0.3, x + s * 0.08, cy + s * 0.4), s * 0.03, fill=col)
        c.line([(cx - s * 0.48, cy - s * 0.42), (cx + s * 0.48, cy - s * 0.42)], col, s * 0.07)
    elif name == "export":
        c.line([(cx, cy + s * 0.12), (cx, cy - s * 0.42)], col, s * 0.12)
        c.poly([(cx - s * 0.24, cy - s * 0.2), (cx, cy - s * 0.48), (cx + s * 0.24, cy - s * 0.2)], fill=col)
        c.line([(cx - s * 0.42, cy - s * 0.02), (cx - s * 0.42, cy + s * 0.4), (cx + s * 0.42, cy + s * 0.4), (cx + s * 0.42, cy - s * 0.02)], col, s * 0.1)
    elif name == "rescan":
        c.d.arc([(cx - s * 0.38) * S, (cy - s * 0.38) * S, (cx + s * 0.38) * S, (cy + s * 0.38) * S], 40, 330, fill=col, width=int(s * 0.11 * S))
        a = math.radians(40)
        px, py = cx + s * 0.38 * math.cos(a), cy + s * 0.38 * math.sin(a)
        c.poly([(px - s * 0.2, py - s * 0.02), (px + s * 0.14, py - s * 0.14), (px + s * 0.06, py + s * 0.22)], fill=col)
    elif name == "follow":
        c.ellipse((cx - s * 0.13, cy - s * 0.13, cx + s * 0.13, cy + s * 0.13), fill=col)
        c.ellipse((cx - s * 0.38, cy - s * 0.38, cx + s * 0.38, cy + s * 0.38), outline=col, width=s * 0.08)
    elif name in ("left", "right", "left2", "right2"):
        dirn = -1 if name.startswith("left") else 1
        n = 2 if name.endswith("2") else 1
        for i in range(n):
            ox = (i - (n - 1) / 2) * s * 0.34
            c.poly([(cx + ox - dirn * s * 0.18, cy - s * 0.3), (cx + ox + dirn * s * 0.2, cy), (cx + ox - dirn * s * 0.18, cy + s * 0.3)], fill=col)
    elif name == "folder":
        c.rect((cx - s * 0.46, cy - s * 0.26, cx - s * 0.02, cy - s * 0.1), s * 0.05, fill=col)
        c.rect((cx - s * 0.46, cy - s * 0.18, cx + s * 0.46, cy + s * 0.36), s * 0.07, fill=col)
    elif name == "dup":
        c.rect((cx - s * 0.46, cy - s * 0.2, cx - s * 0.06, cy + s * 0.2), s * 0.06, outline=col, width=s * 0.08)
        c.rect((cx + s * 0.06, cy - s * 0.2, cx + s * 0.46, cy + s * 0.2), s * 0.06, outline=col, width=s * 0.08)
        c.line([(cx + s * 0.02, cy + s * 0.32), (cx + s * 0.5, cy - s * 0.32)], col, s * 0.08)
    elif name == "loop":
        c.d.ellipse([(cx - s * 0.46) * S, (cy - s * 0.26) * S, cx * S, (cy + s * 0.26) * S], outline=col, width=int(s * 0.09 * S))
        c.d.ellipse([cx * S, (cy - s * 0.26) * S, (cx + s * 0.46) * S, (cy + s * 0.26) * S], outline=col, width=int(s * 0.09 * S))


def swirl(c, cx, cy, r):
    """The 0.9 tiles' badge: a violet disc with a white three-arm swirl."""
    c.ellipse((cx - r, cy - r, cx + r, cy + r), fill=rgba(VIOLET), outline=rgba(WHITE, 200), width=1.2)
    rr = r * 0.62
    for k in range(3):
        a0 = k * 120
        c.d.arc([(cx - rr) * S, (cy - rr) * S, (cx + rr) * S, (cy + rr) * S], a0, a0 + 75, fill=rgba(WHITE), width=int(r * 0.26 * S))
    c.ellipse((cx - r * 0.55, cy - r * 0.62, cx - r * 0.05, cy - r * 0.32), fill=(255, 255, 255, 80))


def confetti(c, box, n, seed, alpha=(90, 150), cols=None):
    x0, y0, x1, y1 = box
    rnd = random.Random(seed)
    cols = cols or [h2(x) for x in ("5ef0e0", "6cbcff", "9cf06a", "c98cff", "ff6fb0", "ff8a8a", "ffb347", "ffe066", "ffffff")]
    for _ in range(n):
        x, y = rnd.uniform(x0, x1), rnd.uniform(y0, y1)
        a = math.radians(rnd.uniform(0, 180))
        L, w = rnd.uniform(6, 12), 3.5
        dx, dy = math.cos(a) * L / 2, math.sin(a) * L / 2
        c.line([(x - dx, y - dy), (x + dx, y + dy)], rgba(rnd.choice(cols), rnd.randint(*alpha)), w)


# ---- parts ---------------------------------------------------------------------------------------------------------------------
# Shapes from the round-1 redesign (soft cards, keys with a side wall), colours from 0.9. Text wells: LD_WELL = cream | tone | glass.
WELL_STYLE = os.environ.get("LD_WELL", "tone")


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def ink_on(c):
    """Dark grape on light candy (yellow, mint, teal...), white on the rest."""
    return GRAPE if lum(c) > 170 else WHITE


def card(c, box, title=None, candy="violet", r=16, badge=True, inner_top=40, sprinkles=False):
    """A candy card: a soft drop shadow, a gentle top-to-bottom gradient, a thin light rim and a top highlight; the title in white,
    letter-spaced, with a soft shadow; the 0.9 swirl badge at the top right."""
    x0, y0, x1, y1 = box
    top, bot = CANDY[candy]
    c.blurred(lambda d, s: d.rounded_rectangle([x0 * s, (y0 + 5) * s, x1 * s, (y1 + 5) * s], r * s, fill=(20, 4, 44, 140)), 6)
    c.vgrad(box, mix(top, WHITE, 0.06), mix(bot, (0, 0, 0), 0.06), r)
    c.vgrad((x0 + 2, y0 + 2, x1 - 2, y0 + inner_top), WHITE, top, r - 2, alpha=34)
    c.rect(box, r, outline=rgba(WHITE, 150), width=1.5)
    c.line([(x0 + r, y0 + 1.5), (x1 - r, y0 + 1.5)], rgba(WHITE, 110), 1.2)
    if sprinkles:
        confetti(c, (x0 + 30, y1 - 16, x1 - 30, y1 - 8), max(2, int((x1 - x0) / 90)), seed=int(x0 * 7 + y0), alpha=(70, 110), cols=[WHITE])
    if title:
        tc = ink_on(top)
        sh = (0, 0, 0, 70) if tc == WHITE else (255, 255, 255, 90)
        c.text((x0 + 17, y0 + inner_top / 2 + 2), title, 17, sh, anchor="lm", spacing=1.6)
        c.text((x0 + 16, y0 + inner_top / 2 + 1), title, 17, rgba(tc), anchor="lm", spacing=1.6)
    if badge:
        swirl(c, x1 - 24, y0 + inner_top / 2 + 1, 12)


def well(c, box, r=10, candy="violet"):
    """A text well in the chosen style."""
    x0, y0, x1, y1 = box
    top, bot = CANDY[candy]
    if WELL_STYLE == "cream":
        c.rect(box, r, fill=rgba(mix(CREAM, top, 0.10)), outline=rgba(mix(bot, (0, 0, 0), 0.25), 160), width=1.2)
        c.vgrad((x0 + 1.5, y0 + 1.5, x1 - 1.5, y0 + 8), mix(bot, (0, 0, 0), 0.3), CREAM, r, alpha=70)
        c.line([(x0 + r, y1 - 1), (x1 - r, y1 - 1)], rgba(WHITE, 160), 1)
    elif WELL_STYLE == "tone":
        deep = mix(bot, GRAPE_LO, 0.42)
        c.rect(box, r, fill=rgba(deep), outline=rgba(mix(deep, (0, 0, 0), 0.25)), width=1)
        c.vgrad((x0 + 1, y0 + 1, x1 - 1, y0 + 8), (0, 0, 0), deep, r, alpha=80)
        c.line([(x0 + r, y1 - 0.5), (x1 - r, y1 - 0.5)], rgba(WHITE, 70), 1)
    else:   # glass
        c.rect(box, r, fill=(255, 255, 255, 46), outline=rgba(WHITE, 120), width=1.2)
        c.vgrad((x0 + 1, y0 + 1, x1 - 1, y0 + (y1 - y0) * 0.45), WHITE, WHITE, r, alpha=26)


def well_text(candy="violet"):
    """The live text colour that goes with well()."""
    if WELL_STYLE == "cream":
        return GRAPE
    if WELL_STYLE == "tone":
        return WHITE
    return WHITE if lum(CANDY[candy][1]) < 175 else GRAPE


def well_dim(candy="violet"):
    if WELL_STYLE == "cream":
        return mix(GRAPE, CREAM, 0.45)
    return mix(well_text(candy), CANDY[candy][0], 0.45)


def key(w, h, cap="", icon_name=None, candy="pink", state="off", px=15, toggle=False):
    """A key image (w x h + 6 px for the side wall), the round-1 shape in 0.9 colours. Momentaries: candy cap; pressed = sunk and
    darker. Toggles: off = a grape cap with the caption and icon in the key's candy; on = the candy cap with a glow ring."""
    c = Canvas(w, h + 6)
    r = 10
    dy = 3 if state == "pressed" else 0
    lit = not toggle or state == "on"
    top, bot = CANDY[candy]
    if lit:
        ft, fb = mix(top, WHITE, 0.12), bot
        if state == "pressed":
            ft, fb = mix(ft, (0, 0, 0), 0.18), mix(fb, (0, 0, 0), 0.18)
    else:
        ft, fb = mix(GRAPE_HI, VIOLET, 0.35), mix(GRAPE, GRAPE_LO, 0.3)
    wall = mix(fb, (0, 0, 0), 0.45)
    c.rect((1, 4, w - 1, h + 5), r, fill=rgba(wall))
    c.vgrad((1, 1 + dy, w - 1, h + 1 + dy), ft, fb, r)
    c.vgrad((3, 2 + dy, w - 3, 2 + dy + h * 0.45), WHITE, ft, r - 2, alpha=40 if lit else 18)
    c.rect((1, 1 + dy, w - 1, h + 1 + dy), r, outline=rgba(WHITE, 90 if lit else 50), width=1)
    if toggle and state == "on":
        c.rect((1, 1 + dy, w - 1, h + 1 + dy), r, outline=rgba(mix(top, WHITE, 0.55), 230), width=2)
    if lit:
        fill = ink_on(ft)
        icol = fill
    else:
        fill = mix(top, WHITE, 0.25)
        icol = top
    cy = 1 + dy + h / 2
    if icon_name and cap:
        iw = px * 1.1
        tw = c.textw(cap, px, spacing=px * 0.06)
        x = w / 2 - (iw + 8 + tw) / 2
        icon(c, icon_name, x + iw / 2, cy, iw, rgba(icol))
        c.text((x + iw + 8, cy + 1), cap, px, rgba(fill), anchor="lm", spacing=px * 0.06)
    elif icon_name:
        icon(c, icon_name, w / 2, cy, min(w, h) * 0.5, rgba(icol))
    elif cap:
        c.text((w / 2, cy + 1), cap, px, rgba(fill), spacing=px * 0.06)
    return c.image()


def seg(w, h, cap, candy="pink", lit=False, px=15):
    """One segment of a lit choice (a radio group)."""
    return key(w, h - 6, cap, None, candy, "on" if lit else "off", px, toggle=True)


def slider_strip(w, h, frames):
    """The gain slider film strip (frames stacked vertically), in the 0.9 knob's colours: a violet track, a yellow value bar, a cream
    thumb with a pink pointer, and the unity (100 %) tick of the 0..200 % range."""
    strip = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for i in range(frames):
        t = i / (frames - 1)
        c = Canvas(w, h)
        th, ty = 12, h / 2
        x0, x1 = h / 2, w - h / 2
        c.rect((x0 - th / 2, ty - th / 2, x1 + th / 2, ty + th / 2), th / 2, fill=rgba(VIOLET), outline=rgba(WHITE, 120), width=1)
        xm = (x0 + x1) / 2
        c.line([(xm, ty - th / 2 - 6), (xm, ty - th / 2 - 1)], rgba(WHITE, 170), 2)
        xv = x0 + (x1 - x0) * t
        if xv > x0 + 1:
            c.vgrad((x0 - th / 2 + 2, ty - th / 2 + 2, xv, ty + th / 2 - 2), GOLD_HI, GOLD_LO, th / 2 - 2)
        r = h / 2 - 3
        c.ellipse((xv - r, ty - r + 2.5, xv + r, ty + r + 2.5), fill=(40, 8, 80, 140))
        c.ellipse((xv - r, ty - r, xv + r, ty + r), fill=rgba(CREAM), outline=rgba(WHITE), width=2)
        c.line([(xv, ty - r * 0.62), (xv, ty + r * 0.1)], rgba(PINK), 3)
        strip.alpha_composite(c.image(), (0, i * h))
    return strip


def kind_strip(w, h, big=False, candy="violet"):
    """What a pad holds, one frame per kind (0 empty, 1..23 category, 24 missing): a pill in the well style with a candy chip of the
    category's family and its name (red with "!" when the sample is missing)."""
    frames = 25
    strip = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for k in range(frames):
        c = Canvas(w, h)
        if k == 0:
            cap, chip = "EMPTY", None
        elif k == 24:
            cap, chip = "MISSING!", "red"
        else:
            cap, chip = CATS[k - 1][0], FAMILY[CATS[k - 1][1]]
        well(c, (0.75, 0.75, w - 0.75, h - 0.75), h / 2 - 1, candy)
        d = h - 12
        if chip:
            top, bot = CANDY[chip]
            c.vgrad((6, 6, 6 + d, 6 + d), top, bot, d / 2)
            c.ellipse((6, 6, 6 + d, 6 + d), outline=rgba(WHITE, 220), width=1.2)
        else:
            c.ellipse((6, 6, 6 + d, 6 + d), outline=rgba(well_dim(candy)), width=1.5)
        c.text((14 + d, h / 2 + 1), cap, 19 if big else 15, rgba(well_text(candy) if k else well_dim(candy)), anchor="lm", spacing=1.2)
        strip.alpha_composite(c.image(), (0, k * h))
    return strip


def dot_strip(w, h):
    """A thin candy bar per kind (PAD EDIT's pad grid)."""
    frames = 25
    s = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for k in range(frames):
        cc = Canvas(w, h)
        if k == 0:
            cc.rect((8, 1, w - 8, h - 1), (h - 2) / 2, fill=rgba(VIOLET))
        else:
            top, bot = CANDY["red" if k == 24 else FAMILY[CATS[k - 1][1]]]
            cc.vgrad((8, 1, w - 8, h - 1), top, bot, (h - 2) / 2)
        s.alpha_composite(cc.image(), (0, k * h))
    return s


# ---- the banner: the 0.9 slot-machine marquee ---------------------------------------------------------------------------------
def cherry(c, cx, cy, s):
    c.line([(cx - s * 0.18, cy + s * 0.05), (cx + s * 0.05, cy - s * 0.4), (cx + s * 0.22, cy + s * 0.05)], rgba(h2("2f9e3a")), s * 0.08)
    c.ellipse((cx + s * 0.02, cy - s * 0.5, cx + s * 0.32, cy - s * 0.36), fill=rgba(h2("3cc41e")))
    for dx in (-0.2, 0.22):
        x = cx + dx * s
        c.ellipse((x - s * 0.2, cy, x + s * 0.2, cy + s * 0.4), fill=rgba(h2("e8323c")), outline=rgba(WHITE), width=1.2)
        c.ellipse((x - s * 0.11, cy + s * 0.06, x - s * 0.03, cy + s * 0.14), fill=(255, 255, 255, 180))


def reel(c, cx, cy, sym, s=40):
    c.rect((cx - s / 2, cy - s / 2 - 2, cx + s / 2, cy + s / 2 + 2), 8, fill=rgba(CREAM), outline=rgba(OUTLINE), width=2.5)
    c.vgrad((cx - s / 2 + 3, cy - s / 2, cx + s / 2 - 3, cy - s / 2 + 8), (190, 170, 220), CREAM, 4, alpha=140)
    if sym == "cherry":
        cherry(c, cx, cy - 2, s * 0.62)
    elif sym == "seven":
        c.text((cx, cy + 1), "7", s * 0.82, rgba(h2("ff2e4d")), stroke=rgba(WHITE), sw=1.5)
    else:
        icon(c, "star", cx, cy + 1, s * 0.7, rgba(h2("ffc400")))
        c.text((cx, cy), "", 1, (0, 0, 0, 0))


def banner(c, box=(6, 4, 1274, 78)):
    x0, y0, x1, y1 = box
    r = 18
    # a horizontal pink -> violet -> blue gradient
    g = Image.new("RGBA", (int((x1 - x0) * S), int((y1 - y0) * S)))
    gd = ImageDraw.Draw(g)
    stops = [h2("ff2e7e"), h2("b13dff"), h2("2677f0")]
    for x in range(g.width):
        t = x / (g.width - 1)
        col = mix(stops[0], stops[1], t * 2) if t < 0.5 else mix(stops[1], stops[2], (t - 0.5) * 2)
        gd.line([(x, 0), (x, g.height)], fill=rgba(col))
    m = Image.new("L", g.size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, g.width - 1, g.height - 1], r * S, fill=255)
    c.blurred(lambda d, s: d.rounded_rectangle([x0 * s, (y0 + 4) * s, x1 * s, (y1 + 4) * s], r * s, fill=(20, 4, 44, 150)), 4)
    layer = Image.new("RGBA", c.im.size, (0, 0, 0, 0))
    layer.paste(g, (int(x0 * S), int(y0 * S)), m)
    c.im.alpha_composite(layer)
    c.rect((x0, y0, x1, y0 + (y1 - y0) * 0.42), r * 0.8, fill=(255, 255, 255, 36))
    c.rect(box, r, outline=rgba(WHITE), width=3)
    # marquee bulbs, top and bottom
    for k, x in enumerate(range(int(x0) + 28, int(x1) - 20, 38)):
        col = GOLD_HI if k % 2 == 0 else h2("ff9fd0")
        for y in (y0 + 5, y1 - 5):
            c.blurred(lambda d, s, x=x, y=y, col=col: d.ellipse([(x - 6) * s, (y - 6) * s, (x + 6) * s, (y + 6) * s], fill=rgba(col, 200)), 2)
            c.ellipse((x - 4, y - 4, x + 4, y + 4), fill=rgba(col))


def wordmark(c, x, y, px=44):
    """LUCKY DIP: gold gradient letters with a thick grape outline (the 0.9 banner title)."""
    f = font(px * S, "Bold")
    txt, sp = "LUCKY DIP", 4
    # outline pass
    c.text((x, y), txt, px, rgba(GRAPE), anchor="lm", spacing=sp, stroke=rgba(GRAPE), sw=4)
    # gradient fill through a mask
    mask = Image.new("L", c.im.size, 0)
    md = ImageDraw.Draw(mask)
    xx = x * S
    for ch in txt:
        md.text((xx, y * S), ch, font=f, fill=255, anchor="lm")
        xx += md.textlength(ch, font=f) + sp * S
    bb = mask.getbbox() or (0, 0, 1, 1)
    grad = Image.new("RGBA", c.im.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)
    for yy in range(bb[1], bb[3]):
        gd.line([(bb[0], yy), (bb[2], yy)], fill=rgba(mix(GOLD_HI, GOLD_LO, (yy - bb[1]) / max(1, bb[3] - bb[1]))))
    c.im.paste(grad, (0, 0), mask)


def page_bg(w=1280, h=628):
    """The cabinet: the 0.9 diagonal grape gradient with candy confetti."""
    c = Canvas(w, h, rgba(GRAPE))
    g = Image.new("RGBA", (w * S // 4, h * S // 4))
    gd = ImageDraw.Draw(g)
    for y in range(g.height):
        for x in range(0, g.width, 2):
            t = (x / g.width + y / g.height) / 2
            col = mix(GRAPE_HI, GRAPE, t / 0.55) if t < 0.55 else mix(GRAPE, GRAPE_LO, (t - 0.55) / 0.45)
            gd.line([(x, y), (x + 1, y)], fill=rgba(col))
    c.im.alpha_composite(g.resize(c.im.size, Image.BILINEAR))
    confetti(c, (0, 0, w, h), 70, seed=11, alpha=(60, 110))
    return c
