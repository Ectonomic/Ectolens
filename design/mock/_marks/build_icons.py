#!/usr/bin/env python3
"""Builds Ectolens's icon from glyphs.corners (mark round one): the Ecto splat with
viewfinder corners in the family's dark green. Each size KDE draws gets its own SVG at that size's line weight
(ECM's <size>-apps-ectolens.svg), plus the scalable master; PNGs for the mock and checks.

    python3 build_icons.py    # writes icons/*-apps-ectolens.svg and design/mark/
"""
import pathlib, subprocess
from PIL import Image
import glyphs as G

HERE = pathlib.Path(__file__).parent
REPO = HERE.parents[2]
ICONS = REPO / 'icons'
MARK = REPO / 'design' / 'mark'; MARK.mkdir(parents=True, exist_ok=True)
SIZED = (16, 22, 24, 32, 48, 64)
PNG = (16, 22, 24, 32, 48, 64, 128, 256, 512)

def icon(px=None):
    return G.svg(G.corners(px or 128), px) + '\n'

for old in ICONS.glob('*-apps-ectolens.svg'):
    old.unlink()
(ICONS / 'sc-apps-ectolens.svg').write_text(icon())
for px in SIZED:
    (ICONS / f'{px}-apps-ectolens.svg').write_text(icon(px))
(MARK / 'ectolens.svg').write_text(icon())

x = 0; html = ''
for px in PNG:
    html += f'<div style="position:absolute;left:{x}px;top:0;width:{px}px;height:{px}px">{G.svg(G.corners(px), px, gid=f"g{px}")}</div>'
    x += px + 20
page = MARK / '_render.html'; shot = MARK / '_render.png'
page.write_text(f'<!doctype html><html><body style="margin:0;background:transparent">{html}</body></html>')
subprocess.run(['google-chrome', '--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
                '--default-background-color=00000000', f'--window-size={x + 20},{max(PNG) + 20}', f'--screenshot={shot}',
                f'file://{page}'], capture_output=True, check=True)
sheet = Image.open(shot).convert('RGBA'); x = 0
for px in PNG:
    sheet.crop((x, 0, x + px, px)).save(MARK / f'ectolens-{px}.png'); x += px + 20
page.unlink(); shot.unlink()
print('wrote', len(SIZED) + 1, 'icon SVGs in', ICONS, 'and', len(PNG), 'PNGs in', MARK)
