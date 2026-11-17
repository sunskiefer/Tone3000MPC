#!/usr/bin/env python3
"""Facebook image set from the real skin previews -> out dir. _1 hero, _2 best bits, perspective_vertical, perspective_horizontal."""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(R, "build/fb")
os.makedirs(out, exist_ok=True)
F = lambda n, s: ImageFont.truetype(os.path.join(R, "art/fonts", n), s)
bold, sten = "TitilliumWeb-Bold.ttf", "SairaStencilOne-Regular.ttf"
if not os.path.exists(os.path.join(R, "art/fonts", sten)):
    sten = bold
P = lambda v: Image.open(os.path.join(R, "build/preview", "page_0.png" if v == 0 else "page_0_mode0-%d.png" % v)).convert("RGB")
def bg(w, h):
    y = np.linspace(0, 1, h)[:, None, None]
    a = np.array([22, 22, 26]) * (1 - y) + np.array([8, 8, 10]) * y
    return Image.fromarray(np.repeat(a, w, 1).astype("uint8"))
def title(d, x, y, t, sz, fill=(255, 255, 255)):
    d.text((x, y), t, font=F(sten, sz), fill=fill)
def shot(im, w):
    im = im.resize((w, int(im.height * w / im.width)), Image.LANCZOS)
    m = Image.new("RGB", (im.width + 8, im.height + 8), (70, 70, 76)); m.paste(im, (4, 4)); return m
def shadowed(base, im, xy):
    sh = Image.new("RGBA", base.size, (0, 0, 0, 0)); ImageDraw.Draw(sh).rectangle((xy[0] + 14, xy[1] + 18, xy[0] + im.width + 14, xy[1] + im.height + 18), fill=(0, 0, 0, 200))
    base.paste(Image.alpha_composite(base.convert("RGBA"), sh.filter(ImageFilter.GaussianBlur(14))).convert("RGB")); base.paste(im, xy)
# 1 hero 1200x630
b = bg(1200, 630); d = ImageDraw.Draw(b)
title(d, 50, 40, "Tone3000MPC", 92); d.text((54, 150), "NAM amps, pedals & IR cabs on the Akai Force / MPC OS", font=F(bold, 30), fill=(26, 180, 99))
d.text((54, 200), "Browse the TONE3000 library on the Force. Download only what you play.", font=F(bold, 26), fill=(220, 220, 220))
shadowed(b, shot(P(1), 700), (460, 300)); shadowed(b, shot(P(0), 520), (40, 330)); d.text((54, 245), "free  ·  L'Cronx  ·  ARMv7 VST2 effect", font=F(bold, 26), fill=(180, 180, 180))
b.save(os.path.join(out, "Tone3000MPC_1.png"))
# 2 best bits 1200x900
b = bg(1200, 900); d = ImageDraw.Draw(b); title(d, 40, 24, "The best bits", 56)
bits = [(1, "Chain of NAM + IR blocks, tap to MOVE"), (2, "Browse TONE3000 on the Force"), (3, "Calibrated in/out + gate"), (4, "Pre/post/global EQ")]
for i, (v, t) in enumerate(bits):
    x, y = 40 + (i % 2) * 580, 120 + (i // 2) * 390
    b.paste(shot(P(v), 540), (x, y)); d.text((x, y + 300), t, font=F(bold, 30), fill=(255, 255, 255))
d.text((40, 860), "unlimited presets + 48 factory presets that fill themselves from TONE3000", font=F(bold, 26), fill=(26, 180, 99))
b.save(os.path.join(out, "Tone3000MPC_2.png"))
# perspective
def persp(im, w, ang):
    im = shot(im, w).convert("RGBA"); W, H = im.size; k = ang
    coeffs = None
    pts_dst = [(0, 0), (W, int(H * k)), (W, int(H * (1 - k))), (0, H)]
    src = [(0, 0), (W, 0), (W, H), (0, H)]
    A = []; B = []
    for (x, y), (u, v) in zip(pts_dst, src):
        A += [[x, y, 1, 0, 0, 0, -u * x, -u * y], [0, 0, 0, x, y, 1, -v * x, -v * y]]; B += [u, v]
    c = np.linalg.solve(np.array(A, float), np.array(B, float))
    return im.transform((W, H), Image.PERSPECTIVE, c.tolist(), Image.BICUBIC)
for name, size, layout in (("perspective_vertical", (1080, 1350), "v"), ("perspective_horizontal", (1200, 630), "h")):
    b = bg(*size).convert("RGBA"); d = ImageDraw.Draw(b)
    if layout == "v":
        title(d, 50, 40, "Tone3000MPC", 100); d.text((54, 170), "NAM amps, pedals & cabs for the Force", font=F(bold, 36), fill=(26, 180, 99))
        for i, v in enumerate((1, 2, 3)):
            p = persp(P(v), 900, 0.07); b.alpha_composite(p, (90 + (i - 1) * 20, 260 + i * 350))
    else:
        title(d, 40, 30, "Tone3000MPC", 70); d.text((44, 115), "free · L'Cronx · Force / MPC OS", font=F(bold, 26), fill=(26, 180, 99))
        for i, v in enumerate((1, 2)):
            p = persp(P(v), 640, 0.06); b.alpha_composite(p, (30 + i * 560, 190 + i * 70))
    b.convert("RGB").save(os.path.join(out, name + ".png"))
print(os.listdir(out))
