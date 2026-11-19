#!/usr/bin/env python3
"""Convert synthetic native framebuffer captures into review PNGs."""

import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw

root = Path(__file__).resolve().parent
directory = root / "review"
paths = sorted(directory.glob("*.ppm"))
if not paths:
    raise SystemExit("Run the host renderer to create PPM captures first.")

board = Image.new("RGB", (1920, ((len(paths) + 2) // 3) * 388), "#121918")
draw = ImageDraw.Draw(board)
for index, path in enumerate(paths):
    with Image.open(path) as capture:
        capture.save(path.with_suffix(".png"))
        capture.thumbnail((640, 360), Image.Resampling.LANCZOS)
        x, y = (index % 3) * 640, (index // 3) * 388
        board.paste(capture, (x, y + 28))
        draw.text((x + 8, y + 8), path.stem, fill="#f3f6ed")
board.save(directory / "matrix.png")

fonts = sorted((root / "assets").glob("*.ttf"))
record = {
    "kind": "Synthetic library rendered on the host",
    "renderer": "native/ui.cpp",
    "fixtures": "native/ui_capture.cpp",
    "fonts": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in fonts},
    "images": [p.with_suffix(".png").name for p in paths],
    "brand_source": "branding/lockup.svg",
    "brand_mask": "branding/mark-alpha.hpp",
    "brand_manifest": "branding/checks.json",
}
(directory / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
