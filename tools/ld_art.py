#!/usr/bin/env python3
"""Lucky Dip's skin art, drawn from code with Pillow (no image files in, so a restyle is this one file).
Primitives only: the page layouts and the TUI widgets that sit on them are in tools/make_tui.py.

Look ("prize night"): a deep ink cabinet, rounded cards with a soft top light, recessed display wells with warm cream text,
chunky keys with a coloured icon, gold for the main verb. Colour means category everywhere: six families, the same on the pad tiles,
the PAD EDIT keys and the category strips.
Fonts: Titillium Web (SIL OFL, tools/fonts/), baked into the art; the live text on the MPC is Titillium Web too.
"""
import math, os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
S = 3   # supersampling: everything is drawn at S x and scaled down

# ---- palette ------------------------------------------------------------------------------------------------------------------
INK = (17, 14, 28)
INK2 = (24, 20, 39)
CARD_TOP = (41, 35, 63)
CARD_BOT = (30, 26, 47)
CARD_LINE = (66, 57, 96)
WELL = (10, 8, 18)
WELL_LINE = (52, 45, 78)
CREAM = (255, 243, 222)
DIM = (160, 150, 190)
FAINT = (105, 96, 138)
GOLD = (255, 198, 52)
GOLD_DEEP = (214, 140, 20)
PINK = (255, 64, 129)
KEY_FACE = (52, 45, 78)
KEY_FACE2 = (40, 34, 61)

