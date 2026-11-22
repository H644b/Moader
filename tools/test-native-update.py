#!/usr/bin/env python3
"""Exercise the native updater in a temporary SD tree. --live also downloads the latest official release."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import urllib.request

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--live", action="store_true")
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="moader-update-") as tmp:
    for live in ([False, True] if args.live else [False]):
        binary = Path(tmp).resolve() / ("live" if live else "fixtures")
        sources = ["native/ui_tests/update_test.cpp", "native/update.cpp",
                   "source/model.cpp", "source/library.cpp", "source/zip.cpp"]
        flags = ["-std=c++20", "-O1", "-pthread", "-Iinclude", "-Inative"]
        libs = ["-lz"]
        if sys.platform == "darwin":
            flags.append("-Wno-deprecated-declarations")
        else:
            libs.append("-lcrypto")
        if live:
            sources.append("native/catalog_fetch.cpp")
            flags.append("-DMOADER_LIVE_UPDATE")
            libs.append("-lcurl")
        subprocess.run([os.environ.get("CXX", "c++"), *flags, *sources, *libs, "-o", str(binary)],
                       cwd=root, check=True)
        command = [str(binary), str(binary) + "-sd"]
        if live:
            api = binary.parent / "github-release.json"
            request = urllib.request.Request("https://api.github.com/repos/H644b/Moader/releases/latest",
                                             headers={"User-Agent": "Moader-updater-test"})
            with urllib.request.urlopen(request, timeout=20) as response:
                api.write_bytes(response.read(256 * 1024 + 1))
            command.append(str(api))
        subprocess.run(command, check=True, timeout=120)
