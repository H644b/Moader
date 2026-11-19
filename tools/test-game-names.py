#!/usr/bin/env python3
"""Exercise the production title and icon readers against the SDK's control-data layout."""
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parent.parent
build = root / "build/host"
build.mkdir(parents=True, exist_ok=True)
flags = [
    "clang++", "-std=c++20", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
    "-Wno-invalid-constexpr", "-Wno-missing-field-initializers", "-fsanitize=address,undefined",
    "-I" + str(root), "-I" + str(root / "include"), "-I" + str(root / "tests/host_sdk"),
    "-idirafter", str(root / "vendor/devkitpro/libounce/include"),
]
names = build / "game-names-test"
subprocess.run([*flags, str(root / "tests/game_names.cpp"), "-lz", "-o", str(names)], check=True)
subprocess.run([str(names)], check=True)

icons = build / "game-icons-test"
subprocess.run([*flags, "-Wno-unused-function", str(root / "tests/game_icons.cpp"),
                str(root / "native/artwork.cpp"), "-pthread", "-lz", "-o", str(icons)], check=True)
subprocess.run([str(icons), str(root / "branding/hbmenu.jpg"), str(root / "branding/mark-alpha.png")],
               check=True, timeout=20)
