#!/usr/bin/env python3
"""Embed the pinned OFL typefaces used by both the console and host renderer."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / "build/generated/native_font.hpp"
out.parent.mkdir(parents=True, exist_ok=True)
with out.open("w") as header:
    header.write("// Generated from native/assets, SIL OFL 1.1.\n#pragma once\n")
    for name, file in [
        ("text", "Text"),
        ("strong", "Strong"),
        ("display", "Display"),
        ("fallback", "NotoSans-Regular"),
    ]:
        data = (root / ("native/assets/" + file + ".ttf")).read_bytes()
        header.write("inline constexpr unsigned char font_" + name + "[] = {\n")
        for start in range(0, len(data), 40):
            header.write(",".join(map(str, data[start : start + 40])) + ",\n")
        header.write("};\n")
