# Draws the program icon (Kassiopeia, our own drawing): python tools/make_icon.py <folder> writes app.ico and two
# preview pictures there; copy app.ico to res/. Needs Pillow.
"""Kassiopeia, withdrawn into her shell: the shell seen from above *is* the icon – its rim of marginal scutes is the
icon's edge – and on its plates glow glyphs nobody can read. No head, no legs, no background."""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

S = 1024
CX, CY = S / 2, S / 2
RX, RY = S * 0.43, S * 0.48  # the shell: a little longer than wide, filling the icon

SHELL_DARK = (58, 40, 20, 255)
MARGIN = (96, 68, 34, 255)
PLATE = (128, 92, 48, 255)
PLATE_LIGHT = (150, 110, 60, 255)
GOLD = (255, 222, 120, 255)


def on_shell(fx, fy):
    """A point given in shell units (-1..1) on the canvas."""
    return (CX + fx * RX, CY + fy * RY)


def plates():
    """The scutes as polygons in shell units: 5 vertebral down the middle, 4 costal on each side."""
    ys = [-0.78, -0.47, -0.16, 0.16, 0.47, 0.78]
    vert = []
    for i in range(5):
        y0, y1 = ys[i], ys[i + 1]
        w0 = 0.20 if i in (0, 4) else 0.24
        vert.append([(-w0, y0), (w0, y0), (w0 + 0.06, (y0 + y1) / 2), (w0, y1), (-w0, y1), (-w0 - 0.06, (y0 + y1) / 2)])
    costal = []
    cy = [-0.72, -0.30, 0.10, 0.48, 0.80]
    for side in (-1, 1):
        for i in range(4):
            y0, y1 = cy[i], cy[i + 1]
            inner0, inner1 = 0.25, 0.25
            outer = 0.78 - 0.18 * abs((y0 + y1) / 2)
            p = [(inner0, y0), (outer, y0 + 0.04), (outer + 0.02, y1 - 0.04), (inner1, y1)]
            costal.append([(side * x, y) for x, y in p])
    return vert, costal


def glyph(d, center, size, rnd, width):
    """A glyph nobody can read: a few strokes, arcs and dots from a seeded random source."""
    cx, cy = center
    pts = [(cx + rnd.uniform(-1, 1) * size, cy + rnd.uniform(-1, 1) * size) for _ in range(4)]
    d.line(pts[:rnd.choice([2, 3, 4])], fill=GOLD, width=width, joint='curve')
    if rnd.random() < 0.7:
        r = size * rnd.uniform(0.35, 0.6)
        a = rnd.uniform(0, 360)
        d.arc([cx - r, cy - r, cx + r, cy + r], a, a + rnd.uniform(120, 240), fill=GOLD, width=width)
    if rnd.random() < 0.6:
        x, y = cx + rnd.uniform(-1, 1) * size * 0.8, cy + rnd.uniform(-1, 1) * size * 0.8
        q = width * 0.9
        d.ellipse([x - q, y - q, x + q, y + q], fill=GOLD)
    if rnd.random() < 0.5:
        x = cx + rnd.uniform(-0.6, 0.6) * size
        d.line([(x, cy - size * 0.9), (x, cy + size * 0.9)], fill=GOLD, width=width)


