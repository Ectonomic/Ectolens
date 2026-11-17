#!/usr/bin/env python3
"""Builds Ectolens's icon from glyphs.corners (mark round one, pulled in 1.5 units 2026-11-17): the Ecto splat with
viewfinder corners in the family's dark green, in the rounded box the family's app icons share: flat #0a0a0a
filling the picture, corners of radius 14 in 64, the splat 0.607 of the box's width, centered. Up to 24 px the
mark is larger in the box (0.80), so its corners stay readable. Each size KDE draws gets
its own SVG at the line weight of the size the mark is shown at (ECM's <size>-apps-ectolens.svg), plus the
scalable master; PNGs for the mock and checks. design/mark/ectolens.svg stays the bare mark, for pages.

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

BOX = '#0a0a0a'
SPLAT_CENTER = (32.03, 32.5)  # the middle of the splat's bounds in its 64-unit box

def boxed(px=None, gid='g'):
    p = px or 128
    s = 0.80 if p <= 24 else 0.607 * 64 / 61   # the splat is 61 units wide
    tx, ty = 32 - SPLAT_CENTER[0] * s, 32 - SPLAT_CENTER[1] * s
    size = f' width="{px}" height="{px}"' if px else ''
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"{size}>'
            f'<defs><linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="4" y1="4" x2="60" y2="60">'
            f'<stop offset="0" stop-color="#c8f22e"/><stop offset="1" stop-color="#9be012"/></linearGradient></defs>'
            f'<rect width="64" height="64" rx="14" fill="{BOX}"/>'
            f'<g transform="translate({tx:.3f} {ty:.3f}) scale({s:.4f})"><path d="{G.H.SPLAT}" fill="url(#{gid})"/>'
            f'{G.corners(p * s)}</g></svg>')

def icon(px=None):
    return boxed(px) + '\n'

for old in ICONS.glob('*-apps-ectolens.svg'):
    old.unlink()
(ICONS / 'sc-apps-ectolens.svg').write_text(icon())
for px in SIZED:
    (ICONS / f'{px}-apps-ectolens.svg').write_text(icon(px))
(MARK / 'ectolens.svg').write_text(G.svg(G.corners(128)) + '\n')
(MARK / 'ectolens-app.svg').write_text(icon())

x = 0; html = ''
for px in PNG:
    html += f'<div style="position:absolute;left:{x}px;top:0;width:{px}px;height:{px}px">{boxed(px, gid=f"g{px}")}</div>'
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
