#!/usr/bin/env python3
"""Offline unit tests for shadow_skin geometry/invariants that don't need the art toolchain or a device:
seg_rects honouring sw=, the two filmstrip frame-count conventions, and html_art's inlined SVGs keeping their ids
and classes apart. No device: python3 tools/test_shadow_skin.py"""
import os
import re
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import shadow_skin  # noqa: E402
import html_art  # noqa: E402


class SegRects(unittest.TestCase):
    def widths(self, w):
        return {r[2] for r in shadow_skin.seg_rects(w)}

    def test_enum_v_defaults_to_135(self):
        w = {"kind": "enum_v", "options": ["A", "B", "C"], "cx": 100, "cy": 100}
        self.assertEqual(self.widths(w), {135})

    def test_enum_v_honours_sw(self):
        w = {"kind": "enum_v", "options": ["A", "B", "C"], "cx": 100, "cy": 100, "sw": 56}
        self.assertEqual(self.widths(w), {56})

    def test_enum_h_defaults_to_117(self):
        w = {"kind": "enum_h", "options": ["A", "B"], "cx": 100, "cy": 100}
        self.assertEqual(self.widths(w), {117})

    def test_enum_h_honours_sw(self):
        w = {"kind": "enum_h", "options": ["A", "B"], "cx": 100, "cy": 100, "sw": 68}
        self.assertEqual(self.widths(w), {68})


class QLinkBounds(unittest.TestCase):
    TAB = {"widgets": [{"kind": "knob", "key": k, "cx": 100 + 150 * i, "cy": 300, "r": 30}
                       for i, k in enumerate("abcdefgh")]}

    def tearDown(self):
        shadow_skin.apply_theme([])

    def test_outlines_are_opt_in(self):
        shadow_skin.apply_theme([])
        self.assertFalse(shadow_skin.QLINK_COLUMNS)
        shadow_skin.apply_theme(["qlink_bounds=column"])
        self.assertTrue(shadow_skin.QLINK_COLUMNS)
        shadow_skin.apply_theme([])
        self.assertFalse(shadow_skin.QLINK_COLUMNS)

    def test_one_rect_per_column_empty_slots_skipped(self):
        rects = shadow_skin.qlink_column_bounds(self.TAB, list("abcd") + ["-"] * 4 + list("efgh"))
        self.assertEqual(len(rects), 3)
        self.assertEqual(rects[1], "0 0 0 0")
        x0 = [int(r.split()[0]) for r in (rects[0], rects[2])]
        self.assertLess(x0[0], x0[1])
        self.assertEqual(len(shadow_skin.qlink_column_bounds(self.TAB, list("abcd") + ["-"] * 4)), 1)


class StudioPreviewTabs(unittest.TestCase):
    """studio.preview must read a skin in either shape: release.sh previews whatever the build wrote."""

    def test_tab_components_reads_both_shapes(self):
        import studio
        kids = [{"componentData": {"type": "Image"}}]
        named = {"tabName": "A", "componentName": "A|A"}
        inline = {"tabName": "A", "componentDefinition": {"componentsData": kids}}
        self.assertEqual(studio.tab_components(named, {"A|A": {"componentsData": kids}}), kids)
        self.assertEqual(studio.tab_components(inline, {}), kids)

    def test_a_generated_2x_skin_previews_like_a_3x_one(self):
        tui = _gen_like_tui()
        defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        import studio
        before = studio.tab_components(tui["pageData"]["tabs"][0], defs)
        shadow_skin.to_mpc2x(tui)
        after = studio.tab_components(tui["pageData"]["tabs"][0], {})
        self.assertEqual([c["componentData"]["type"] for c in before], [c["componentData"]["type"] for c in after])