FAMILY = {   # name -> colour
    "kick": (255, 90, 90), "snare": (255, 150, 52), "metal": (255, 214, 70), "perc": (88, 214, 124),
    "tonal": (70, 170, 255), "fx": (190, 116, 255), "other": (150, 146, 172), "missing": (255, 70, 70),
}
# the engine's 23 categories (CAT_NAME order): short caption, family
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
        self.im = Image.new("RGBA", (w * S, h * S), fill)
        self.d = ImageDraw.Draw(self.im, "RGBA")

    def _b(self, box):
        return [v * S for v in box]

    def rect(self, box, r=0, fill=None, outline=None, width=1):
        self.d.rounded_rectangle(self._b(box), r * S, fill=fill, outline=outline, width=int(width * S))

    def ellipse(self, box, fill=None, outline=None, width=1):
        self.d.ellipse(self._b(box), fill=fill, outline=outline, width=int(width * S))

    def poly(self, pts, fill=None, outline=None):
        self.d.polygon([(x * S, y * S) for x, y in pts], fill=fill, outline=outline)

    def line(self, pts, fill, width=1):
        self.d.line([(x * S, y * S) for x, y in pts], fill=fill, width=int(width * S), joint="curve")

    def text(self, xy, s, px, fill, anchor="mm", weight="SemiBold", spacing=0):
        f = font(px * S, weight)
        if not spacing:
            self.d.text((xy[0] * S, xy[1] * S), s, font=f, fill=fill, anchor=anchor)
            return
        # letter-spaced caption
        widths = [self.d.textlength(ch, font=f) for ch in s]
        total = sum(widths) + spacing * S * (len(s) - 1)
        x = xy[0] * S - (total / 2 if anchor[0] == "m" else total if anchor[0] == "r" else 0)
        for ch, w in zip(s, widths):
            self.d.text((x, xy[1] * S), ch, font=f, fill=fill, anchor="l" + anchor[1])
            x += w + spacing * S

    def textw(self, s, px, weight="SemiBold"):
        return self.d.textlength(s, font=font(px * S, weight)) / S

    def vgrad(self, box, top, bot, r=0):
        x0, y0, x1, y1 = box
        g = Image.new("RGBA", (int((x1 - x0) * S), int((y1 - y0) * S)))
        gd = ImageDraw.Draw(g)
        for y in range(g.height):
            gd.line([(0, y), (g.width, y)], fill=rgba(mix(top, bot, y / max(1, g.height - 1)), top[3] if len(top) > 3 else 255))
        m = Image.new("L", g.size, 0)
        ImageDraw.Draw(m).rounded_rectangle([0, 0, g.width - 1, g.height - 1], r * S, fill=255)
        self.im.paste(g, (int(x0 * S), int(y0 * S)), m)

    def shadow(self, box, r, blur=10, alpha=120, dy=4):
        x0, y0, x1, y1 = box
        pad = blur * 3
        sh = Image.new("RGBA", (int((x1 - x0 + 2 * pad) * S), int((y1 - y0 + 2 * pad) * S)), (0, 0, 0, 0))
        ImageDraw.Draw(sh).rounded_rectangle([pad * S, pad * S, (x1 - x0 + pad) * S, (y1 - y0 + pad) * S], r * S, fill=(0, 0, 0, alpha))
        sh = sh.filter(ImageFilter.GaussianBlur(blur * S / 2))
        self.im.alpha_composite(sh, (int((x0 - pad) * S), int((y0 - pad + dy) * S)))

    def image(self):
        return self.im.resize((self.w, self.h), Image.LANCZOS)


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
    elif name == "clear":   # an empty pad: a dashed square
        k = s * 0.36
        for i in range(4):
            a, b = [(cx - k, cy - k), (cx + k, cy - k), (cx + k, cy + k), (cx - k, cy + k)][i], [(cx + k, cy - k), (cx + k, cy + k), (cx - k, cy + k), (cx - k, cy - k)][i]
            for t in (0.0, 0.55):
                p0 = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
                p1 = (a[0] + (b[0] - a[0]) * (t + 0.35), a[1] + (b[1] - a[1]) * (t + 0.35))
                c.line([p0, p1], col, s * 0.1)
    elif name == "level":   # match levels: three bars of one height with a line
        for i, h in enumerate((0.5, 0.5, 0.5)):
            x = cx - s * 0.3 + i * s * 0.3
            c.rect((x - s * 0.08, cy - s * h * 0.6, x + s * 0.08, cy + s * 0.4), s * 0.03, fill=col)
        c.line([(cx - s * 0.48, cy - s * 0.42), (cx + s * 0.48, cy - s * 0.42)], col, s * 0.07)
    elif name == "export":   # an arrow out of a tray
        c.line([(cx, cy + s * 0.12), (cx, cy - s * 0.42)], col, s * 0.12)
        c.poly([(cx - s * 0.24, cy - s * 0.2), (cx, cy - s * 0.48), (cx + s * 0.24, cy - s * 0.2)], fill=col)
        c.line([(cx - s * 0.42, cy - s * 0.02), (cx - s * 0.42, cy + s * 0.4), (cx + s * 0.42, cy + s * 0.4), (cx + s * 0.42, cy - s * 0.02)], col, s * 0.1)
    elif name == "rescan":   # a circular arrow
        c.d.arc([(cx - s * 0.38) * S, (cy - s * 0.38) * S, (cx + s * 0.38) * S, (cy + s * 0.38) * S], 40, 330, fill=col, width=int(s * 0.11 * S))
        a = math.radians(40)
        px, py = cx + s * 0.38 * math.cos(a), cy + s * 0.38 * math.sin(a)
        c.poly([(px - s * 0.2, py - s * 0.02), (px + s * 0.14, py - s * 0.14), (px + s * 0.06, py + s * 0.22)], fill=col)
    elif name == "follow":   # a finger tap: a dot with rings
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
    elif name == "dup":   # no duplicates: two squares, one crossed
        c.rect((cx - s * 0.46, cy - s * 0.2, cx - s * 0.06, cy + s * 0.2), s * 0.06, outline=col, width=s * 0.08)
        c.rect((cx + s * 0.06, cy - s * 0.2, cx + s * 0.46, cy + s * 0.2), s * 0.06, outline=col, width=s * 0.08)
        c.line([(cx + s * 0.02, cy + s * 0.32), (cx + s * 0.5, cy - s * 0.32)], col, s * 0.08)
    elif name == "loop":
        c.d.arc([(cx - s * 0.46) * S, (cy - s * 0.26) * S, (cx) * S, (cy + s * 0.26) * S], 0, 360, fill=col, width=int(s * 0.09 * S))
        c.d.arc([(cx) * S, (cy - s * 0.26) * S, (cx + s * 0.46) * S, (cy + s * 0.26) * S], 0, 360, fill=col, width=int(s * 0.09 * S))
    elif name == "copy":
        c.rect((cx - s * 0.36, cy - s * 0.4, cx + s * 0.16, cy + s * 0.2), s * 0.06, outline=col, width=s * 0.08)
        c.rect((cx - s * 0.16, cy - s * 0.2, cx + s * 0.36, cy + s * 0.4), s * 0.06, fill=col)
    elif name == "link":
        for dx in (-0.16, 0.16):
            c.rect((cx + dx * s - s * 0.26, cy - s * 0.14, cx + dx * s + s * 0.26, cy + s * 0.14), s * 0.14, outline=col, width=s * 0.09)


