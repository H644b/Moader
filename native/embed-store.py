#!/usr/bin/env python3
"""Pack the locally reviewed preview snapshot. Builds have no network dependency."""

from pathlib import Path
import json

root = Path(__file__).resolve().parent
store = root / "store"
out = root.parent / "build/generated/store_assets.hpp"
out.parent.mkdir(parents=True, exist_ok=True)
with out.open("w") as dest:
    dest.write("#pragma once\nnamespace store_assets {\n")
    dest.write(
        'inline constexpr char catalog[] = R"MOADER('
        + (store / "catalog.json").read_text()
        + ')MOADER";\n'
    )
    entries = []
    for i, (url, file) in enumerate(json.loads((store / "cache.json").read_text()).items()):
        data = (store / file).read_bytes()
        dest.write("inline constexpr unsigned char p%d[] = {\n" % i)
        for j in range(0, len(data), 48):
            dest.write(",".join(map(str, data[j : j + 48])) + ",\n")
        dest.write("};\n")
        entries.append("{%s,p%d,sizeof(p%d)}" % (json.dumps(url), i, i))
    dest.write("struct Preview {const char* url;const unsigned char* bytes;unsigned length;};\n")
    dest.write("inline constexpr Preview images[] = {\n" + ",\n".join(entries) + "};\n}\n")
