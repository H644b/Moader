#!/usr/bin/env python3
"""Check direct installation from a public HTTPS catalog into an isolated host library."""
import argparse
from pathlib import Path
import platform
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("catalog", help="Public catalog.json URL")
parser.add_argument("mods", nargs="+", help="Release IDs to download; use a small release first for failure checks")
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="moader-store-test-") as tmp:
    work = Path(tmp).resolve()
    binary = work / "store-test"
    sources = ["tests/store_install.cpp", "native/store.cpp", "native/catalog_fetch.cpp",
               "source/model.cpp", "source/library.cpp", "source/zip.cpp", "source/web.cpp"]
    deps = ["-lcurl", "-lz", "-pthread"]
    if platform.system() != "Darwin":
        deps.append("-lcrypto")
    subprocess.run(["c++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-Wno-missing-field-initializers", "-Inative", "-Iinclude",
                    *sources, *deps, "-o", str(binary)], cwd=root, check=True)
    subprocess.run([str(binary), args.catalog, str(work / "sd"), *args.mods], check=True)
