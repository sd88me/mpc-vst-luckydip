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
        c.rect((1, 1, w - 1, h - 1), 10, fill=A.rgba(colour, 50), outline=A.rgba(colour, 230), width=2.5)
        return c.image()
    return asset("glow", make, w, h, colour)


# ---- reusable components --------------------------------------------------------------------------------------------------
DEFS = {}
FOCUS = {"version": 1, "backgroundColour": "12ffffff", "outlineColour": hexc(A.GOLD), "backgroundInset": 0.0, "outlineThickness": 1.5}
SLIDER_FRAMES = 101


def slider_def(w, h):
    name = "ldslider_%dx%d" % (w, h)
    strip = asset("slider", A.slider_strip, w, h, SLIDER_FRAMES)
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
    def text(self, key, box, px, colour=A.GOLD, just="left", sample="", style="SemiBold"):
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

    def key(self, key, x, y, w, h, caption="", icon=None, candy="pink", toggle=False, px=15, sample=False):
        """A pill key at (x, y) w x h (+6 px shadow). Toggles: dark, lit candy when on; momentaries: candy, sink while pressed."""
        off = asset("key", A.key, w, h, caption, icon, candy, "off", px, toggle)
        on = asset("key", A.key, w, h, caption, icon, candy, "on" if toggle else "pressed", px, toggle)
        self.button(key, x, y, on, off, sample=sample)

    def segments(self, key, x, y, w, h, captions, candy="pink", px=15, gap=6, sample=0):
        for i, cap in enumerate(captions):
            on = asset("seg", A.seg, w, h, cap, candy, True, px)
            off = asset("seg", A.seg, w, h, cap, candy, False, px)
            self.button(key, x + i * (w + gap), y, on, off, bid=i, group=len(captions))
        self.sample[key] = sample

    def slider(self, key, x, y, w, h, sample=0.5):
        comp, strip = slider_def(w, h)
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
    """The 0.9 slot-machine banner on every tab: wordmark, three reels, the status well, GENERATE ALL."""
    c = p.c
    A.banner(c, (6, 4, 1274, 80))
    A.wordmark(c, 26, 43, 44)
    for k, sym in enumerate(("cherry", "seven", "star")):
        A.reel(c, 300 + k * 44, 42, sym, 36)
    A.well(c, (412, 20, 1030, 66), 12)
    p.text("status", (430, 22, 850, 64), 21, A.GOLD, sample=status)
    p.text("lib_info", (850, 22, 1016, 64), 16, A.LAVENDER, just="right", sample=lib)
    p.key("generate", 1044, 17, 214, 48, "GENERATE ALL", "dice", A.K_GEN, px=16)


def small(c, xy, s, anchor="lm", px=12):
    A.caption(c, xy, s, px, anchor=anchor, spacing=1.4)


# ---- PADS 1-8 / PADS 9-16 ----------------------------------------------------------------------------------------------------
NAMES = ["DH Kick 04", "Snare MSXII Yo", "TH Snare Magic", "SH Perc 12", "TH Hat 10", "DH Cld Hat 2", "Magic HiHat", "NT Perc 7",
         "Sub Boom 808", "Vinyl Clap", "Rim Tick", "Warm Rhodes Cm", "Vox Hey!", "Glitch 05", "Long Pad Am", "Ride Bell"]
KINDS = [1, 2, 2, 10, 5, 6, 5, 10, 17, 4, 3, 20, 16, 15, 22, 12]
PILLS = ["KICK", "SNARE", "SNARE", "PERC", "HAT", "C.HAT", "HAT", "PERC", "BASS", "CLAP", "RIM", "CHORD", "VOX", "GLITCH", "PAD", "RIDE"]
TILE_W, TILE_H = 302, 260


