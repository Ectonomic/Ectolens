#!/usr/bin/env python3
# Cuts the README's pictures out of the films that tools/readme-pictures.sh makes
# (tests/e2e/overlay/readme-scenes.sh films each scene in an off-screen KWin; film.py writes
# the frames). Every picture the README shows is a cut in CUTS below, and test_readme_pictures.py
# holds the README, CUTS and docs/pictures/ to each other.
#   moving cuts: an animated WebP, the frames as filmed (their own times), from FROM to TO seconds
#   still cuts:  a still WebP, the film's frame nearest AT seconds
#   reveal cuts: an animated WebP of two stills, BEFORE (Spectacle) and AT (Ectolens), with a divider
#                sweeping from one to the other and back, each side labeled
# Areas are in the screen's own coordinates (1280×720; the frames are twice that).
# usage: tools/readme-pictures.py FILMS_DIR OUT_DIR [CUT ... | --scenes SCENE ...]
import os, sys, zlib
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SCALE = 2
FONT = os.path.join(HERE, '..', 'src', 'fonts', 'Geist-Medium.ttf')

# Every picture the README shows: its scene's film, moving or still, the part of the screen it
# shows (x, y, width, height, in the screen's 1280×720), when (seconds into the film, or a mark the
# scene stamped, with seconds after it) and the picture's width in pixels. A literal, so
# tests/ReadmePicturesTest.py can read it without running anything.
# DETAIL: the terminal, its frame and the overlay's rows around it.
CUTS = {
    'readme-hero': {'scene': 'hero', 'kind': 'moving', 'area': (0, 0, 1280, 720), 'from': 0.4, 'to': None, 'width': 1600,
                    'keys': [('printscreen', 'Print Screen')]},
    'readme-after-capture': {'scene': 'menus', 'kind': 'still', 'area': (10, 44, 760, 570), 'at': ('aftermenu', 0), 'width': 1520},
    'readme-record-menu': {'scene': 'menus', 'kind': 'still', 'area': (10, 44, 760, 570), 'at': ('recordmenu', 0), 'width': 1520},
    'readme-recording-settings': {'scene': 'settings', 'kind': 'still', 'area': (140, 6, 1000, 708), 'at': ('recording', 0), 'width': 1600},
    'readme-sound-tracks': {'scene': 'settings', 'kind': 'still', 'area': (140, 6, 1000, 708), 'at': ('sound', 0), 'width': 1600},
    'readme-marks': {'scene': 'marks', 'kind': 'moving', 'area': (10, 44, 760, 570), 'from': 0, 'to': ('box', 0.2), 'width': 1520},
    'readme-text-box': {'scene': 'marks', 'kind': 'moving', 'area': (10, 44, 760, 570), 'from': ('box', 0), 'to': None, 'width': 1520},
    'readme-color-menu': {'scene': 'menus', 'kind': 'still', 'area': (400, 44, 420, 460), 'at': ('colormenu', 0), 'width': 840},
    'readme-font-menu': {'scene': 'menus', 'kind': 'still', 'area': (440, 44, 420, 460), 'at': ('fontmenu', 0), 'width': 840},
    'readme-hide-private': {'scene': 'private', 'kind': 'moving', 'area': (10, 44, 760, 570), 'from': 0, 'to': None, 'width': 1520},
    'readme-sizes': {'scene': 'size', 'kind': 'moving', 'area': (0, 0, 1280, 720), 'from': 0, 'to': None, 'width': 1600},
    'readme-pick-color': {'scene': 'tools', 'kind': 'still', 'area': (40, 160, 480, 360), 'at': ('pickcolor', 0), 'width': 960},
    'readme-measure': {'scene': 'tools', 'kind': 'still', 'area': (60, 150, 480, 360), 'at': ('measure', 0), 'width': 960},
    'readme-qr-code': {'scene': 'tools', 'kind': 'still', 'area': (300, 260, 480, 360), 'at': ('qrmenu', 0), 'width': 960},
    'readme-pin': {'scene': 'tools', 'kind': 'still', 'area': (227, 80, 853, 640), 'at': ('pinned', 0), 'width': 960},
    'readme-window': {'scene': 'window', 'kind': 'still', 'area': (150, 14, 980, 690), 'at': ('window', 0), 'width': 1200},
    'readme-window-light': {'scene': 'light', 'kind': 'still', 'area': (150, 14, 980, 690), 'at': ('window', 0), 'width': 1200},
    'readme-ecto-look': {'scene': 'ectolook', 'kind': 'still', 'area': (10, 44, 760, 570), 'at': ('look', 0), 'width': 1140},
    'readme-kde-look': {'scene': 'kdelook', 'kind': 'still', 'area': (10, 44, 760, 570), 'at': ('look', 0), 'width': 1140},
    # Spectacle's interface and Ectolens's at the same moment, a divider sweeping from one to the
    # other (Spectacle as this computer has it, filmed on the same desktop)
    'readme-vs-unframed': {'scene': 'hero', 'kind': 'reveal', 'before': ('spectacle', ('unframed', 0)), 'at': ('pointing', 0),
                           'area': (0, 0, 1280, 720), 'width': 1600},
    'readme-vs-framed': {'scene': 'marks', 'kind': 'reveal', 'before': ('spectacle', ('framed', 0)), 'at': ('framed', 0),
                         'area': (0, 0, 1280, 720), 'width': 1600},
    'readme-vs-marking': {'scene': 'marks', 'kind': 'reveal', 'before': ('spectacle', ('numbers', 0)), 'at': ('numbers', 0),
                          'area': (40, 20, 720, 340), 'width': 1440},
    'readme-vs-record': {'scene': 'menus', 'kind': 'reveal', 'before': ('spectacle', ('recordmenu', 0)), 'at': ('recordmenu', 0),
                         'area': (0, 0, 1280, 720), 'width': 1600},
    'readme-vs-window': {'scene': 'window', 'kind': 'reveal', 'before': ('spectacle', ('window', 0)), 'at': ('window', 0),
                         'area': (0, 0, 1280, 720), 'width': 1600},
    'readme-agent': {'scene': 'agent', 'kind': 'moving', 'area': (0, 0, 1280, 720), 'from': 0, 'to': ('asking', 1.4), 'width': 1600},
    'readme-appearance': {'scene': 'settings', 'kind': 'still', 'area': (140, 6, 1000, 708), 'at': ('appearance', 0), 'width': 1600},
}


