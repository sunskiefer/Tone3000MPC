#!/usr/bin/env python3
"""Numbered overlay of every control on its page, plus docs/CONTROLS.md (the control map: id, type, x, y, w, h, label, key, view).
   python3 tools/overlay.py [preview_dir]   (preview_dir holds studio.py's page_0_mode0-N.png; default build/preview)"""
import os, sys
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from layout_parse import parse, VIEWS
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
prev = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build/preview")
ctl = parse(os.path.join(ROOT, "layout.conf"))
f = ImageFont.truetype(os.path.join(ROOT, "art/fonts/TitilliumWeb-Bold.ttf"), 22)
os.makedirs(os.path.join(ROOT, "docs/img"), exist_ok=True)
for v in range(len(VIEWS)):
    name = "page_0.png" if v == 0 else "page_0_mode0-%d.png" % v
    p = os.path.join(prev, name)
    im = Image.open(p).convert("RGB") if os.path.exists(p) else Image.new("RGB", (1280, 628), (14, 14, 14))
    d = ImageDraw.Draw(im, "RGBA")
    for c in ctl:
        if c["view"] not in (v, -1 if v else -3):
            continue
        x, y = c["x"], c["y"] - 86
        d.rectangle((x, y, x + c["w"], y + c["h"]), outline=(255, 80, 80, 255), width=2)
        t = str(c["id"])
        tw = d.textlength(t, font=f)
        d.rectangle((x, y, x + tw + 8, y + 26), fill=(255, 80, 80, 255))
        d.text((x + 4, y + 1), t, font=f, fill=(0, 0, 0, 255))
    im.save(os.path.join(ROOT, "docs/img/overlay_%d_%s.png" % (v, VIEWS[v].lower())))
rows = ["# Control map", "", "Single source of layout: `layout.conf` (shadow pixels, plugin area x 0..1280, y 86..714). `x y w h` below is the control's",
        "box; to move control N, edit its line in `layout.conf` (the `line` column) and nothing else changes. Numbered overlays: `docs/img/overlay_*.png`.", "",
        "| id | type | page | x | y | w | h | label | param key | layout.conf line |", "|---|---|---|---|---|---|---|---|---|---|"]
for c in ctl:
    pg = VIEWS[c["view"]] if c["view"] >= 0 else "header"
    rows.append("| %d | %s | %s | %d | %d | %d | %d | %s | %s | %d |" % (c["id"], c["type"], pg, c["x"], c["y"], c["w"], c["h"], c.get("label", "") or c.get("get", ""), c.get("key", ""), c["line"]))
open(os.path.join(ROOT, "docs/CONTROLS.md"), "w").write("\n".join(rows) + "\n")
print(len(ctl), "controls")
