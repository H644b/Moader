#!/usr/bin/env python3
"""Embed only public browser assets into the resident binary."""

import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "build/generated/web_assets.hpp"
OUT.parent.mkdir(parents=True, exist_ok=True)
assets = [
    ("index.html", "text/html; charset=utf-8", "/"),
    ("style.css", "text/css; charset=utf-8", "/style.css"),
    ("../branding/lockup.svg", "image/svg+xml", "/lockup.svg"),
    ("../native/assets/Text.ttf", "font/ttf", "/font/text.ttf"),
    ("../native/assets/Strong.ttf", "font/ttf", "/font/strong.ttf"),
    ("app.js", "application/javascript; charset=utf-8", "/app.js"),
    ("icon.svg", "image/svg+xml", "/icon.svg"),
]
lines = [
    "#pragma once",
    '#include "moader/web.hpp"',
    "inline std::vector<moader::WebAsset> moader_web_assets() {",
]
for index, (filename, mime, route) in enumerate(assets):
    data = (ROOT / "web" / filename).read_bytes()
    assert len(data) < 1 << 20
    lines.append(
        "static const unsigned char asset_"
        + str(index)
        + "[] = {"
        + ",".join(map(str, data))
        + "};"
    )
lines.append("return {")
for index, (filename, mime, route) in enumerate(assets):
    lines.append(
        '{"'
        + route
        + '","'
        + mime
        + '",std::string(reinterpret_cast<const char*>(asset_'
        + str(index)
        + "),sizeof(asset_"
        + str(index)
        + "))},"
    )
lines += ["};", "}"]
OUT.write_text("\n".join(lines) + "\n")