def when(c_time, m, last):
    """seconds into the film: a number, a (mark, seconds after it), or None for the film's end"""
    if c_time is None:
        return last
    if isinstance(c_time, (int, float)):
        return float(c_time)
    mark, after = c_time
    if mark not in m:
        raise SystemExit(f'the film has no mark {mark!r} (its marks: {sorted(m)})')
    return m[mark] + after


def frames(folder):
    rows = [line.split() for line in open(os.path.join(folder, 'frames.txt'))]
    return [(int(n), float(t), int(w), int(h), int(s)) for n, t, w, h, s in rows]


def picture(folder, row):
    n, _, w, h, s = row
    data = zlib.decompress(open(os.path.join(folder, f'{n:05d}.zraw'), 'rb').read())
    a = np.frombuffer(data, np.uint8).reshape(h, s)[:, :w * 4].reshape(h, w, 4)
    return Image.fromarray(a[:, :, [2, 1, 0]].copy())


def marks(folder):
    # the scene's marks, stamped with the wall clock, as seconds into the film
    path = os.path.join(os.path.dirname(folder), 'marks-wall.txt')
    start = os.path.join(folder, 'start.txt')
    if not os.path.exists(path) or not os.path.exists(start):
        return {}
    began = float(open(start).read())
    return {name: float(t) - began for name, t in (line.split() for line in open(path))}