def render(simple):
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # the rim of marginal scutes is the edge of the icon
    d.ellipse([CX - RX, CY - RY, CX + RX, CY + RY], fill=MARGIN, outline=SHELL_DARK, width=22)
    inner = 0.84
    d.ellipse([CX - RX * inner, CY - RY * inner, CX + RX * inner, CY + RY * inner], fill=PLATE, outline=SHELL_DARK,
              width=16)
    if not simple:
        for k in range(24):  # marginal scutes
            a = 2 * math.pi * k / 24
            x0, y0 = on_shell(inner * math.cos(a), inner * math.sin(a))
            x1, y1 = on_shell(0.985 * math.cos(a), 0.985 * math.sin(a))
            d.line([(x0, y0), (x1, y1)], fill=SHELL_DARK, width=12)
    vert, costal = plates()
    # the plates on their own layer, cut off at the rim
    pl = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    pd = ImageDraw.Draw(pl)
    for poly in (costal + vert) if not simple else vert:
        pts = [on_shell(x, y) for x, y in poly]
        pd.polygon(pts, fill=PLATE_LIGHT if poly in vert else PLATE)
        pd.line(pts + [pts[0]], fill=SHELL_DARK, width=14 if not simple else 26, joint='curve')
    inner_mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(inner_mask).ellipse([CX - RX * inner + 8, CY - RY * inner + 8, CX + RX * inner - 8,
                                        CY + RY * inner - 8], fill=255)
    pl.putalpha(Image.composite(pl.split()[3], Image.new('L', (S, S), 0), inner_mask))
    img.alpha_composite(pl)
    # the dome: light from above-left, darker towards the rim
    shade = Image.new('L', (S, S), 0)
    sd = ImageDraw.Draw(shade)
    for k in range(60):
        t = k / 59
        sd.ellipse([CX - RX * (1 - t) - 60 * t, CY - RY * (1 - t) - 80 * t, CX + RX * (1 - t) - 60 * t,
                    CY + RY * (1 - t) - 80 * t], fill=int(120 * t))
    shade = shade.filter(ImageFilter.GaussianBlur(30))
    shell_mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(shell_mask).ellipse([CX - RX, CY - RY, CX + RX, CY + RY], fill=255)
    light = Image.new('RGBA', (S, S), (255, 236, 200, 0))
    light.putalpha(Image.composite(shade.point(lambda v: int(v * 0.55)), Image.new('L', (S, S), 0), shell_mask))
    img.alpha_composite(light)
    edge = Image.new('L', (S, S), 255)
    ImageDraw.Draw(edge).ellipse([CX - RX * 0.8, CY - RY * 0.8, CX + RX * 0.8, CY + RY * 0.8], fill=0)
    edge = edge.filter(ImageFilter.GaussianBlur(60))
    dark = Image.new('RGBA', (S, S), (20, 12, 4, 0))
    dark.putalpha(Image.composite(edge.point(lambda v: int(v * 0.45)), Image.new('L', (S, S), 0), shell_mask))
    img.alpha_composite(dark)

    # glyphs, glowing
    layer = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(layer)
    rnd = random.Random(1973)  # the year Momo appeared
    if simple:
        for i, poly in enumerate(vert[1:4]):
            cx = sum(x for x, _ in poly) / len(poly)
            cy = sum(y for _, y in poly) / len(poly)
            glyph(gd, on_shell(cx, cy), 70, rnd, 34)
    else:
        for poly in vert + costal:
            cx = sum(x for x, _ in poly) / len(poly)
            cy = sum(y for _, y in poly) / len(poly)
            size = 52 if poly in vert else 40
            glyph(gd, on_shell(cx, cy), size, rnd, 13)
        for k in range(24):  # a faint ember on every marginal scute
            a = 2 * math.pi * (k + 0.5) / 24
            x, y = on_shell(0.915 * math.cos(a), 0.915 * math.sin(a))
            gd.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(255, 214, 110, 170))
    glow = layer.filter(ImageFilter.GaussianBlur(20 if not simple else 30))
    r, g, b, a = glow.split()
    glow = Image.merge('RGBA', (r, g, b, a.point(lambda v: min(255, int(v * 2.6)))))
    # glow stays on the shell
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).ellipse([CX - RX, CY - RY, CX + RX, CY + RY], fill=255)
    glow.putalpha(Image.composite(glow.split()[3], Image.new('L', (S, S), 0), mask))
    img.alpha_composite(glow)
    img.alpha_composite(layer)
    return img


if __name__ == '__main__':
    out = sys.argv[1]
    big, small = render(False), render(True)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    imgs = [(small if s <= 32 else big).resize((s, s), Image.LANCZOS) for s in sizes]
    prev = Image.new('RGBA', (512, 512), (40, 44, 56, 255))
    prev.alpha_composite(big.resize((512, 512), Image.LANCZOS))
    prev.save(os.path.join(out, 'preview_512.png'))
    strip = Image.new('RGBA', (sum(s * 4 + 10 for s in sizes[:4]) + 10, 210), (230, 230, 230, 255))
    x = 10
    for s, im in zip(sizes[:4], imgs[:4]):
        strip.alpha_composite(im.resize((s * 4, s * 4), Image.NEAREST), (x, 10))
        x += s * 4 + 10
    strip.save(os.path.join(out, 'preview_small.png'))
    imgs[-1].save(os.path.join(out, 'app.ico'), sizes=[(s, s) for s in sizes], append_images=imgs[:-1])
    print('ok')