# ---- parts ---------------------------------------------------------------------------------------------------------------------
def card(c, box, title=None, r=14, accent=None):
    """A rounded panel with a soft top light; title in letter-spaced small caps at the top left."""
    x0, y0, x1, y1 = box
    c.shadow(box, r, blur=8, alpha=150, dy=4)
    c.vgrad(box, CARD_TOP, CARD_BOT, r)
    c.rect(box, r, outline=rgba(CARD_LINE), width=1.2)
    c.line([(x0 + r, y0 + 1.2), (x1 - r, y0 + 1.2)], (255, 255, 255, 28), 1)
    if title:
        tx = x0 + 18
        if accent:
            c.rect((tx, y0 + 15, tx + 4, y0 + 31), 2, fill=rgba(accent))
            tx += 12
        c.text((tx, y0 + 23), title, 14, rgba(DIM), anchor="lm", weight="Bold", spacing=1.6)


def well(c, box, r=8):
    """A recessed display: dark, with an inner top shadow and a faint lit lower edge."""
    x0, y0, x1, y1 = box
    c.rect(box, r, fill=rgba(WELL), outline=rgba(WELL_LINE), width=1)
    c.vgrad((x0 + 1, y0 + 1, x1 - 1, y0 + 8), (0, 0, 0, 120), WELL, r)
    c.line([(x0 + r, y1 - 0.5), (x1 - r, y1 - 0.5)], (255, 255, 255, 22), 1)


def key(w, h, caption="", icon_name=None, colour=GOLD, state="off", px=15, solid=False):
    """A key image (w x h + a 4 px lip below). state: off (dark face, coloured icon), on (lit: coloured face), pressed (lit and sunk).
    solid: the off state is coloured too (the main verbs)."""
    c = Canvas(w, h + 6)
    lit = state in ("on", "pressed") or solid
    dy = 3 if state == "pressed" else 0
    face_top = mix(colour, (255, 255, 255), 0.18) if lit else KEY_FACE
    face_bot = mix(colour, (0, 0, 0), 0.12) if lit else KEY_FACE2
    if solid and state == "off":
        face_top, face_bot = colour, mix(colour, (0, 0, 0), 0.22)
    if state == "pressed":
        face_top, face_bot = mix(face_top, (0, 0, 0), 0.18), mix(face_bot, (0, 0, 0), 0.18)
    lip = mix(face_bot, (0, 0, 0), 0.45)
    c.rect((1, 4, w - 1, h + 5), 9, fill=rgba(lip))                            # side wall
    c.vgrad((1, 1 + dy, w - 1, h + 1 + dy), face_top, face_bot, 9)               # cap
    c.rect((1, 1 + dy, w - 1, h + 1 + dy), 9, outline=(255, 255, 255, 40 if not lit else 70), width=1)
    if state == "on" and not solid:   # a glow ring for a lit toggle
        c.rect((1, 1 + dy, w - 1, h + 1 + dy), 9, outline=rgba(mix(colour, (255, 255, 255), 0.5), 200), width=2)
    ink = INK if lit else colour
    cap_ink = INK if lit else CREAM
    cy = 1 + dy + h / 2
    if icon_name and caption:
        iw = px * 1.15
        tw = c.textw(caption, px, "Bold") + 2 * len(caption) * 0.8
        x = w / 2 - (iw + 8 + tw) / 2
        icon(c, icon_name, x + iw / 2, cy, iw, rgba(ink))
        c.text((x + iw + 8, cy + 1), caption, px, rgba(cap_ink), anchor="lm", weight="Bold", spacing=0.8)
    elif icon_name:
        icon(c, icon_name, w / 2, cy, min(w, h) * 0.5, rgba(ink))
    elif caption:
        c.text((w / 2, cy + 1), caption, px, rgba(cap_ink), anchor="mm", weight="Bold", spacing=0.8)
    return c.image()


def seg(w, h, caption, colour=GOLD, lit=False, px=15, first=False, last=False):
    """One segment of a segmented choice (radio group): flat, joined to its neighbours."""
    c = Canvas(w, h)
    r = 8
    fill = mix(colour, (255, 255, 255), 0.1) if lit else KEY_FACE2
    c.rect((0.5, 0.5, w - 0.5, h - 0.5), r, fill=rgba(fill), outline=rgba(CARD_LINE if not lit else colour))
    c.text((w / 2, h / 2 + 1), caption, px, rgba(INK if lit else CREAM), weight="Bold", spacing=0.6)
    return c.image()


