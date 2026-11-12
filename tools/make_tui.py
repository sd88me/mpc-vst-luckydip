#!/usr/bin/env python3
"""Write skins/TUI.json + skins/Q-Links.json (the MPC's native skin) and the PNGs they use. Art primitives: tools/ld_art.py.
Structure from Profit-08 / Crate Digger 2 (tools/make_tui.py there): one background PNG per tab with all static art, live widgets at the
same coordinates (Labels for text, Buttons with baked captions, a film-strip slider component, Image strips for the category bands),
one Q-Link group of four per printed block.

Tabs (the 0.9 order): PADS 1-8, PADS 9-16, PAD EDIT, SETTINGS.

    python3 tools/make_tui.py              # -> skins/
    python3 tools/make_tui.py --preview    # also docs/skin-preview/<tab>.png: the background plus every widget at sample values
"""
import hashlib, os, re, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "skins")
PREVIEW = os.path.join(ROOT, "docs", "skin-preview")
sys.path.insert(0, HERE)
import ld_art as A
from PIL import Image

GEN = open(os.path.join(ROOT, "src", "params_gen.h")).read()
KEYS = re.findall(r'^\s*\{"([^"]+)"', GEN, re.M)   # VST order (generated: the frozen 161, then the appended ones)
FONT = "Titillium Web"
TUI_PX = 0.66   # a Label of TUI height h draws text about 0.66 * h pixels tall (measured on the Live III)
W, H = 1280, 628


def P(key):
    return "Parameter %d" % KEYS.index(key)


def hexc(c, a=255):
    return "%02x%02x%02x%02x" % (a, c[0], c[1], c[2])


# ---- a JSON writer that keeps key order and allows the overlay widget's duplicate "version" key --------------------------
class Obj:
    def __init__(self, *pairs):
        self.pairs = list(pairs)


def dump(v, ind=0):
    sp = "  " * ind
    pairs = v.pairs if hasattr(v, "pairs") else list(v.items()) if isinstance(v, dict) else None
    if pairs is not None:
        if not pairs:
            return "{}"
        return "{\n%s\n%s}" % (",\n".join("%s  %s: %s" % (sp, dump(k), dump(x, ind + 1)) for k, x in pairs), sp)
    if isinstance(v, list):
        return "[]" if not v else "[\n%s\n%s]" % (",\n".join(sp + "  " + dump(x, ind + 1) for x in v), sp)
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v) if isinstance(v, float) else str(v)
    return '"%s"' % str(v).replace("\\", "\\\\").replace('"', '\\"')


# ---- TUI building blocks --------------------------------------------------------------------------------------------------
def B(x, y, w, h):
    return "%d %d %d %d" % (round(x), round(y), round(w), round(h))


def bounds(b, focus="No", visible="Always", ind=None):
    d = {"version": 2 if ind else 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": "Show", "whenVisible": visible,
         "boundsType": "Absolute", "bounds": b}
    if ind:
        d["additionalInvalidatingHandles"] = ind
    return d


def entry(name, typ, data, b, remap=None, focus="No", visible="Always", ind=None):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": remap}] if remap else []},
            "bounds": bounds(b, focus, visible, ind)}


def bgd(colour="0", image=""):
    return {"version": 1, "focussed": {"version": 1, "colour": colour, "image": image},
            "unfocussed": {"version": 1, "colour": colour, "image": image}}


def component(children, actions=None, ignore=False, background=None):
    return {"version": 2, "actions": actions or [], "backgroundData": background or bgd(), "ignoreMousePresses": ignore,
            "disableCoarseDataWheel": False, "componentsData": children}


def act(on, handler, extra=""):
    return {"version": 1, "onAction": on, "handler": handler, "handleName": "Data", "additionalData": extra}


def text_style(h, colour, just, style="SemiBold"):
    return {"version": 1, "font": {"version": 1, "name": FONT, "style": style, "height": h}, "colour": colour,
            "justification": just, "case": "Original"}


# ---- assets ---------------------------------------------------------------------------------------------------------------
_made = {}


def asset(stem, make, *args):
    name = "%s_%s.png" % (stem, hashlib.md5(repr(args).encode()).hexdigest()[:8])
    if name not in _made:
        im = make(*args)
        im.save(os.path.join(OUT, name))
        _made[name] = im
    return name


def img(name):
    return _made[name]


