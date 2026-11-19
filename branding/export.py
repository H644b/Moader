#!/usr/bin/env python3
"""Export the approved cartridge identity and interface glyphs for the framebuffer."""

import hashlib
import io
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parent
NS = "{http://www.w3.org/2000/svg}"


def raster(svg, w, h):
    data = subprocess.run(
        ["rsvg-convert", "-w", str(w * 3), "-h", str(h * 3)],
        input=svg.encode(),
        stdout=subprocess.PIPE,
        check=True,
    ).stdout
    return Image.open(io.BytesIO(data)).convert("RGBA").resize((w, h), Image.Resampling.LANCZOS)


def alpha(name, svg, w, h):
    mask = raster(svg, w, h).getchannel("A")
    if name == "mark":
        mask.save(ROOT / "mark-alpha.png")
    data = mask.tobytes()
    lines = [",".join(map(str, data[i : i + 40])) for i in range(0, len(data), 40)]
    return (
        "inline constexpr unsigned char "
        + name
        + "Data[] = {\n"
        + ",\n".join(lines)
        + "\n};\ninline constexpr Mask "
        + name
        + "{"
        + str(w)
        + ","
        + str(h)
        + ","
        + name
        + "Data};\n"
    )


def glyph(path):
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" '
        'stroke="white" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">'
        + path
        + "</svg>"
    )


def main():
    icon = (ROOT / "hbmenu-source.svg").read_text()
    (ROOT / "favicon.svg").write_text(icon)
    (ROOT.parent / "web/icon.svg").write_text(icon)
    img = raster(icon, 256, 256).convert("RGB")
    img.save(ROOT / "hbmenu.jpg", quality=96, subsampling=0, progressive=False)
    art = ET.fromstring((ROOT / "lockup.svg").read_text())

    def layer(ink):
        layer_tree = ET.fromstring(ET.tostring(art, encoding="unicode"))
        for group in layer_tree.iter():
            for child in list(group):
                if child.tag == NS + "path" and child.get("fill") != ink:
                    group.remove(child)
        layer_tree.set("viewBox", "0 0 289.088 57")
        return ET.tostring(layer_tree, encoding="unicode")

    symbols = {
        "layers": '<path d="m12 2 10 5.5L12 13 2 7.5Z M2 12l10 5.5L22 12 M2 16.5 12 22l10-5.5"/>',
        "collection": '<rect x="3" y="7" width="13" height="13" rx="2"/><path d="M7 4h11a2 2 0 0 1 2 2v11"/>',
        "gear": '<path d="m10 2-.7 2.6-2 .9-2.4-1.1-2 3.4 2 1.8-.2 2.4-2 1.6 1.8 3.6 2.6-.6L9 18l.5 2.9 4 .1.8-2.7 2.2-.9 2.4 1 2-3.4-1.9-1.8.1-2.4 2.1-1.6-1.8-3.5-2.7.5-2-1.5-.5-2.7Z"/><circle cx="12" cy="11.6" r="3.6"/>',
        "general": '<path d="M2 5h10m5 0h5M2 12h4m5 0h11M2 19h10m5 0h5"/><circle cx="14.5" cy="5" r="2.3"/><circle cx="8.5" cy="12" r="2.3"/><circle cx="14.5" cy="19" r="2.3"/>',
        "wifi": '<path d="M2 8q10-9 20 0M5.5 12q6.5-6 13 0M9 16q3-3 6 0"/><circle cx="12" cy="20" r="1" fill="white"/>',
        "controls": '<path d="M5 6h14q2 0 2.5 3l1 6q.5 5-3 3l-4-3H9l-4 3q-4 2-3.5-3l1-6Q3 6 5 6ZM5 10.5h5m-2.5-2.5v5"/><circle cx="16.6" cy="12" r="1"/><circle cx="19" cy="9.5" r="1"/>',
        "info": '<circle cx="12" cy="12" r="9.4"/><path d="M12 11v6M12 7v.5"/>',
        "globe": '<circle cx="12" cy="12" r="9.4"/><ellipse cx="12" cy="12" rx="4.5" ry="9.4"/><path d="M3 9h18M3 15h18"/>',
        "code": '<path d="m7 6-5 6 5 6m10-12 5 6-5 6m-3-16-4 20"/>',
    }
    result = [
        "// Generated from the brand vectors by branding/export.py.\n#pragma once\nnamespace moader::brand {\nstruct Mask {int w,h; const unsigned char* a;};\n"
    ]
    result.append(alpha("mark", (ROOT / "mark.svg").read_text(), 96, 96))
    result.append(alpha("logoMint", layer("#9CF4CE"), 348, 69))
    result.append(alpha("logoPaper", layer("#F3F6ED"), 348, 69))
    for name, path in symbols.items():
        result.append(alpha(name, glyph(path), 48, 48))
    result.append(alpha("bag", (ROOT / "store.svg").read_text(), 56, 56))
    (ROOT / "mark-alpha.hpp").write_text("".join(result) + "}\n")
    checks = {
        p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest()
        for p in [
            "mark.svg",
            "lockup.svg",
            "store.svg",
            "hbmenu-source.svg",
            "hbmenu.jpg",
            "mark-alpha.hpp",
            "favicon.svg",
        ]
    }
    (ROOT / "checks.json").write_text(json.dumps(checks, indent=2) + "\n")


if __name__ == "__main__":
    main()