def slider_strip(w, h, frames, colour=GOLD):
    """A horizontal gain slider film strip (frames stacked vertically): a recessed track, the filled part, a round thumb, and
    the unity (100 %) tick of a 0..200 % range."""
    strip = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for i in range(frames):
        t = i / (frames - 1)
        c = Canvas(w, h)
        th = 10
        ty = h / 2
        x0, x1 = h / 2, w - h / 2
        c.rect((x0 - th / 2, ty - th / 2, x1 + th / 2, ty + th / 2), th / 2, fill=rgba(WELL), outline=rgba(WELL_LINE))
        xm = x0 + (x1 - x0) * 0.5
        c.line([(xm, ty - th / 2 - 5), (xm, ty - th / 2 - 1)], rgba(FAINT), 1.5)
        xv = x0 + (x1 - x0) * t
        if xv > x0 + 1:
            c.vgrad((x0 - th / 2 + 1, ty - th / 2 + 1, xv, ty + th / 2 - 1), mix(colour, (255, 255, 255), 0.25), mix(colour, (0, 0, 0), 0.15), th / 2 - 1)
        r = h / 2 - 3
        c.ellipse((xv - r, ty - r + 1.5, xv + r, ty + r + 1.5), fill=(0, 0, 0, 110))
        c.ellipse((xv - r, ty - r, xv + r, ty + r), fill=rgba(CREAM), outline=rgba(mix(colour, (0, 0, 0), 0.2)), width=2)
        c.ellipse((xv - r * 0.35, ty - r * 0.35, xv + r * 0.35, ty + r * 0.35), fill=rgba(colour))
        strip.alpha_composite(c.image(), (0, i * h))
    return strip


def kind_strip(w, h, big=False):
    """What a pad holds, one frame per kind (0 empty, 1..23 category, 24 missing): a coloured band with the category name.
    big: the PAD EDIT header version (taller, larger text)."""
    frames = 25
    strip = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for k in range(frames):
        c = Canvas(w, h)
        if k == 0:
            cap, col = "EMPTY", (80, 72, 108)
        elif k == 24:
            cap, col = "MISSING", FAMILY["missing"]
        else:
            cap, col = CATS[k - 1][0], FAMILY[CATS[k - 1][1]]
        c.vgrad((0, 0, w, h), mix(col, (255, 255, 255), 0.12), mix(col, (0, 0, 0), 0.18), 7)
        c.rect((0, 0, w, h), 7, outline=(255, 255, 255, 50))
        ink = INK if k else CREAM
        px = 20 if big else 15
        c.text((14, h / 2 + 1), cap, px, rgba(ink), anchor="lm", weight="Bold", spacing=1.4)
        if k == 24:
            c.text((w - 16, h / 2 + 1), "!", px + 4, rgba(ink), anchor="rm", weight="Bold")
        strip.alpha_composite(c.image(), (0, k * h))
    return strip


def wordmark(c, x, y):
    """LUCKY DIP in gold with a pink shadow, and a little prize-ticket mark."""
    # ticket
    tx, ty, tw, th = x, y - 20, 50, 40
    c.rect((tx + 2, ty + 3, tx + tw + 2, ty + th + 3), 6, fill=rgba(PINK, 200))
    c.rect((tx, ty, tx + tw, ty + th), 6, fill=rgba(GOLD))
    for yy in (ty + th / 2,):
        c.ellipse((tx - 6, yy - 6, tx + 6, yy + 6), fill=rgba(INK2))
        c.ellipse((tx + tw - 6, yy - 6, tx + tw + 6, yy + 6), fill=rgba(INK2))
    icon(c, "star", tx + tw / 2, ty + th / 2, 26, rgba(INK))
    # letters
    lx = tx + tw + 16
    c.text((lx + 2, y + 3), "LUCKY DIP", 40, rgba(PINK), anchor="lm", weight="Bold", spacing=2)
    c.text((lx, y), "LUCKY DIP", 40, rgba(GOLD), anchor="lm", weight="Bold", spacing=2)


def page_bg(w=1280, h=628):
    """The cabinet: deep ink with a soft top glow and a sprinkle of confetti dots."""
    c = Canvas(w, h, rgba(INK))
    c.vgrad((0, 0, w, h), INK2, INK, 0)
    glow = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([w * S * 0.15, -h * S * 0.6, w * S * 0.85, h * S * 0.35], fill=(120, 70, 200, 40))
    c.im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(60 * S)))
    import random
    rnd = random.Random(7)
    cols = [GOLD, PINK, FAMILY["tonal"], FAMILY["perc"], FAMILY["fx"]]
    for _ in range(90):
        x, y = rnd.uniform(0, w), rnd.uniform(0, h)
        r = rnd.uniform(1.0, 2.2)
        c.ellipse((x - r, y - r, x + r, y + r), fill=rgba(rnd.choice(cols), rnd.randint(30, 70)))
    return c