def clear(w, h, a=3):   # alpha 3: invisible but still a touch area (fully transparent images have none)
    return asset("clear", lambda w, h, a: Image.new("RGBA", (w, h), (255, 255, 255, a)), w, h, a)


def glow(w, h, colour):
    def make(w, h, colour):
        c = A.Canvas(w, h)
        c.rect((1, 1, w - 1, h - 1), 8, fill=A.rgba(colour, 46), outline=A.rgba(colour, 220), width=2)
        return c.image()
    return asset("glow", make, w, h, colour)


# ---- reusable components --------------------------------------------------------------------------------------------------
DEFS = {}
FOCUS = {"version": 1, "backgroundColour": "12ffffff", "outlineColour": hexc(A.GOLD), "backgroundInset": 0.0, "outlineThickness": 1.5}
SLIDER_FRAMES = 101


def slider_def(w, h, colour):
    name = "ldslider_%dx%d_%s" % (w, h, hexc(colour))
    strip = asset("slider", A.slider_strip, w, h, SLIDER_FRAMES, colour)
    if name not in DEFS:
        DEFS[name] = component([
            entry("focus", "Focus", FOCUS, B(-4, -3, w + 8, h + 6), visible="WhenFocussed"),
            entry("strip", "Knob", {"version": 3, "knobType": "FilmStrip", "filmStrip": strip, "numFrames": SLIDER_FRAMES, "invert": False,
                                    "dragOrientation": "Horizontal", "handleName": "Data"}, B(0, 0, w, h)),
        ], actions=[act("Mouse Down", "Q-Link"), act("Double Click", "Show Overlay", "knob overlay"),
                    act("Enter Pressed", "Show Overlay", "knob overlay")])
    return name, strip


