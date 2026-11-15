#!/usr/bin/env python3
"""Embed the pinned OFL font so the NRO has no SD font dependency."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
font = (root / 'native/assets/NotoSans-Regular.ttf').read_bytes()
out = root / 'build/generated/native_font.hpp'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('// Generated from native/assets/NotoSans-Regular.ttf (SIL OFL 1.1).\n'
               '#pragma once\ninline constexpr unsigned char moader_font[] = {\n' +
               '\n'.join(','.join(str(b) for b in font[n:n+32])+',' for n in range(0,len(font),32)) + '\n};\n')
