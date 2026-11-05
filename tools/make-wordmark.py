#!/usr/bin/env python3
"""Porpoise - the wordmark at the top left, from Ruben's logo.

    python3 tools/make-wordmark.py

tools/brand/porp-logo-wordmark-color.svg is the dolphin in cyan and the name in
white. This writes two masks, white on clear, that the app tints for each look:

    assets/brand/wordmark.png       the whole wordmark (white on OLED, black on Light)
    assets/brand/wordmark-mark.png  the dolphin alone (cyan over the white, on dark backdrops)
    assets/brand/wordmark-glow.png  the whole wordmark blurred: its glow in the boot (ui_app_boot.cpp)
    assets/brand/boot/wordmark.png  the whole wordmark, larger: the boot draws it big
    assets/brand/boot/trace-01..12.png  its outline being drawn in, twelve steps to 0.6
        of the way (where every part's outline has closed)
        (each part's own line, as the boot concepts page draws it: a dash
        running round it, 7 units wide)
"""
import io
import re
import sys
from pathlib import Path

import cairosvg
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "tools" / "brand" / "porp-logo-wordmark-color.svg"
OUT = ROOT / "assets" / "brand"
HEIGHT = 176  # twice the largest it's drawn


def mask(svg: str, path: Path, blur: float = 0, height: int = HEIGHT) -> None:
    png = cairosvg.svg2png(bytestring=svg.encode("utf-8"), output_height=height)
    a = Image.open(io.BytesIO(png)).convert("RGBA").split()[3]
    if blur:
        a = a.filter(ImageFilter.GaussianBlur(blur)).point(lambda p: min(255, int(p * 1.6)))
    a = a.point(lambda p: (p + 8) // 17 * 17)
    Image.merge("LA", (Image.new("L", a.size, 255), a)).save(path, optimize=True)
    try:
        import oxipng
        oxipng.optimize(str(path), level=6, strip=oxipng.StripChunks.all())
    except ImportError:
        pass
    print(f"{path.relative_to(ROOT)}: {a.width} x {a.height}, {path.stat().st_size} bytes")


def main() -> int:
    svg = SRC.read_text(encoding="utf-8")
    svg = re.sub(r"<style>.*?</style>", "", svg, flags=re.S)
    whole = svg.replace('class="st1"', 'fill="#fff"').replace('class="st0"', 'fill="#fff"')
    mark = svg.replace('class="st1"', 'fill="#fff"').replace('class="st0"', 'fill="none"')
    mask(whole, OUT / "wordmark.png")
    mask(mark, OUT / "wordmark-mark.png")
    mask(whole, OUT / "wordmark-glow.png", blur=9)
    boot = OUT / "boot"
    boot.mkdir(exist_ok=True)
    mask(whole, boot / "wordmark.png", height=BOOT_HEIGHT)
    for step in range(1, TRACE_STEPS + 1):
        mask(trace(svg, TRACE_FULL * step / TRACE_STEPS), boot / f"trace-{step:02d}.png", height=BOOT_HEIGHT)
    return 0


BOOT_HEIGHT = 384  # the boot draws it 307 tall: a little over, for 4K
TRACE_STEPS = 12
TRACE_FULL = 0.6  # every outline is closed by then (the dashes are longer than the paths)


def trace(svg: str, progress: float) -> str:
    """Each part's outline drawn in to progress (0..1): the dash the concepts
    page uses (4200 units for the dolphin, 1500 for a letter)."""
    def stroke(m: re.Match, length: float) -> str:
        return (f'{m.group(1)}fill="none" stroke="#fff" stroke-width="7" stroke-linejoin="round" '
                f'stroke-dasharray="{length} {length}" stroke-dashoffset="{length * (1 - progress):.2f}"')
    svg = re.sub(r'(<path )class="st1"', lambda m: stroke(m, 4200), svg)
    svg = re.sub(r'(<(?:path|rect) )class="st0"', lambda m: stroke(m, 1500), svg)
    return svg


if __name__ == "__main__":
    sys.exit(main())