def overlay_def():
    return component([entry("overlay", "KnobOverlay", Obj(
        ("version", 2), ("version", 1),
        ("actions", [{"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}]),
        ("overlayType", "Knob With Header"), ("scaleType", "Linear"), ("polarityType", "Unipolar"),
        ("showMinusSign", True), ("handleName", "Data")), "0 0 1280 800", focus="Yes")], ignore=True)


# ---- a page: static art (canvas) + live widgets ------------------------------------------------------------------------------
class Page:
    def __init__(self, tab, fn):
        self.tab, self.fn = tab, fn
        self.c = A.page_bg(W, H)
        self.kids, self.groups, self.qrows = [], [], []
        self.sample = {}

    # live text
    def text(self, key, box, px, colour=A.CREAM, just="left", sample="", style="SemiBold"):
        x0, y0, x1, y1 = box
        j = {"left": "left verticallyCentred", "centre": "horizontallyCentred verticallyCentred", "right": "right verticallyCentred"}[just]
        self.kids.append(entry(key, "Label", {"version": 1, "textStyle": text_style(float(round(px / TUI_PX)), hexc(colour), j, style),
                                              "type": "Value", "handleName": "Data"}, B(x0, y0, x1 - x0, y1 - y0), remap=P(key)))
        self.sample[key + "$"] = sample

    def button(self, key, x, y, on, off, bid=1, group=1, sample=False):
        w, h = img(off).size
        self.kids.append(entry(key, "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": bid, "numButtonsInGroup": group,
                                               "handleName": "Data", "gestureBehaviour": "Instant"}, B(x, y, w, h), remap=P(key)))
        if group > 1:
            self.sample.setdefault(key, None)
        else:
            self.sample[key] = sample

    def key(self, key, x, y, w, h, caption="", icon=None, colour=A.GOLD, toggle=False, solid=False, px=15, sample=False):
        """A key at (x, y) w x h (+6 px lip). Toggles light up; momentaries sink while pressed."""
        off = asset("key", A.key, w, h, caption, icon, colour, "off", px, solid)
        on = asset("key", A.key, w, h, caption, icon, colour, "on" if toggle else "pressed", px, solid)
        self.button(key, x, y, on, off, sample=sample)

    def segments(self, key, x, y, w, h, captions, colour=A.GOLD, px=15, gap=4, sample=0):
        for i, cap in enumerate(captions):
            on = asset("seg", A.seg, w, h, cap, colour, True, px)
            off = asset("seg", A.seg, w, h, cap, colour, False, px)
            self.button(key, x + i * (w + gap), y, on, off, bid=i, group=len(captions))
        self.sample[key] = sample

    def slider(self, key, x, y, w, h, colour=A.GOLD, sample=0.5):
        comp, strip = slider_def(w, h, colour)
        self.kids.append(entry(key, comp, {"version": 1, "handleName": "Data"}, B(x, y, w, h), remap=P(key), focus="Yes"))
        self.sample[key] = ("slider", strip, sample, w, h)

    def strip(self, key, x, y, name, frames, sample=1):
        w, h = img(name).size[0], img(name).size[1] // frames
        self.kids.append(entry(key, "Image", {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": name, "numFrames": frames,
                                              "handleName": "Data"}, B(x, y, w, h), remap=P(key)))
        self.sample[key] = ("strip", name, sample, w, h)

    def qgroup(self, rect, keys):
        assert len(keys) <= 4 and len(self.groups) < 4
        self.groups.append(B(rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]))
        self.qrows.append(keys)


# ---- shared: the header on every tab ------------------------------------------------------------------------------------------
def header(p, status="Kit generated", lib="2,184 samples"):
    c = p.c
    A.wordmark(c, 22, 44)
    A.well(c, (330, 16, 1030, 72), 10)
    c.ellipse((348, 38, 360, 50), fill=A.rgba(A.FAMILY["perc"]))
    p.text("status", (370, 18, 840, 70), 22, A.CREAM, sample=status)
    p.text("lib_info", (840, 18, 1014, 70), 17, A.DIM, just="right", sample=lib)
    p.key("generate", 1046, 14, 216, 54, "GENERATE ALL", "dice", A.GOLD, solid=True, px=17)


# ---- PADS 1-8 / PADS 9-16 ----------------------------------------------------------------------------------------------------
NAMES = ["DH Kick 04", "Snare MSXII Yo", "TH Snare Magic", "SH Perc 12", "TH Hat 10", "DH Cld Hat 2", "Magic HiHat", "NT Perc 7",
         "Sub Boom 808", "Vinyl Clap", "Rim Tick", "Warm Rhodes Cm", "Vox Hey!", "Glitch 05", "Long Pad Am", "Ride Bell"]
KINDS = [1, 2, 2, 10, 5, 6, 5, 10, 17, 4, 3, 20, 16, 15, 22, 12]
TILE_W, TILE_H = 302, 256


def pads_page(tab, fn, first):
    p = Page(tab, fn)
    header(p)
    c = p.c
    kstrip = asset("kind", A.kind_strip, 222, 34)
    for i in range(8):
        n = first + i
        col, row = i % 4, i // 4
        tx, ty = 16 + col * (TILE_W + 12), 90 + row * (TILE_H + 12)
        A.card(c, (tx, ty, tx + TILE_W, ty + TILE_H), r=14)
        # pad number and what it holds
        A.well(c, (tx + 12, ty + 12, tx + 62, ty + 46), 7)
        c.text((tx + 37, ty + 30), str(n), 24, A.rgba(A.CREAM), weight="Bold")
        p.strip("pad%d_kind" % n, tx + 68, ty + 12, kstrip, 25, sample=KINDS[n - 1])
        # sample name: tap it to hear the pad
        A.well(c, (tx + 12, ty + 56, tx + TILE_W - 12, ty + 106), 9)
        A.icon(c, "play", tx + TILE_W - 34, ty + 81, 16, A.rgba(A.FAINT))
        p.text("pad%d_name" % n, (tx + 26, ty + 58, tx + TILE_W - 50, ty + 104), 21, A.CREAM, sample=NAMES[n - 1])
        p.button("pad%d_play" % n, tx + 12, ty + 56, glow(TILE_W - 24, 50, A.GOLD), clear(TILE_W - 24, 50))
        # pool and gain
        c.text((tx + 14, ty + 126), "DRAWS", 12, A.rgba(A.FAINT), anchor="lm", weight="Bold", spacing=1.4)
        p.text("pad%d_pill" % n, (tx + 70, ty + 114, tx + 180, ty + 138), 15, A.DIM, sample=A.CATS[KINDS[n - 1] - 1][0].split()[0])
        c.text((tx + TILE_W - 58, ty + 126), "GAIN", 12, A.rgba(A.FAINT), anchor="rm", weight="Bold", spacing=1.4)
        p.text("pad%d_gain" % n, (tx + TILE_W - 56, ty + 114, tx + TILE_W - 14, ty + 138), 16, A.CREAM, just="right", sample="100")
        p.slider("pad%d_gain" % n, tx + 12, ty + 144, TILE_W - 24, 32, A.GOLD, sample=0.5)
        # lock and reroll
        kw = (TILE_W - 24 - 10) // 2
        p.key("pad%d_lock" % n, tx + 12, ty + 192, kw, 46, "LOCK", "lock", A.GOLD, toggle=True, sample=(n in (1, 4)))
        p.key("pad%d_reroll" % n, tx + 22 + kw, ty + 192, kw, 46, "REROLL", "dice", A.FAMILY["tonal"])
    for row in range(2):
        ty = 90 + row * (TILE_H + 12)
        p.qgroup((16, ty + 138, 1264, ty + 182), ["pad%d_gain" % (first + row * 4 + k) for k in range(4)])
    return p


# ---- PAD EDIT -----------------------------------------------------------------------------------------------------------------
CAT_ROWS = [("DRUMS", ["kick", "snare", "rim", "clap", "tom", "conga", "percussion"]),
            ("METALS", ["hat", "closed_hat", "open_hat", "crash", "ride", "cymbal"]),
            ("TONAL", ["bass", "synth", "stab", "chord", "lead", "pad"]),
            ("TEXTURE", ["fx", "glitch", "vox", "other"])]


def pad_key(n, lit):
    def make(n, lit):
        return A.key(86, 50, str(n), None, A.GOLD, "on" if lit else "off", 22)
    return asset("padkey", make, n, lit)


def dot_strip():
    def make():
        frames, w, h = 25, 86, 7
        s = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
        for k in range(frames):
            col = (70, 62, 98) if k == 0 else A.FAMILY["missing"] if k == 24 else A.FAMILY[A.CATS[k - 1][1]]
            cc = A.Canvas(w, h)
            cc.rect((6, 1, w - 6, h - 1), 3, fill=A.rgba(col))
            s.alpha_composite(cc.image(), (0, k * h))
        return s
    return asset("kinddot", make)


def edit_page():
    p = Page("PAD EDIT", 2)
    header(p, status="Pad 7 selected")
    c = p.c
    # left: the 16 pads as on the MPC (pad 1 bottom left), tap to select; a colour bar under each shows what it holds
    A.card(c, (16, 90, 420, 620), "PADS", accent=A.GOLD)
    dots = dot_strip()
    for n in range(1, 17):
        r, col = 3 - (n - 1) // 4, (n - 1) % 4
        x, y = 34 + col * 94, 128 + r * 76
        p.button("sel_pad", x, y, pad_key(n, True), pad_key(n, False), bid=n - 1, group=16)
        p.strip("pad%d_kind" % n, x, y + 59, dots, 25, sample=KINDS[n - 1])
    p.sample["sel_pad"] = 6
    p.key("follow", 34, 438, 368, 40, "FOLLOW PLAYED PAD", "follow", A.FAMILY["perc"], toggle=True, sample=True)
    c.text((34, 506), "KIT", 14, A.rgba(A.DIM), anchor="lm", weight="Bold", spacing=1.6)
    p.key("clear_all", 34, 520, 179, 38, "CLEAR ALL", "clear", A.FAMILY["kick"], px=14)
    p.key("normalise", 223, 520, 179, 38, "MATCH LEVELS", "level", A.FAMILY["tonal"], px=14)
    p.key("unlock_all", 34, 568, 179, 38, "UNLOCK ALL", "unlock", A.GOLD, px=14)
    p.key("export", 223, 568, 179, 38, "EXPORT KIT", "export", A.GOLD, solid=True, px=14)

    # right top: the selected pad
    A.card(c, (436, 90, 1264, 358), "SELECTED PAD", accent=A.GOLD)
    big = asset("kindbig", A.kind_strip, 300, 40, True)
    p.strip("sel_kind", 456, 124, big, 25, sample=5)
    c.text((786, 144), "PAD", 14, A.rgba(A.FAINT), anchor="lm", weight="Bold", spacing=1.6)
    p.text("sel_pad", (830, 124, 900, 164), 28, A.CREAM, sample="7", style="Bold")
    p.key("sel_prev", 1132, 122, 52, 40, "", "left", A.CREAM)
    p.key("sel_next", 1192, 122, 52, 40, "", "right", A.CREAM)
    A.well(c, (456, 176, 1244, 232), 10)
    A.icon(c, "play", 1216, 204, 20, A.rgba(A.FAINT))
    p.text("sel_name", (474, 178, 1190, 230), 26, A.CREAM, sample="TechHouse-HiHat-Magic 03")
    p.button("sel_play", 456, 176, glow(788, 56, A.GOLD), clear(788, 56))
    c.text((458, 260), "GAIN", 14, A.rgba(A.FAINT), anchor="lm", weight="Bold", spacing=1.6)
    p.slider("sel_gain", 520, 244, 600, 34, A.GOLD, sample=0.5)
    p.text("sel_gain", (1136, 244, 1244, 278), 20, A.CREAM, just="right", sample="100 %")
    kw = (788 - 4 * 10) // 5
    for i, (k, cap, ic, colr, tog) in enumerate([("sel_lock", "LOCK", "lock", A.GOLD, True), ("sel_reroll", "REROLL", "dice", A.FAMILY["tonal"], False),
                                                ("sel_clear", "CLEAR", "clear", A.FAMILY["kick"], False), ("sel_fav", "FAVOURITE", "star", A.GOLD, False),
                                                ("sel_reject", "REJECT", "x", A.FAMILY["missing"], False)]):
        p.key(k, 456 + i * (kw + 10), 298, kw, 44, cap, ic, colr, toggle=tog, px=15)
    p.qgroup((450, 238, 1250, 284), ["sel_gain", "sel_pad", "sel_lock", "follow"])

    # right bottom: which categories this pad draws from, by family
    A.card(c, (436, 370, 1264, 620), "THIS PAD DRAWS FROM", accent=A.FAMILY["perc"])
    c.text((1246, 393), "tap to add or remove", 13, A.rgba(A.FAINT), anchor="rm")
    lit = {"hat", "closed_hat", "open_hat"}
    for r, (fam, cats) in enumerate(CAT_ROWS):
        y = 412 + r * 50
        c.text((456, y + 20), fam, 13, A.rgba(A.DIM), anchor="lm", weight="Bold", spacing=1.4)
        for i, ck in enumerate(cats):
            k = A.CAT_KEYS.index(ck)
            cap, famc = A.CATS[k]
            p.key("sel_cat_" + ck, 556 + i * 98, y, 92, 38, cap.replace("CLOSED ", "CL ").replace("OPEN ", "OP "), None, A.FAMILY[famc],
                  toggle=True, px=14, sample=ck in lit)
    return p


# ---- SETTINGS ---------------------------------------------------------------------------------------------------------------
def folder_rows(p, x0, x1, keypre, colour, loc_sample, sub_sample):
    """LOCATION and INSIDE rows: a well with the folder name and arrow keys (INSIDE also jumps by 10)."""
    c = p.c
    c.text((x0, 140), "LOCATION", 13, A.rgba(A.FAINT), anchor="lm", weight="Bold", spacing=1.4)
    p.key(keypre + "_loc_prev", x0, 152, 46, 46, "", "left", A.CREAM)
    p.key(keypre + "_loc_next", x1 - 46, 152, 46, 46, "", "right", A.CREAM)
    A.well(c, (x0 + 54, 152, x1 - 54, 198), 9)
    A.icon(c, "folder", x0 + 76, 175, 20, A.rgba(colour))
    p.text(keypre + "_loc_name", (x0 + 96, 154, x1 - 64, 196), 21, A.CREAM, sample=loc_sample)
    c.text((x0, 226), "INSIDE", 13, A.rgba(A.FAINT), anchor="lm", weight="Bold", spacing=1.4)
    p.key(keypre + "_sub_prev10", x0, 238, 46, 46, "", "left2", A.DIM)
    p.key(keypre + "_sub_prev", x0 + 52, 238, 46, 46, "", "left", A.CREAM)
    p.key(keypre + "_sub_next", x1 - 98, 238, 46, 46, "", "right", A.CREAM)
    p.key(keypre + "_sub_next10", x1 - 46, 238, 46, 46, "", "right2", A.DIM)
    A.well(c, (x0 + 106, 238, x1 - 106, 284), 9)
    p.text(keypre + "_sub_name", (x0 + 120, 240, x1 - 116, 282), 20, A.CREAM, sample=sub_sample)
    c.text((x0 + 26, 298), "-10", 11, A.rgba(A.FAINT), anchor="mm", weight="Bold")
    c.text((x1 - 23, 298), "+10", 11, A.rgba(A.FAINT), anchor="mm", weight="Bold")


def settings_page():
    p = Page("SETTINGS", 3)
    header(p, status="Library ready")
    c = p.c
    # where samples come from
    A.card(c, (16, 90, 636, 400), "SAMPLES FROM", accent=A.FAMILY["tonal"])
    folder_rows(p, 34, 618, "src", A.FAMILY["tonal"], "Default (auto)", "Whole location")
    c.text((34, 322), "Default (auto) = the Expansions and Samples folders on every drive.", 13, A.rgba(A.DIM), anchor="lm")
    A.well(c, (34, 342, 400, 388), 9)
    p.text("lib_info", (50, 344, 390, 386), 20, A.CREAM, sample="2,184 samples")
    p.key("rescan", 412, 342, 206, 46, "RESCAN", "rescan", A.FAMILY["tonal"], solid=True, px=16)
    # where kits go
    A.card(c, (644, 90, 1264, 400), "EXPORT KITS TO", accent=A.GOLD)
    folder_rows(p, 662, 1246, "exp", A.GOLD, "MPC Documents", "Lucky Dip")
    c.text((662, 322), "SAMPLES", 13, A.rgba(A.FAINT), anchor="lm", weight="Bold", spacing=1.4)
    p.segments("export_mode", 760, 304, 240, 36, ["COPY INTO KIT", "LINK TO ORIGINALS"], A.GOLD, px=14, sample=0)
    A.well(c, (662, 352, 1032, 392), 9)
    c.text((676, 372), "LAST", 11, A.rgba(A.FAINT), anchor="lm", weight="Bold", spacing=1.2)
    p.text("export_name", (714, 354, 1026, 390), 17, A.CREAM, sample="LuckyDip-1112-201544")
    p.key("export", 1044, 348, 202, 44, "EXPORT KIT", "export", A.GOLD, solid=True, px=16)
    # how the library is filtered and kits are drawn
    A.card(c, (16, 412, 1264, 620), "LIBRARY RULES", accent=A.FAMILY["perc"])
    p.key("skip_loops", 34, 450, 230, 46, "SKIP LOOPS", "loop", A.FAMILY["perc"], toggle=True, sample=True)
    c.text((280, 465), "Leave out loops: names with \"loop\", a BPM", 14, A.rgba(A.DIM), anchor="lm")
    c.text((280, 485), "tag (120 bpm) and loop folders.", 14, A.rgba(A.DIM), anchor="lm")
    p.key("prevent_dup", 660, 450, 230, 46, "NO DUPLICATES", "dup", A.FAMILY["perc"], toggle=True, sample=True)
    c.text((906, 465), "One sample on one pad only (unless", 14, A.rgba(A.DIM), anchor="lm")
    c.text((906, 485), "a pad's categories run dry).", 14, A.rgba(A.DIM), anchor="lm")
    c.text((34, 548), "MAX FILE SIZE", 13, A.rgba(A.FAINT), anchor="lm", weight="Bold", spacing=1.4)
    p.segments("max_size", 34, 562, 88, 40, ["OFF", "1 MB", "2 MB", "5 MB", "10 MB", "20 MB"], A.FAMILY["perc"], px=14, sample=3)
    c.text((588, 582), "Leave out files bigger than this.", 14, A.rgba(A.DIM), anchor="lm")
    c.text((588, 600), "RESCAN after changing a rule.", 13, A.rgba(A.FAINT), anchor="lm")
    p.key("unlock_all", 1032, 560, 214, 44, "UNLOCK ALL PADS", "unlock", A.GOLD, px=14)
    p.qgroup((22, 440, 1258, 612), ["skip_loops", "prevent_dup", "max_size", "export_mode"])
    return p


# ---- preview: the background plus the widgets at sample values (checks that the live parts land on the art) ------------------
def render_preview(p, bgname):
    from PIL import ImageDraw
    im = Image.open(os.path.join(OUT, bgname)).convert("RGBA")
    d = ImageDraw.Draw(im)
    radio_seen = {}
    for k in p.kids:
        cd = k["componentData"]
        x, y, w, h = [int(v) for v in k["bounds"]["bounds"].split()]
        typ, data = cd["type"], cd["data"]
        remap = k["handle remapping"]["map"][0]["value"] if k["handle remapping"]["map"] else None
        key = KEYS[int(remap.split()[-1])] if remap else None
        smp = p.sample.get(key)
        if typ == "Button":
            if data["numButtonsInGroup"] > 1:
                lit = smp == data["buttonId"]
            else:
                lit = bool(smp)
            im.alpha_composite(img(data["onImage" if lit else "offImage"]), (x, y))
        elif typ == "Label":
            ts = data["textStyle"]
            col = tuple(int(ts["colour"][i:i + 2], 16) for i in (2, 4, 6))
            f = A.font(round(ts["font"]["height"] * TUI_PX), ts["font"]["style"])
            txt = p.sample.get(key + "$", "")
            jl = ts["justification"].split()[0]
            ax = {"left": (x, "lm"), "right": (x + w, "rm")}.get(jl, (x + w / 2, "mm"))
            d.text((ax[0], y + h / 2), txt, font=f, fill=col, anchor=ax[1])
        elif isinstance(smp, tuple) and smp[0] in ("slider", "strip"):
            _, name, val, sw, sh = smp
            sim = img(name)
            frames = sim.size[1] // sh
            fi = int(round(val * (frames - 1))) if smp[0] == "slider" else int(val)
            im.alpha_composite(sim.crop((0, fi * sh, sw, fi * sh + sh)), (x, y))
    # Q-Link groups, as the MPC's overlay draws them
    for g in p.groups:
        x, y, w, h = [int(v) for v in g.split()]
        d.rectangle([x, y, x + w, y + h], outline=(80, 200, 255, 160), width=1)
    return im


def main():
    preview = "--preview" in sys.argv
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    pages = [pads_page("PADS 1-8", 0, 1), pads_page("PADS 9-16", 1, 9), edit_page(), settings_page()]
    tabs, qmaps = [], []
    if preview:
        os.makedirs(PREVIEW, exist_ok=True)
    for p in pages:
        slug = p.tab.lower().replace(" ", "_").replace("-", "_")
        bgname = "bg_%s.png" % slug
        p.c.image().convert("RGB").save(os.path.join(OUT, bgname))
        kids = [entry("bg", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bgname}, "0 0 1280 628")] + p.kids
        tabs.append({"version": 1, "tabName": p.tab, "fnKeyIndex": p.fn, "fnKeySubIndex": 0,
                     "componentDefinition": component(kids, background=bgd(hexc(A.INK))), "qlinkBoundsData": p.groups})
        q = {"Q-Link %d" % n: -1 for n in range(1, 17)}
        for gi, keys in enumerate(p.qrows):
            for i, key in enumerate(keys):
                q["Q-Link %d" % (13 - 4 * gi + i)] = KEYS.index(key)
        qmaps.append({"Tab": p.fn + 1, "SubTab": 1, "Bank Direction": "RowTopDown", "Q-Links": q})
        if preview:
            render_preview(p, bgname).convert("RGB").save(os.path.join(PREVIEW, slug + ".png"))
    repl = [{"key": P(k), "value": v} for k, v in (("export_mode", ["Copy samples", "Link to originals"]), ("skip_loops", ["OFF", "ON"]),
                                                   ("max_size", ["Off", "1 MB", "2 MB", "5 MB", "10 MB", "20 MB"]),
                                                   ("prevent_dup", ["OFF", "ON"]), ("follow", ["OFF", "ON"]))]
    defs = [{"key": "knob overlay", "value": overlay_def()}] + [{"key": k, "value": v} for k, v in DEFS.items()]
    tui = {"pageData": {"version": 1,
                        "componentDefinitions": {"version": 2, "importFiles": [], "localComponentDefinitions": defs},
                        "replacementStrings": repl, "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    text = dump(tui) + "\n"
    assert "object at 0x" not in text, "a Python object leaked into TUI.json"
    open(os.path.join(OUT, "TUI.json"), "w").write(text)
    ql = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
          "Screen Mode Q-Links": {"version": 4, "map": qmaps}, "Program Mode Q-Links": {"Q-Link %d" % n: -1 for n in range(1, 17)}}
    open(os.path.join(OUT, "Q-Links.json"), "w").write(dump(ql) + "\n")
    print("wrote %s - %d tabs, %d files" % (OUT, len(tabs), len(os.listdir(OUT))))


if __name__ == "__main__":
    import make_tui   # run inside the importable module, not __main__ (a module imported twice keeps two registries)
    make_tui.main()
