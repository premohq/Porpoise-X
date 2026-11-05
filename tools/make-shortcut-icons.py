#!/usr/bin/env python3
"""Porpoise - the island's shortcuts: four white icons on clear, in a row
(assets/ui/shortcuts.png, 4 cells of 128 x 128), tinted by the app for the look.

    python3 tools/make-shortcut-icons.py

0 Memory Cards, 1 Settings, 2 Online, 3 Screenshots. Drawn here, line icons.
"""
import io
import math
import sys
from pathlib import Path

import cairosvg
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "ui" / "shortcuts.png"
CELL = 128
S = 'fill="none" stroke="#fff" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"'


def gear():
    teeth, r_out, r_in = 8, 46, 36
    pts = []
    for i in range(teeth * 4):
        a = (i / (teeth * 4)) * 2 * math.pi - math.pi / 2
        r = r_out if i % 4 in (1, 2) else r_in
        pts.append(f"{64 + r * math.cos(a):.1f},{64 + r * math.sin(a):.1f}")
    return f'<polygon points="{" ".join(pts)}" {S}/><circle cx="64" cy="64" r="14" {S}/>'


ICONS = [
    # a GameCube memory card: a card with its notched top and the label
    f'<path d="M36 20 h46 l12 12 v76 a6 6 0 0 1 -6 6 h-52 a6 6 0 0 1 -6 -6 v-82 a6 6 0 0 1 6 -6 z" {S}/>'
    f'<rect x="46" y="64" width="36" height="30" rx="4" {S}/><path d="M48 34 v12 M60 34 v12 M72 34 v12" {S}/>',
    gear(),
    # a globe
    f'<circle cx="64" cy="64" r="44" {S}/><ellipse cx="64" cy="64" rx="18" ry="44" {S}/>'
    f'<path d="M22 50 h84 M22 78 h84" {S}/>',
    # a picture: a frame, a sun, hills
    f'<rect x="18" y="28" width="92" height="72" rx="10" {S}/><circle cx="48" cy="52" r="8" {S}/>'
    f'<path d="M24 92 l30 -26 l18 16 l14 -12 l20 18" {S}/>',
]


def main() -> int:
    sheet = Image.new("LA", (CELL * len(ICONS), CELL), (255, 0))
    for i, body in enumerate(ICONS):
        svg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="{CELL}" height="{CELL}">{body}</svg>'
        a = Image.open(io.BytesIO(cairosvg.svg2png(bytestring=svg.encode()))).convert("RGBA").split()[3]
        sheet.paste(Image.merge("LA", (Image.new("L", a.size, 255), a)), (i * CELL, 0))
    sheet.save(OUT, optimize=True)
    print(f"{OUT.relative_to(ROOT)}: {sheet.width} x {sheet.height}, {OUT.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