class ListOrder(unittest.TestCase):
    W = {"key": "slot", "cols": 2, "rows": 3}

    def keys(self, **kw):
        return [k.split("_")[-1] for k in shadow_skin.list_keys(dict(self.W, **kw))]

    def test_default_numbers_across_each_row(self):
        self.assertEqual(self.keys(), ["1", "2", "3", "4", "5", "6"])

    def test_pads_number_from_the_bottom(self):
        self.assertEqual(self.keys(order="pads"), ["5", "6", "3", "4", "1", "2"])

    def test_cols_number_down_each_column_first(self):
        # tiles are laid out row by row (left, right, left, right ...): the left column holds 1..rows, the right the next rows
        self.assertEqual(self.keys(order="cols"), ["1", "4", "2", "5", "3", "6"])
        self.assertEqual(sorted(self.keys(order="cols")), sorted(self.keys()))   # the same keys, only placed differently

    def test_cols_with_three_columns(self):
        w = {"key": "s", "cols": 3, "rows": 2, "order": "cols"}
        self.assertEqual([k.split("_")[-1] for k in shadow_skin.list_keys(w)], ["1", "3", "5", "2", "4", "6"])


class FilmStripFrames(unittest.TestCase):
    def test_rotary_knob_is_one_fewer_than_strip(self):
        self.assertEqual(shadow_skin.ROT_FRAMES, shadow_skin.FRAMES - 1)

    def test_slider_and_meter_equal_strip_length(self):
        # the (l)sstrip generator emits exactly FRAMES frames for sliders and meters, so their FilmStrip
        # numFrames must match it; FRAMES-1 here is the second-thumb bug.
        self.assertEqual(shadow_skin.STRIP_FRAMES, shadow_skin.FRAMES)