def pads_page(tab, fn, first):
    p = Page(tab, fn)
    header(p)
    c = p.c
    kstrip = asset("kind", A.kind_strip, 158, 30)
    for i in range(8):
        n = first + i
        col, row = i % 4, i // 4
        tx, ty = 16 + col * (TILE_W + 12), 90 + row * (TILE_H + 8)
        A.card(c, (tx, ty, tx + TILE_W, ty + TILE_H), "PAD %d" % n, A.TILE_ORDER[i], inner_top=40, sprinkles=True)
        # what it holds (category band) and the sample name: tap the name to hear the pad
        p.strip("pad%d_kind" % n, tx + 112, ty + 6, kstrip, 25, sample=KINDS[n - 1])
        A.well(c, (tx + 16, ty + 48, tx + TILE_W - 16, ty + 94), 10)
        A.icon(c, "play", tx + TILE_W - 36, ty + 71, 16, A.rgba(A.LAV_DIM))
        p.text("pad%d_name" % n, (tx + 28, ty + 50, tx + TILE_W - 52, ty + 92), 20, A.GOLD, sample=NAMES[n - 1])
        p.button("pad%d_play" % n, tx + 16, ty + 48, glow(TILE_W - 32, 46, A.GOLD), clear(TILE_W - 32, 46))
        # pool and gain
        small(c, (tx + 18, ty + 113), "DRAWS")
        A.well(c, (tx + 72, ty + 101, tx + 168, ty + 125), 8)
        p.text("pad%d_pill" % n, (tx + 80, ty + 101, tx + 164, ty + 125), 15, A.GOLD, sample=PILLS[n - 1])
        small(c, (tx + TILE_W - 66, ty + 113), "GAIN", anchor="rm")
        p.text("pad%d_gain" % n, (tx + TILE_W - 62, ty + 101, tx + TILE_W - 18, ty + 125), 17, A.WHITE, just="right", sample="100")
        p.slider("pad%d_gain" % n, tx + 14, ty + 132, TILE_W - 28, 34, sample=0.5)
        # lock and reroll
        kw = (TILE_W - 32 - 10) // 2
        p.key("pad%d_lock" % n, tx + 16, ty + 178, kw, 40, "LOCK", "lock", "mint", toggle=True, sample=(n in (1, 4)))
        p.key("pad%d_reroll" % n, tx + 26 + kw, ty + 178, kw, 40, "REROLL", "dice", A.K_REROLL)
    for row in range(2):
        ty = 90 + row * (TILE_H + 8)
        p.qgroup((16, ty + 128, 1264, ty + 170), ["pad%d_gain" % (first + row * 4 + k) for k in range(4)])
    return p


# ---- PAD EDIT -----------------------------------------------------------------------------------------------------------------
CAT_ROWS = [("DRUMS", ["kick", "snare", "rim", "clap", "tom", "conga", "percussion"]),
            ("METALS", ["hat", "closed_hat", "open_hat", "crash", "ride", "cymbal"]),
            ("TONAL", ["bass", "synth", "stab", "chord", "lead", "pad"]),
            ("TEXTURE", ["fx", "glitch", "vox", "other"])]


def pad_key(n, lit):
    return asset("padkey", lambda n, lit: A.key(86, 48, str(n), None, "yellow", "on" if lit else "off", 22, True), n, lit)


