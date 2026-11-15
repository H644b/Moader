#!/usr/bin/env python3
"""Convert synthetic host-renderer captures for review; no image synthesis."""
from pathlib import Path
from PIL import Image, ImageDraw
import hashlib, json
root=Path(__file__).resolve().parent
paths=sorted((root/'review').glob('*.ppm'))
board=Image.new('RGB',(1920,((len(paths)+2)//3)*388),'#cbd5da')
for i,p in enumerate(paths):
    with Image.open(p) as im:
        im.save(p.with_suffix('.png'))
        im.thumbnail((640,360))
        x=(i%3)*640;y=(i//3)*388
        board.paste(im,(x,y+28))
        ImageDraw.Draw(board).text((x+8,y+8),p.stem+' / SYNTHETIC HOST',fill='#1e303a')
board.save(root/'review/matrix.png')
record={'kind':'synthetic host captures, not device execution',
        'renderer':'native/ui.cpp','fixtures':'native/ui_capture.cpp',
        'font':'native/assets/NotoSans-Regular.ttf',
        'font_sha256':hashlib.sha256((root/'assets/NotoSans-Regular.ttf').read_bytes()).hexdigest(),
        'images':[p.with_suffix('.png').name for p in paths],
        'pairing_code':'000042 is synthetic fixture data, not a console secret',
        'shipping_rasters':['branding/hbmenu.jpg'],
        'brand_source':'branding/mark.svg',
        'brand_mask':'branding/mark-alpha.hpp (same header used by Switch and host)',
        'brand_manifest':'branding/manifest.json',
        'bmp':'synthetic-screenshot.bmp uses native/ui_bitmap.hpp, the actual Minus encoder'}
(root/'review/provenance.json').write_text(json.dumps(record,indent=2)+'\n')