def keycap(im, words, strength):
    # a key, as a hand presses it, in the picture's bottom left corner: what was pressed
    # where the film can't show it. Drawn like a light keyboard key, so it never reads as
    # part of Ectolens.
    if strength <= 0:
        return im
    k = im.width / 1600
    font = ImageFont.truetype(FONT, round(26 * k))
    pad_x, pad_y = round(22 * k), round(14 * k)
    tw = font.getlength(words)
    w, h = round(tw + 2 * pad_x), round(26 * k + 2 * pad_y)
    x, y = round(36 * k), im.height - h - round(36 * k)
    layer = Image.new('RGBA', im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    shadow = Image.new('RGBA', im.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((x, y + round(6 * k), x + w, y + h + round(6 * k)), round(12 * k), fill=(0, 0, 0, 110))
    shadow = shadow.filter(ImageFilter.GaussianBlur(round(8 * k)))
    d.rounded_rectangle((x, y, x + w, y + h), round(12 * k), fill=(236, 236, 240, 255), outline=(190, 190, 198, 255), width=max(1, round(2 * k)))
    d.rounded_rectangle((x + round(3 * k), y + round(3 * k), x + w - round(3 * k), y + h - round(7 * k)), round(10 * k), fill=(250, 250, 252, 255))
    d.text((x + pad_x, y + pad_y - round(3 * k)), words, font=font, fill=(24, 24, 27, 255))
    layer = Image.alpha_composite(shadow, layer)
    alpha = layer.getchannel('A').point(lambda v: round(v * strength))
    layer.putalpha(alpha)
    return Image.alpha_composite(im.convert('RGBA'), layer).convert('RGB')


def cut_area(im, area, width):
    x, y, w, h = (v * SCALE for v in area)
    im = im.crop((x, y, x + w, y + h))
    if width and im.width != width:
        im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
    return im


def moving(name, c, films, out):
    folder = os.path.join(films, c['scene'], 'film', c['scene'])
    rows = frames(folder)
    m = marks(folder)
    start, end = when(c['from'], m, rows[-1][1]), when(c['to'], m, rows[-1][1])
    rows = [r for r in rows if start <= r[1] <= end]
    shown, durations = [], []
    last = None
    for i, row in enumerate(rows):
        t = row[1]
        im = cut_area(picture(folder, row), c['area'], c['width'])
        for mark, words in c.get('keys', []):
            if mark in m:
                age = t - m[mark]
                # in over 0.12 s, held, out over 0.25 s
                strength = 0 if age < 0 or age > 1.6 else min(1, age / 0.12) if age < 0.12 else 1 if age < 1.35 else (1.6 - age) / 0.25
                im = keycap(im, words, strength)
        nxt = rows[i + 1][1] if i + 1 < len(rows) else t + 1.2
        ms = max(20, round((nxt - t) * 1000))
        # a frame no different from the one before only lengthens it
        if last is not None and not ImageChops.difference(im, last).getbbox():
            durations[-1] += ms
            continue
        shown.append(im)
        durations.append(ms)
        last = im
    durations[-1] += 1200   # a moment on the last frame before it starts again
    path = os.path.join(out, name + '.webp')
    shown[0].save(path, save_all=True, append_images=shown[1:], duration=durations, loop=0,
                  quality=82, method=6, minimize_size=False, allow_mixed=False)
    print(f'{path}: {len(shown)} frames, {sum(durations) / 1000:.1f} s, {os.path.getsize(path) / 1e6:.2f} MB')


def still(name, c, films, out):
    folder = os.path.join(films, c['scene'], 'film', c['scene'])
    rows = frames(folder)
    at = when(c['at'], marks(folder), rows[-1][1])
    row = min(rows, key=lambda r: abs(r[1] - at))
    im = cut_area(picture(folder, row), c['area'], c['width'])
    path = os.path.join(out, name + '.webp')
    im.save(path, quality=90, method=6)
    print(f'{path}: {im.size[0]}×{im.size[1]}, {os.path.getsize(path) / 1e6:.2f} MB')


REVEAL_LABELS = ('Spectacle 6.7.5', 'Ectolens')


def frame_at(scene, at, films, area, width):
    folder = os.path.join(films, scene, 'film', scene)
    rows = frames(folder)
    t = when(at, marks(folder), rows[-1][1])
    return cut_area(picture(folder, min(rows, key=lambda r: abs(r[1] - t))), area, width)


def label(im, words, right):
    # a dark pill with the app's name, in a top corner of its side
    k = im.width / 1600
    font = ImageFont.truetype(FONT, round(24 * k))
    pad_x, pad_y = round(16 * k), round(9 * k)
    w, h = round(font.getlength(words) + 2 * pad_x), round(24 * k + 2 * pad_y)
    x = im.width - w - round(24 * k) if right else round(24 * k)
    y = round(24 * k)
    layer = Image.new('RGBA', im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.rounded_rectangle((x, y, x + w, y + h), h // 2, fill=(10, 10, 10, 220), outline=(255, 255, 255, 60), width=max(1, round(k)))
    d.text((x + pad_x, y + pad_y - round(3 * k)), words, font=font, fill=(250, 250, 250, 255))
    return Image.alpha_composite(im, layer)


def reveal(name, c, films, out):
    before_scene, before_at = c['before']
    a = frame_at(before_scene, before_at, films, c['area'], c['width']).convert('RGBA')
    b = frame_at(c['scene'], c['at'], films, c['area'], c['width']).convert('RGBA')
    W, H = a.size
    k = W / 1600

    def split(x):
        # Spectacle left of the divider, Ectolens right of it, and the divider with its handle
        im = a.copy()
        x = max(0, min(W, round(x)))
        im.paste(b.crop((x, 0, W, H)), (x, 0))
        d = ImageDraw.Draw(im)
        lw = max(2, round(3 * k))
        d.rectangle((x - lw // 2 - 1, 0, x + lw // 2 + 1, H), fill=(0, 0, 0, 90))
        d.rectangle((x - lw // 2, 0, x + lw // 2, H), fill=(255, 255, 255, 255))
        r = round(22 * k)
        cy = H // 2
        d.ellipse((x - r - 2, cy - r - 2, x + r + 2, cy + r + 2), fill=(0, 0, 0, 90))
        d.ellipse((x - r, cy - r, x + r, cy + r), fill=(255, 255, 255, 255))
        t = round(7 * k)
        d.polygon([(x - round(5 * k), cy - t), (x - round(5 * k), cy + t), (x - round(13 * k), cy)], fill=(24, 24, 27, 255))
        d.polygon([(x + round(5 * k), cy - t), (x + round(5 * k), cy + t), (x + round(13 * k), cy)], fill=(24, 24, 27, 255))
        # each side's name in its corner, over everything: left of the divider is always Spectacle
        return label(label(im, REVEAL_LABELS[0], False), REVEAL_LABELS[1], True).convert('RGB')

    shown, durations = [], []
    def add(im, ms):
        shown.append(im)
        durations.append(ms)
    steps = 36   # a sweep over 1.44 s at 25 frames a second

    def ease(t):
        return t * t * (3 - 2 * t)
    add(split(W * 0.94), 1400)                                  # Spectacle, a sliver of Ectolens at the right
    for i in range(1, steps + 1):                               # sweep left: Ectolens takes over
        add(split(W * (0.94 - 0.88 * ease(i / steps))), 40)
    durations[-1] = 1900                                        # Ectolens, a sliver of Spectacle at the left
    for i in range(1, steps + 1):                               # and back
        add(split(W * (0.06 + 0.88 * ease(i / steps))), 40)
    durations[-1] = 400
    path = os.path.join(out, name + '.webp')
    shown[0].save(path, save_all=True, append_images=shown[1:], duration=durations, loop=0, quality=84, method=6)
    print(f'{path}: {len(shown)} frames, {sum(durations) / 1000:.1f} s, {os.path.getsize(path) / 1e6:.2f} MB')


def main():
    films, out = sys.argv[1], sys.argv[2]
    rest = sys.argv[3:]
    if rest[:1] == ['--scenes']:
        wanted = [name for name, c in CUTS.items() if c['scene'] in rest[1:] or c.get('before', ('',))[0] in rest[1:]]
    else:
        wanted = rest or list(CUTS)
    os.makedirs(out, exist_ok=True)
    for name in wanted:
        c = CUTS[name]
        {'moving': moving, 'still': still, 'reveal': reveal}[c['kind']](name, c, films, out)


if __name__ == '__main__':
    main()
