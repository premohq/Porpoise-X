#!/usr/bin/env python3
"""Porpoise - the island's controller icons, from Ruben's vectors.

    python3 tools/make-controller-icons.py   writes assets/ui/controllers.png

tools/controller-icons/*.svg are drawn in one colour; each is made white (the
app tints it), fitted into a 64 x 64 cell with a little room round it, and
the cells are laid side by side in this order (ui_app_glass.cpp's kCtl*):

    0 DualSense  1 DualShock 4  2 PlayStation Move  3 Wii Remote
    4 Classic Controller  5 GameCube controller  6 heart  7 heart, outlined
    8 PS3 Eye  9 PlayStation Move Navigation controller (from Ruben's line diagram)
"""
import io
import re
import sys
from pathlib import Path

import cairosvg
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "tools" / "controller-icons"
OUT = ROOT / "assets" / "ui" / "controllers.png"
CELL, ROOM = 64, 4

HEART = "M50 88 C20 66 6 50 6 32 C6 18 17 8 30 8 C39 8 46 13 50 20 C54 13 61 8 70 8 C83 8 94 18 94 32 C94 50 80 66 50 88 Z"


def white(svg: str) -> str:
    svg = re.sub(r'fill="#[0-9A-Fa-f]{3,6}"', 'fill="#FFFFFF"', svg)
    return svg


def render(svg: str) -> Image.Image:
    png = cairosvg.svg2png(bytestring=svg.encode("utf-8"), output_width=1024)
    im = Image.open(io.BytesIO(png)).convert("RGBA")
    box = im.getbbox()
    if box:
        im = im.crop(box)
    side = CELL - ROOM * 2
    k = min(side / im.width, side / im.height)
    im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    cell = Image.new("RGBA", (CELL, CELL), (255, 255, 255, 0))
    cell.alpha_composite(im, ((CELL - im.width) // 2, (CELL - im.height) // 2))
    return cell


def main() -> int:
    names = ["dualsense", "dualshock4", "ps-move", "wiimote", "wii-classic", "gamecube"]
    cells = [render(white((SRC / f"{n}.svg").read_text(encoding="utf-8"))) for n in names]
    heart = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 96"><path fill="#FFFFFF" d="{HEART}"/></svg>'
    outline = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-4 -4 108 104"><path fill="none" stroke="#FFFFFF" '
               f'stroke-width="9" stroke-linejoin="round" d="{HEART}"/></svg>')
    cells += [render(heart), render(outline)]
    cells.append(render(white((SRC / "ps3-eye.svg").read_text(encoding="utf-8"))))
    cells.append(render(white((SRC / "ps-move-nav.svg").read_text(encoding="utf-8"))))
    sheet = Image.new("RGBA", (CELL * len(cells), CELL), (255, 255, 255, 0))
    for i, c in enumerate(cells):
        sheet.alpha_composite(c, (i * CELL, 0))
    # White where there is any cover: the app tints it, edges stay soft.
    px = sheet.load()
    for y in range(sheet.height):
        for x in range(sheet.width):
            a = px[x, y][3]
            px[x, y] = (255, 255, 255, a)
    sheet.save(OUT, optimize=True)
    try:  # smaller, the same pixels
        import oxipng
        oxipng.optimize(str(OUT), level=6, strip=oxipng.StripChunks.all())
    except ImportError:
        pass
    print(f"{OUT.relative_to(ROOT)}: {sheet.width} x {sheet.height}, {OUT.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
