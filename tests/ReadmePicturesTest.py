# SPDX-FileCopyrightText: 2026 Ectolens contributors
# SPDX-License-Identifier: LGPL-2.0-or-later
#
# The README's pictures are the app as filmed: every picture README.md shows is in docs/pictures/,
# says what it shows (its alt text), and is a cut of tools/readme-pictures.py (moving or still, at
# its width; a reveal, which sweeps between two scenes' pictures, moves), from scenes
# tests/e2e/overlay/readme-scenes.sh films; and every file in
# docs/pictures/ is such a cut and is shown. The names come from those three places, never from a
# list here. Plain Python, so it runs wherever the unit tests do.
# usage: python3 -I ReadmePicturesTest.py SOURCE_DIR
import ast, html, os, re, sys

root = sys.argv[1]
failures = []


def fail(message):
    failures.append(message)


# the README's pictures, with their alt text
readme = open(os.path.join(root, 'README.md'), encoding='utf-8').read()
shown = {}
for tag in re.findall(r'<img\s[^>]*>', readme):
    src = re.search(r'src="([^"]+)"', tag)
    alt = re.search(r'alt="([^"]*)"', tag)
    if src and src.group(1).startswith('docs/pictures/'):
        shown[os.path.basename(src.group(1))] = html.unescape(alt.group(1)).strip() if alt else ''
for alt, src in re.findall(r'!\[([^\]]*)\]\((docs/pictures/[^)]+)\)', readme):
    shown[os.path.basename(src)] = alt.strip()

# the cuts, read as the literal they are (tools/readme-pictures.py runs NumPy and Pillow; this doesn't)
tree = ast.parse(open(os.path.join(root, 'tools', 'readme-pictures.py'), encoding='utf-8').read())
cuts = next(ast.literal_eval(n.value) for n in tree.body
            if isinstance(n, ast.Assign) and getattr(n.targets[0], 'id', '') == 'CUTS')

# the scenes the film tool runs
scenes_src = open(os.path.join(root, 'tests', 'e2e', 'overlay', 'readme-scenes.sh'), encoding='utf-8').read()
scenes = re.search(r'^SCENES="([^"]*)"', scenes_src, re.M).group(1).split()

folder = os.path.join(root, 'docs', 'pictures')
files = sorted(os.listdir(folder)) if os.path.isdir(folder) else []


def webp(path):
    """(animated, frames, width, height) from a WebP file's own chunks, or None if it isn't one"""
    data = open(path, 'rb').read()
    if data[:4] != b'RIFF' or data[8:12] != b'WEBP':
        return None
    animated, frames, width, height = False, 0, 0, 0
    i = 12
    while i + 8 <= len(data):
        kind, size = data[i:i + 4], int.from_bytes(data[i + 4:i + 8], 'little')
        body = data[i + 8:i + 8 + size]
        if kind == b'VP8X':
            animated = bool(body[0] & 0x02)
            width = 1 + int.from_bytes(body[4:7], 'little')
            height = 1 + int.from_bytes(body[7:10], 'little')
        elif kind == b'ANMF':
            frames += 1
        elif kind == b'VP8 ' and not width:
            width = int.from_bytes(body[6:8], 'little') & 0x3fff
            height = int.from_bytes(body[8:10], 'little') & 0x3fff
        elif kind == b'VP8L' and not width:
            bits = int.from_bytes(body[1:5], 'little')
            width, height = 1 + (bits & 0x3fff), 1 + ((bits >> 14) & 0x3fff)
        i += 8 + size + (size & 1)
    return animated, frames, width, height


for name, alt in shown.items():
    if name not in files:
        fail(f'README.md shows docs/pictures/{name}, which is not there')
    if not alt:
        fail(f'README.md shows docs/pictures/{name} without saying what it shows (alt text)')
for name in files:
    if name not in shown:
        fail(f'docs/pictures/{name} is not shown in README.md')
    if os.path.splitext(name)[0] not in cuts:
        fail(f'docs/pictures/{name} is no cut of tools/readme-pictures.py, so nothing makes it again')
for name, cut in cuts.items():
    path = os.path.join(folder, name + '.webp')
    for scene in [cut['scene']] + ([cut['before'][0]] if 'before' in cut else []):
        if scene not in scenes:
            fail(f'{name}: its scene {scene!r} is not one readme-scenes.sh films ({" ".join(scenes)})')
    if not os.path.exists(path):
        fail(f'{name}: tools/readme-pictures.py cuts it, but docs/pictures/{name}.webp is not there')
        continue
    info = webp(path)
    if info is None:
        fail(f'docs/pictures/{name}.webp is not a WebP picture')
        continue
    animated, frames, width, height = info
    # a reveal moves too: a divider sweeping from one picture to the other
    if cut['kind'] in ('moving', 'reveal') and not (animated and frames > 1):
        fail(f'docs/pictures/{name}.webp should move, and has {frames} frames')
    if cut['kind'] == 'still' and animated:
        fail(f'docs/pictures/{name}.webp should be still, and moves')
    if width != cut['width']:
        fail(f'docs/pictures/{name}.webp is {width} pixels wide; its cut makes it {cut["width"]}')

for message in failures:
    print('FAIL ', message)
print(f'{len(shown)} pictures shown, {len(files)} in docs/pictures, {len(cuts)} cuts: '
      + ('all held' if not failures else f'{len(failures)} failures'))
sys.exit(1 if failures else 0)