def _gen_like_tui():
    """A TUI.json shaped like write_skin's output (MPC OS 3.x): one tab pointing at a local page definition, one
    widget definition holding a Knob and a Button, version 4 definitions, tab 3, Knob 5, Button 2."""
    def child(ctype, data):
        return {"version": 2, "componentData": {"version": 1, "name": ctype, "type": ctype, "data": data},
                "handle remapping": {"version": 1, "map": []},
                "bounds": {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show",
                           "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 10 10",
                           "additionalInvalidatingHandles": []}}
    bg = {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""},
          "unfocussed": {"version": 1, "colour": "0", "image": ""}}

    action = {"version": 2, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data",
              "additionalData": "", "handle remapping": {"version": 1, "map": []}}

    def definition(kids):
        return {"version": 4, "actions": [dict(action)], "backgroundData": bg, "ignoreMousePresses": False,
                "disableCoarseDataWheel": False, "repeats": False, "hideQLinkBounds": False, "componentsData": kids}
    widget = definition([
        child("Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "k.png", "numFrames": 127,
                       "invert": False, "dragOrientation": "Vertical", "handleName": "Data"}),
        child("Button", {"version": 2, "onImage": "a.png", "offImage": "b.png", "buttonId": 1,
                         "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}),
        child("Label", {"version": 1, "type": "Value", "handleName": "Data"})])
    return {"pageData": {"version": 1, "info": {"version": 1, "type": "CompleteDescription"},
            "componentDefinitions": {"version": 2, "importFiles": ["/x/Generic Knob Overlay.json"],
                                     "localComponentDefinitions": [
                                         {"key": "wKnob", "value": widget},
                                         {"key": "GLOBAL|GLOBAL", "value": definition([child("Image", {
                                             "version": 2, "imageType": "Regular", "colour": "0", "image": "bg.png"}),
                                             child("wKnob", {"version": 1, "handleName": "Data"})])}]},
            "tabs": [{"version": 3, "tabName": "GLOBAL", "fnKeyIndex": 0, "fnKeySubIndex": 0,
                      "qlinkBoundsData": ["0 0 0 0"], "componentName": "GLOBAL|GLOBAL",
                      "initialSize": "0 0 1280 628", "scale": 1.0}]}}


def _versions(o, out=None):
    out = [] if out is None else out
    if isinstance(o, dict):
        if "version" in o:
            out.append(o["version"])
        for x in o.values():
            _versions(x, out)
    elif isinstance(o, list):
        for x in o:
            _versions(x, out)
    return out


class ToMpc2x(unittest.TestCase):
    """The MPC OS 2.15.1 shape (docs/NOTES.md, 2026-10-03). Experimental: not yet confirmed on a device."""

    def setUp(self):
        self.tui = shadow_skin.to_mpc2x(_gen_like_tui())
        self.pd = self.tui["pageData"]

    def test_nothing_above_version_2_remains(self):
        self.assertLessEqual(max(_versions(self.tui)), 2)

    def test_tab_is_version_1_with_the_page_inline(self):
        t = self.pd["tabs"][0]
        self.assertEqual(sorted(t), ["componentDefinition", "fnKeyIndex", "fnKeySubIndex", "qlinkBoundsData",
                                     "tabName", "version"])
        self.assertEqual(t["version"], 1)
        self.assertEqual(t["componentDefinition"]["version"], 2)
        self.assertEqual(t["componentDefinition"]["componentsData"][1]["componentData"]["type"], "wKnob")

    def test_inlined_page_leaves_the_local_definitions(self):
        keys = [d["key"] for d in self.pd["componentDefinitions"]["localComponentDefinitions"]]
        self.assertEqual(keys, ["wKnob"])

    def test_definitions_are_version_2_without_the_newer_fields(self):
        for d in [self.pd["componentDefinitions"]["localComponentDefinitions"][0]["value"],
                  self.pd["tabs"][0]["componentDefinition"]]:
            self.assertEqual(d["version"], 2)
            self.assertNotIn("repeats", d)
            self.assertNotIn("hideQLinkBounds", d)
            self.assertIn("disableCoarseDataWheel", d)

    def test_knob_and_button_data_are_version_1(self):
        kids = self.pd["componentDefinitions"]["localComponentDefinitions"][0]["value"]["componentsData"]
        knob, button = kids[0]["componentData"]["data"], kids[1]["componentData"]["data"]
        self.assertEqual(knob, {"version": 1, "knobType": "FilmStrip", "filmStrip": "k.png", "numFrames": 127,
                                "handleName": "Data"})
        self.assertEqual(button, {"version": 1, "onImage": "a.png", "offImage": "b.png", "buttonId": 1,
                                  "numButtonsInGroup": 1, "handleName": "Data"})

    def test_actions_are_version_1_without_handle_remapping(self):
        acts = self.pd["tabs"][0]["componentDefinition"]["actions"]
        self.assertEqual(acts, [{"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data",
                                 "additionalData": ""}])

    def test_a_tab_pointing_at_a_missing_definition_is_an_error(self):
        t = _gen_like_tui()
        t["pageData"]["tabs"][0]["componentName"] = "nope"
        with self.assertRaises(SystemExit):
            shadow_skin.to_mpc2x(t)


class InlinedSvgs(unittest.TestCase):
    SVG = ('<?xml version="1.0"?><svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
           'id="root" width="10" height="10" viewBox="0 0 10 10"><style>.cls-1{fill:url(#g)}</style>'
           '<defs><linearGradient id="g"><stop offset="0" stop-color="%s"/></linearGradient></defs>'
           '<rect class="cls-1" width="10" height="10"/><circle id="c" r="2" fill="url(\'#g\')"/>'
           '<use xlink:href="#c"/><use href="#c"/></svg>')

    def test_two_svgs_sharing_ids_and_classes_stay_apart(self):
        with tempfile.TemporaryDirectory() as d:
            art = html_art.Art()
            for i, colour in enumerate(("red", "blue")):
                path = os.path.join(d, "%d.svg" % i)
                open(path, "w").write(self.SVG % colour)
                art.images[path] = "img%d" % i
            defs = art.image_defs()
        ids = re.findall(r'\sid="([^"]+)"', defs)
        self.assertEqual(len(ids), len(set(ids)), ids)
        for iid, colour in (("img0", "red"), ("img1", "blue")):
            part = defs[defs.index('id="%s"' % iid):]
            part = part[:part.index("</svg>")]
            self.assertIn('id="%s-g"><stop offset="0" stop-color="%s"' % (iid, colour), part)
            self.assertIn(".%s-cls-1{fill:url(#%s-g)}" % (iid, iid), part)
            self.assertIn('class="%s-cls-1"' % iid, part)
            self.assertIn("url('#%s-g')" % iid, part)
            self.assertEqual(part.count('href="#%s-c"' % iid), 2)
            self.assertNotRegex(part, r'(url\(\s*[\'"]?#|href="#)(g|c)\b')


if __name__ == "__main__":
    unittest.main()