def edit_page():
    p = Page("PAD EDIT", 2)
    header(p, status="Pad 7 selected")
    c = p.c
    # left: the 16 pads as on the MPC (pad 1 bottom left), tap to select; a candy bar under each shows what it holds
    A.card(c, (16, 90, 420, 620), "PADS", "yellow")
    dots = asset("kinddot", A.dot_strip, 86, 7)
    for n in range(1, 17):
        r, col = 3 - (n - 1) // 4, (n - 1) % 4
        x, y = 36 + col * 94, 140 + r * 72
        p.button("sel_pad", x, y, pad_key(n, True), pad_key(n, False), bid=n - 1, group=16)
        p.strip("pad%d_kind" % n, x, y + 56, dots, 25, sample=KINDS[n - 1])
    p.sample["sel_pad"] = 6
    p.key("follow", 36, 432, 364, 40, "FOLLOW PLAYED PAD", "follow", "mint", toggle=True, sample=True)
    small(c, (38, 500), "KIT", px=14)
    p.key("clear_all", 36, 516, 177, 38, "CLEAR ALL", "clear", A.K_CLEAR, px=14)
    p.key("normalise", 223, 516, 177, 38, "MATCH LEVELS", "level", "blue", px=14)
    p.key("unlock_all", 36, 564, 177, 38, "UNLOCK ALL", "unlock", "violet", px=14)
    p.key("export", 223, 564, 177, 38, "EXPORT KIT", "export", "orange", px=14)

    # right top: the selected pad
    A.card(c, (436, 90, 1264, 358), "SELECTED PAD", "pink")
    big = asset("kindbig", A.kind_strip, 230, 34, True)
    p.strip("sel_kind", 660, 93, big, 25, sample=5)
    small(c, (930, 110), "PAD", px=14)
    p.text("sel_pad", (970, 92, 1030, 128), 26, A.WHITE, sample="7", style="Bold")
    p.key("sel_prev", 1072, 146, 80, 42, "", "left", "violet")
    p.key("sel_next", 1164, 146, 80, 42, "", "right", "violet")
    A.well(c, (456, 140, 1056, 194), 12)
    A.icon(c, "play", 1030, 167, 18, A.rgba(A.LAV_DIM))
    p.text("sel_name", (472, 142, 1010, 192), 25, A.GOLD, sample="TechHouse-HiHat-Magic 03")
    p.button("sel_play", 456, 140, glow(600, 54, A.GOLD), clear(600, 54))
    small(c, (458, 224), "GAIN", px=14)
    p.slider("sel_gain", 520, 207, 600, 36, sample=0.5)
    p.text("sel_gain", (1136, 207, 1244, 243), 20, A.WHITE, just="right", sample="100")
    kw = (788 - 4 * 10) // 5
    for i, (k, cap, ic, cd, tog) in enumerate([("sel_lock", "LOCK", "lock", "mint", True), ("sel_reroll", "REROLL", "dice", A.K_REROLL, False),
                                              ("sel_clear", "CLEAR", "clear", A.K_CLEAR, False), ("sel_fav", "FAVOURITE", "star", A.K_FAV, False),
                                              ("sel_reject", "REJECT", "x", A.K_REJECT, False)]):
        p.key(k, 456 + i * (kw + 10), 262, kw, 44, cap, ic, cd, toggle=tog, px=15)
    small(c, (458, 330), "TAP THE NAME TO HEAR THE PAD.  FAVOURITES COME UP TWICE AS OFTEN, REJECTS NEVER.", px=11)
    p.qgroup((450, 202, 1250, 248), ["sel_gain", "sel_pad", "sel_lock", "follow"])

    # right bottom: which categories this pad draws from, by family
    A.card(c, (436, 370, 1264, 620), "THIS PAD DRAWS FROM", "purple")
    small(c, (1210, 390), "TAP TO ADD OR REMOVE", anchor="rm", px=11)
    lit = {"hat", "closed_hat", "open_hat"}
    for r, (fam, cats) in enumerate(CAT_ROWS):
        y = 422 + r * 48
        small(c, (458, y + 19), fam, px=13)
        for i, ck in enumerate(cats):
            k = A.CAT_KEYS.index(ck)
            cap, famc = A.CATS[k]
            p.key("sel_cat_" + ck, 556 + i * 98, y, 92, 36, cap.replace("CLOSED ", "CL ").replace("OPEN ", "OP "), None, A.FAMILY[famc],
                  toggle=True, px=13, sample=ck in lit)
    return p


