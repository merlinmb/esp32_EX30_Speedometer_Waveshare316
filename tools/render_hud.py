#!/usr/bin/env python3
"""Preview of the HUD warp, using the same maths as src/ui_waveshare.cpp.

Draws the upright HUD (as the driver should see it), warps it into what the panel shows,
and simulates the windscreen reflection. Writes docs/images/hud-*.png.

Usage: python3 tools/render_hud.py
"""
import math
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H, CELL = 820, 320, 8
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, "tools", "fonts")


def inverse_map(h, flip_x, flip_y, x, y):
    cx, cy, w2, h2 = (W - 1) / 2, (H - 1) / 2, W / 2, H / 2
    if flip_x: x = (W - 1) - x
    if flip_y: y = (H - 1) - y
    dx, dy = x - cx - h["offsetX"], y - cy - h["offsetY"]
    a = math.radians(h["rotateDeg"])
    px = dx * math.cos(a) + dy * math.sin(a)
    py = -dx * math.sin(a) + dy * math.cos(a)
    px += h["skew"] * py
    X, Y = px / w2, py / h2
    Y -= h["bow"] * X * X
    d = 1 - h["keystone"] * Y
    if d < 0.1: return -10000, -10000
    X /= d
    d = 1 + h["taper"] * X
    if d < 0.1: return -10000, -10000
    Y /= d
    return cx + X / h["scaleX"] * w2, cy + Y / h["scaleY"] * h2


def warp(src, h, rotation=0):
    """Panel image for the upright HUD image src, cell by cell as on the device."""
    turned = rotation == 2
    fx, fy = h["mirrorH"] != turned, h["mirrorV"] != turned
    nx, ny = (W + CELL - 1) // CELL + 1, (H + CELL - 1) // CELL + 1
    nodes = np.zeros((ny, nx, 2))
    for j in range(ny):
        for i in range(nx):
            nodes[j, i] = inverse_map(h, fx, fy, i * CELL, j * CELL)
    # bilinear interpolation of the node grid, then nearest-pixel lookup
    ys, xs = np.mgrid[0:H, 0:W]
    i, j = xs // CELL, ys // CELL
    fxr, fyr = (xs % CELL) / CELL, (ys % CELL) / CELL
    a, b = nodes[j, i], nodes[j, i + 1]
    c, d = nodes[j + 1, i], nodes[j + 1, i + 1]
    top = a + (b - a) * fxr[..., None]
    bot = c + (d - c) * fxr[..., None]
    uv = np.floor(top + (bot - top) * fyr[..., None] + 0.5).astype(int)
    u, v = uv[..., 0], uv[..., 1]
    ok = (u >= 0) & (u < W) & (v >= 0) & (v < H)
    out = np.zeros((H, W, 3), np.uint8)
    out[ok] = src[v[ok], u[ok]]
    return out


def hud_image(speed="68", grid=False, colour=(255, 255, 255)):
    img = Image.new("RGB", (W, H), "black")
    g = ImageDraw.Draw(img)
    dim = tuple(int(c * 0.28) for c in colour)
    title = ImageFont.truetype(os.path.join(FONTS, "Barlow-SemiBold.ttf"), 30)
    if grid:
        for x in range(0, W, 41): g.line([(x, 0), (x, H)], fill=dim)
        for y in range(0, H, 40): g.line([(0, y), (W, y)], fill=dim)
        g.rectangle([0, 0, W - 1, H - 1], outline=colour, width=2)
        g.line([(0, H // 2), (W, H // 2)], fill=colour)
        g.line([(W // 2, 0), (W // 2, H)], fill=colour)
        g.ellipse([W // 2 - 121, H // 2 - 121, W // 2 + 121, H // 2 + 121], outline=colour, width=2)
        g.text((W // 2, 20), "TOP", font=title, fill=colour, anchor="mm")
        g.text((12, H // 4), "LEFT", font=title, fill=colour, anchor="lm")
        g.text((W - 12, H // 4), "RIGHT", font=title, fill=colour, anchor="rm")
        return np.array(img)
    big = ImageFont.truetype(os.path.join(FONTS, "BarlowCondensed-SemiBold.ttf"), 300)
    g.text((W // 2, 258), speed, font=big, fill=colour, anchor="ms")
    half = g.textlength(speed, font=big) / 2
    g.text((W // 2 + half + 14, 258), "MPH", font=title, fill=colour, anchor="ls")
    gx, gw, gy, gh = 60, W - 120, 278, 22
    g.rectangle([gx - 3, gy - 3, gx + gw + 2, gy + gh + 2], outline=dim, width=2)
    g.rectangle([gx, gy, gx + int(gw * 68 / 120) - 1, gy + gh - 1], fill=colour)
    lx = gx + round(gw * 70 / 120)
    g.rectangle([lx - 3, gy - 12, lx + 2, gy + gh + 11], fill=(255, 42, 26))
    return np.array(img)


NONE = dict(mirrorV=False, mirrorH=False, scaleX=1, scaleY=1, keystone=0, taper=0, bow=0,
            skew=0, rotateDeg=0, offsetX=0, offsetY=0)


def main():
    out = os.path.join(ROOT, "docs", "images")
    os.makedirs(out, exist_ok=True)
    example = dict(NONE, mirrorV=True, scaleX=0.88, scaleY=0.85, keystone=0.12, bow=0.10, skew=0.05, rotateDeg=-1.5)
    for name, grid in (("hud-speed", False), ("hud-grid", True)):
        src = hud_image(grid=grid)
        Image.fromarray(src).save(os.path.join(out, f"{name}-driver-view.png"))
        Image.fromarray(warp(src, dict(NONE, mirrorV=True))).save(os.path.join(out, f"{name}-panel-flat.png"))
        Image.fromarray(warp(src, example)).save(os.path.join(out, f"{name}-panel-corrected.png"))
    print("written to", out)


if __name__ == "__main__":
    main()