# ---- SETTINGS ---------------------------------------------------------------------------------------------------------------
def folder_rows(p, x0, x1, keypre, loc_sample, sub_sample):
    """LOCATION and INSIDE rows: a well with the folder name and arrow keys (INSIDE also jumps by 10)."""
    c = p.c
    small(c, (x0 + 2, 148), "LOCATION")
    p.key(keypre + "_loc_prev", x0, 160, 46, 42, "", "left", "violet")
    p.key(keypre + "_loc_next", x1 - 46, 160, 46, 42, "", "right", "violet")
    A.well(c, (x0 + 54, 160, x1 - 54, 204), 12)
    A.icon(c, "folder", x0 + 76, 182, 20, A.rgba(A.GOLD))
    p.text(keypre + "_loc_name", (x0 + 96, 162, x1 - 64, 202), 20, A.GOLD, sample=loc_sample)
    small(c, (x0 + 2, 230), "INSIDE")
    p.key(keypre + "_sub_prev10", x0, 242, 46, 42, "", "left2", "purple")
    p.key(keypre + "_sub_prev", x0 + 52, 242, 46, 42, "", "left", "violet")
    p.key(keypre + "_sub_next", x1 - 98, 242, 46, 42, "", "right", "violet")
    p.key(keypre + "_sub_next10", x1 - 46, 242, 46, 42, "", "right2", "purple")
    A.well(c, (x0 + 106, 242, x1 - 106, 286), 12)
    p.text(keypre + "_sub_name", (x0 + 120, 244, x1 - 116, 284), 19, A.GOLD, sample=sub_sample)
    small(c, (x0 + 23, 300), "-10", anchor="mm", px=11)
    small(c, (x1 - 23, 300), "+10", anchor="mm", px=11)


def settings_page():
    p = Page("SETTINGS", 3)
    header(p, status="Library ready")
    c = p.c
    # where samples come from
    A.card(c, (16, 90, 636, 404), "SAMPLE SOURCE", "blue")
    folder_rows(p, 34, 618, "src", "Default (auto)", "Whole location")
    small(c, (36, 324), "DEFAULT (AUTO) = EXPANSIONS + SAMPLES FOLDERS ON EVERY DRIVE", px=11)
    A.well(c, (34, 342, 400, 386), 12)
    p.text("lib_info", (50, 344, 390, 384), 20, A.GOLD, sample="2,184 samples")
    p.key("rescan", 412, 342, 206, 44, "RESCAN", "rescan", "teal", px=16)
    # where kits go
    A.card(c, (644, 90, 1264, 404), "EXPORT FOLDER", "orange")
    folder_rows(p, 662, 1246, "exp", "MPC Documents", "Lucky Dip")
    small(c, (664, 326), "SAMPLES")
    p.segments("export_mode", 760, 306, 240, 44, ["COPY INTO KIT", "LINK ORIGINALS"], "orange", px=14, sample=0)
    A.well(c, (662, 354, 1032, 392), 12)
    small(c, (676, 373), "LAST", px=11)
    p.text("export_name", (714, 355, 1026, 391), 17, A.GOLD, sample="LuckyDip-1112-201544")
    p.key("export", 1044, 350, 202, 42, "EXPORT KIT", "export", "orange", px=16)
    # how the library is filtered and kits are drawn
    A.card(c, (16, 414, 1264, 620), "LIBRARY", "green")
    p.key("skip_loops", 34, 462, 230, 44, "SKIP LOOPS", "loop", "mint", toggle=True, sample=True)
    A.caption(c, (280, 476), "Leave out loops: names with \"loop\", a BPM", 14, anchor="lm", spacing=0.3)
    A.caption(c, (280, 495), "tag (120 bpm) and loop folders.", 14, anchor="lm", spacing=0.3)
    p.key("prevent_dup", 660, 462, 230, 44, "NO DUPLICATES", "dup", "mint", toggle=True, sample=True)
    A.caption(c, (906, 476), "One sample on one pad only (unless", 14, anchor="lm", spacing=0.3)
    A.caption(c, (906, 495), "a pad's categories run dry).", 14, anchor="lm", spacing=0.3)
    small(c, (36, 540), "MAX FILE SIZE")
    p.segments("max_size", 34, 552, 86, 44, ["OFF", "1 MB", "2 MB", "5 MB", "10 MB", "20 MB"], "green", px=14, sample=3)
    A.caption(c, (588, 568), "Leave out files bigger than this.", 14, anchor="lm", spacing=0.3)
    A.caption(c, (588, 588), "RESCAN after changing these.", 13, anchor="lm", spacing=0.3)
    p.key("unlock_all", 1032, 554, 214, 42, "UNLOCK ALL PADS", "unlock", "violet", px=14)
    p.qgroup((22, 452, 1258, 606), ["skip_loops", "prevent_dup", "max_size", "export_mode"])
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
                     "componentDefinition": component(kids, background=bgd(hexc(A.GRAPE))), "qlinkBoundsData": p.groups})
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
