# Judges an overlay test run (run.sh): PASS/FAIL lines, exit 1 on any failure.
# The frame is the script's frame:300,150,800,500 on a 1600x900 virtual screen.
import json, os, subprocess, sys
from PIL import Image

out = sys.argv[1]
scenario = sys.argv[2] if len(sys.argv) > 2 else 'frame'
def notifications():
    path = f'{out}/notifications.jsonl'
    return [json.loads(l) for l in open(path)] if __import__('os').path.exists(path) else []


def lines(prefix):
    return [l.split()[1:] for l in open(f'{out}/ectolens.log').read().splitlines() if l.startswith(prefix + ' ') or l == prefix]


def report(results):
    for ok, name, why in results:
        print(('PASS  ' if ok else 'FAIL  ') + name + ('' if ok else f' ({why})'))
    sys.exit(0 if all(r[0] for r in results) else 1)


def options_places():
    # every report of the options row's place with the tool row and the tool in use's button, in log order:
    # [{'step', 'row': [x,y,w,h], 'tool': (name, [x,y,w,h]), 'top': [x,y,w,h]}]
    places, step, cur = [], None, None
    for l in open(f'{out}/ectolens.log').read().splitlines():
        p = l.split()
        if p[:1] == ['ECTOLENS-STEP']:
            step = p[1]
        elif p[:2] == ['ECTOLENS-RECT', 'optionsRow']:
            cur = {'step': step, 'row': [int(v) for v in p[2:6]], 'tool': None, 'top': None}
            places.append(cur)
        elif p[:1] == ['ECTOLENS-CURRENTTOOL'] and cur is not None and cur['tool'] is None:
            cur['tool'] = (p[1], [int(v) for v in p[2:6]]) if len(p) >= 6 else (p[1], None)
        elif p[:2] == ['ECTOLENS-RECT', 'topRow'] and cur is not None and cur['top'] is None:
            cur['top'] = [int(v) for v in p[2:6]]
    return places


def options_pad():
    v = [l for l in lines('ECTOLENS-PROP') if l[:2] == ['optionsRow', 'sidePadding']]
    return float(v[0][2]) if v else None


def judge_place(pl, pad):
    # The options lie in a row above the tool row (below it only when the row has no room above), and
    # the button of the tool that called them stands right under the row's last option (a tool on the
    # tool row's right half) or its first (the left half).
    row, top = pl['row'], pl['top']
    name, tool = pl['tool'] or ('none', None)
    if not top or not tool or pad is None:
        return f"{pl['step']}: missing a report (row {row}, tool {pl['tool']}, tool row {top}, padding {pad})"
    x, y, w, h = row
    if not w > h:
        return f"{pl['step']}: the row stands on end ({w}x{h})"
    above = y + h <= top[1] - 4 and y + h >= top[1] - 20
    below = y >= top[1] + top[3] + 4 and y <= top[1] + top[3] + 20
    # the screen the tool row is on: below is right only when the row has no room above on it
    screens = [[int(v) for v in l[:4]] for l in lines('ECTOLENS-SCREEN')]
    cx, cy = top[0] + top[2] / 2, top[1] + top[3] / 2
    scr = next((sc for sc in screens if sc[0] <= cx < sc[0] + sc[2] and sc[1] <= cy < sc[1] + sc[3]), [0, 0, 0, 0])
    no_room_above = top[1] - 4 - h < scr[1] + 12
    if not ((above and not no_room_above) or (below and no_room_above)):
        return f"{pl['step']}: the row at {row} is not just above the tool row at {top}"
    right_half = tool[0] + tool[2] / 2 > top[0] + top[2] / 2
    if right_half and abs((x + w - pad) - (tool[0] + tool[2])) > 1:
        return f"{pl['step']}: {name} is on the right half, but the row's end {x + w - pad} is not over its right edge {tool[0] + tool[2]}"
    if not right_half and abs((x + pad) - tool[0]) > 1:
        return f"{pl['step']}: {name} is on the left half, but the row's start {x + pad} is not over its left edge {tool[0]}"
    return None


def judge_sliders():
    # every slider the app reported (found by its type, and any two-way picker whose thumb stays inside):
    # its thumb inside its track whatever its value, and room past its ends when it stands in a row of
    # controls (the blur sliders need padding on left and right).
    # Returns the faults and, per slider, the values it was seen at.
    bad, seen = [], {}
    for n, x, y, w, h, pad, value, tx, ty, tw, th, inrow in (l for l in lines('ECTOLENS-SLIDER') if len(l) == 12):
        x, y, w, h, pad, value, tx, ty, tw, th = (float(v) for v in (x, y, w, h, pad, value, tx, ty, tw, th))
        seen.setdefault(n, set()).add(round(value, 3))
        if tx < x + pad - 0.5 or tx + tw > x + w - pad + 0.5:
            bad.append(f'{n} at {value}: the thumb {tx:.1f}..{tx + tw:.1f} leaves the track {x + pad:.1f}..{x + w - pad:.1f}')
        if ty < y - 0.5 or ty + th > y + h + 0.5:
            bad.append(f'{n} at {value}: the thumb {ty:.1f}..{ty + th:.1f} leaves it up and down {y:.1f}..{y + h:.1f}')
        if inrow == '1' and pad < 6:
            bad.append(f'{n} stands in a row with {pad} px past its ends')
    return bad, seen


def lime(p):
    # Ectolens's default accent, Ecto green (#c8f22e), as KWin's screenshot shows it
    r, g, b = p
    return g > 190 and r > 140 and b < 120


if scenario == 'startup':
    # Print Screen from cold, as on the desktop: D-Bus starts Ectolens on the key (inner.sh, startup).
    # Print Screen must not freeze the whole desktop while it loads.
    import datetime, re
    def wall(hms):
        h, m, rest = hms.split(':')
        return (int(h) * 3600 + int(m) * 60 + float(rest)) * 1000
    presses = [wall(l.split()[2]) for l in open(f'{out}/presses.log') if l.startswith('PRESS ')]
    starts = [dict(re.findall(r'(\S+)=(\S+)', l)) for l in open(f'{out}/ectolens.log') if 'ECTOLENS-STARTUP ' in l]
    watch = open(f'{out}/compositor-watch.log').read()
    m = re.search(r'WATCH samples=(\d+) longest=(\d+)', watch)
    samples, longest = (int(m.group(1)), int(m.group(2))) if m else (0, 9999)
    results = [(len(starts) == len(presses) and len(presses) > 0, 'every press started Ectolens and drew the overlay', f'{len(starts)} of {len(presses)}')]
    waits = []
    for press, st in zip(presses, starts):
        started = datetime.datetime.fromtimestamp(int(st['started']) / 1000)
        start_ms = (started.hour * 3600 + started.minute * 60 + started.second) * 1000 + started.microsecond / 1000
        waits.append(round(start_ms + float(st.get('drawn', 1e9)) - press))
    results.append((bool(waits) and max(waits) < 1000, 'the overlay is drawn within a second of the key', f'{waits} ms'))
    stalls = [int(st['longest-stall'].rstrip('ms')) for st in starts]
    results.append((bool(stalls) and max(stalls) < 250, 'Ectolens never stops answering for a quarter second', f'{stalls} ms'))
    results.append((samples > 100 and longest < 100, 'KWin keeps answering (no answer slower than 100 ms) while Ectolens starts', f'longest {longest} ms over {samples} answers'))
    print(f'      press to drawn: {waits} ms; Ectolens longest stall: {stalls} ms; KWin longest answer: {longest} ms')
    report(results)

if scenario == 'printkey':
    # Print Screen's own path (Activate, start mode Gui), frame Konsole, Enter, with settings untouched.
    # Design proposal 2: Enter is Capture; After Capture copies only by default, the overlay closes
    # instead of opening the Ectolens window, and KDE's notification says what was taken.
    windows = open(f'{out}/windows.txt').read()
    viewer = 'com.ectonomic.ectolens normal=true' in windows
    saved = open(f'{out}/saved.txt').read().split()
    clip = lines('ECTOLENS-CLIPBOARD')
    notes = notifications()
    note = notes[-1] if notes else {}
    results = [
        (not viewer, 'Enter after Print Screen leaves no Ectolens window open', windows.strip()[-200:]),
        (not saved, 'nothing was saved to Screenshots (copy only by default)', ' '.join(saved)),
        (any(c[:1] == ['image'] for c in clip), 'the picture went to the clipboard', str(clip)),
        (lines('ECTOLENS-FRAME')[:1] == [['400', '196', '799', '508', 'window']], 'a real click on Konsole frames it', str(lines('ECTOLENS-FRAME'))),
        (note.get('title') == 'Konsole 799×508', "the notification's title names the app and the size", note.get('title')),
        (note.get('text') == 'Copied to the clipboard.', 'the notification says it was copied', note.get('text')),
        ('Save' in note.get('buttons', []) and 'Annotate' in note.get('buttons', []), 'the notification offers Save and Annotate', note.get('buttons')),
        (open(f'{out}/process.txt').read().strip() == 'running', 'Ectolens stays running while the notification can still save', open(f'{out}/process.txt').read().strip()),
    ]
    report(results)
if scenario == 'toggle':
    windows = open(f'{out}/windows.txt').read()
    report([
        (bool(lines('ECTOLENS-CLOSED-BY-SECOND-PRESS')), 'Print Screen pressed again while the overlay is up closes it', ''),
        (not [l for l in windows.splitlines() if len(l.split()) > 2 and 'ectolens' in l.split()[2].lower()],
         'no Ectolens window or overlay is left', windows.strip()[-200:]),
        (not lines('ECTOLENS-CLIPBOARD'), 'nothing was captured', str(lines('ECTOLENS-CLIPBOARD'))),
        (open(f'{out}/process.txt').read().strip() == 'exited', 'Ectolens quit, as Esc makes it', open(f'{out}/process.txt').read().strip()),
    ])
if scenario == 'toolclicks':
    # after every real click on a tool button (or on the picture), the options rail shows for the
    # tools that have options: Text, Number, Rectangle, Arrow
    steps, cur = [], None
    for l in open(f'{out}/ectolens.log').read().splitlines():
        if l.startswith('ECTOLENS-STEP '):
            cur = {'step': l.split()[1], 'tool': None, 'rail': None}
            steps.append(cur)
        elif cur and l.startswith('ECTOLENS-TOOL '):
            cur['tool'] = l.split()[1]
        elif cur and l.startswith('ECTOLENS-RECT optionsRow '):
            cur['rail'] = l.split()[2:6]
    results = []
    for st in steps:
        if st['tool'] in ('TextTool', 'NumberTool', 'RectangleTool', 'ArrowTool'):
            results.append((st['rail'] is not None, f"after {st['step']} ({st['tool']}) the options row shows", 'no row'))
    results.append((len(results) >= 14, 'every click was judged', f'{len(results)} judged'))
    pad = options_pad()
    bad = [m for m in (judge_place(pl, pad) for pl in options_places()) if m]
    results.append((not bad and len(options_places()) >= 14, 'after every click the options lie above the tool row, lined up with the tool that called them',
                    '; '.join(bad) or f'{len(options_places())} places'))
    report(results)
if scenario == 'textbox':
    # A click and drag with the Text tool draws a text box that constrains the text.
    # The drag drew (40,40) to (240,110) inside the frame (wherever TEST_FRAME put it).
    from PIL import Image as _I
    fr = (lines('ECTOLENS-FRAME')[-1:] or [['400', '196', '799', '508']])[0]
    FX, FY = int(fr[0]), int(fr[1])
    texts = [l for l in lines('ECTOLENS-TEXT')]
    def info(i):
        if i >= len(texts) or texts[i][0] == 'none':
            return None
        t = texts[i]
        x, y, w, h = (int(v) for v in t[1:5])
        ww, wh = (int(v) for v in t[5].split('=')[1].split(','))
        return {'box': t[0] == 'box=1', 'x': x, 'y': y, 'w': w, 'h': h, 'wh': wh, 'chars': int(t[6].split('=')[1]),
                'align': int(t[7].split('=')[1]) if len(t) > 7 else None}
    drawn, typed, resized, padded, label = info(0), info(1), info(2), info(3), info(4)
    label_padded, shifted, ctrled, centered, mid = info(5), info(6), info(7), info(8), info(9)
    tools = [l for l in lines('ECTOLENS-TOOL')]
    def tool_align(i):
        return int(tools[i][-1].split('=')[1]) if i < len(tools) and tools[i][-1].startswith('align=') else None
    words = 'click record to start then pick the window you want to keep'
    handles = [l for l in lines('ECTOLENS-AT') if l and l[0] == 'textBoxHandle4']
    results = []
    results.append((bool(drawn) and drawn['box'] and abs(drawn['x'] - (FX + 40)) <= 1 and abs(drawn['y'] - (FY + 40)) <= 1 and abs(drawn['w'] - 200) <= 1 and drawn['h'] >= 69,
                    'a drag with the Text tool draws a box where the pointer went', str(drawn)))
    results.append((bool(typed) and typed['box'] and typed['chars'] == len(words) and typed['w'] == drawn['w'] and typed['x'] == drawn['x'],
                    'typed words go into the box, which keeps its width', str(typed)))
    results.append((bool(resized) and resized['box'] and abs(resized['w'] - 100) <= 2 and resized['x'] == drawn['x'] and resized['h'] > typed['h'],
                    "dragging the box's right handle in narrows it and the words rewrap onto more lines", f'{resized} after {typed}'))
    results.append((bool(padded) and padded['box'] and padded['x'] == resized['x'] and padded['chars'] == len(words) + 3,
                    "a click in the box's padding puts the cursor in it: the next keys go into the same box", str(padded)))
    results.append((bool(label) and not label['box'] and label['chars'] == len('hi there'),
                    'a click that wobbles 3 px still makes a plain label', str(label)))
    results.append((len(handles) == 1, 'a plain label shows no box handles', f'{len(handles)} handle reports'))
    results.append((bool(label_padded) and bool(label) and not label_padded['box'] and label_padded['x'] == label['x'] and label_padded['chars'] == len('hi there') + 3,
                    "a click in a plain label's padding puts the cursor in it: the next keys go into the same label", f'{label_padded} after {label}'))
    results.append((bool(shifted) and shifted['box'] and abs(shifted['x'] - (FX + 500)) <= 1 and abs(shifted['y'] - (FY + 60)) <= 1
                    and abs(shifted['w'] - 100) <= 1 and abs(shifted['h'] - 100) <= 1,
                    'a drag with Shift held draws a square box (its longer side) from the press', str(shifted)))
    results.append((bool(ctrled) and ctrled['box'] and abs(ctrled['x'] - (FX + 320)) <= 1 and abs(ctrled['y'] - (FY + 90)) <= 1
                    and abs(ctrled['w'] - 120) <= 1 and abs(ctrled['h'] - 60) <= 1,
                    'a drag with Ctrl held draws the box around the press', str(ctrled)))
    # the frame's handles sit on the frame line (they had drifted off it): their rect is
    # the middle of the line's stroke, at any scale
    geo = (lines('ECTOLENS-FRAMEGEO') or [None])[0]
    if geo:
        lx, ly, lw, lh, stroke, hx, hy, hw, hh, hline, radius = (float(v) for v in geo)
        line_middle = (lx + stroke / 2, ly + stroke / 2, lw - stroke, lh - stroke)
        on = all(abs(a - b) < 0.01 for a, b in zip((hx, hy, hw, hh), line_middle)) and abs(hline - stroke) < 0.01
        results.append((on, 'the frame handles are centered on the middle of the frame line', f'handles {hx},{hy} {hw}x{hh} line {hline}; the line\'s middle {line_middle}, stroke {stroke}'))
    else:
        results.append((False, 'the frame handles are centered on the middle of the frame line', 'no ECTOLENS-FRAMEGEO'))
    # and on the screen (1:1 shots only): at each edge handle, no green past the line on the frame's side
    # (the step a handle half a line inward left), and the circle reaching out its radius from the line
    try:
        shot = _I.open(f'{out}/framed.png').convert('RGB')
    except Exception:
        shot = None
    if shot is not None and shot.size == (1600, 900) and geo:
        def green(p):
            # the accent's hue, shadowed or not (the row above shades the top handle)
            r, g, b = p
            return g > 110 and g > r * 1.08 and g > b * 2.5
        cx, cy = lx + lw / 2, ly + lh / 2
        bad = []
        # (outward direction, along-axis fixed coordinate, line middle, sample function)
        for name, horizontal, line_mid, outward, at in (('top', True, ly + stroke / 2, -1, cx), ('bottom', True, ly + lh - stroke / 2, 1, cx),
                                                         ('left', False, lx + stroke / 2, -1, cy), ('right', False, lx + lw - stroke / 2, 1, cy)):
            def px(a, d):
                # a: position along the edge, d: position across it
                return shot.getpixel((int(a), int(d))) if horizontal else shot.getpixel((int(d), int(a)))
            def run(a):
                ds = [d for d in range(int(line_mid) - 14, int(line_mid) + 15) if green(px(a, d))]
                return (min(ds), max(ds)) if ds else None
            line_run = run(at + radius + 8)
            handle_run = run(at)
            if not line_run or not handle_run:
                bad.append(f'{name}: nothing green ({line_run}, {handle_run})')
                continue
            inner_line, inner_handle = (line_run[1], handle_run[1]) if outward < 0 else (line_run[0], handle_run[0])
            outer_handle = handle_run[0] if outward < 0 else handle_run[1]
            reach = abs(outer_handle - line_mid) + 0.5
            if inner_handle != inner_line or abs(reach - radius) > 1.2:
                bad.append(f'{name}: line {line_run}, handle {handle_run}, reaches {reach:.1f} of {radius:.0f}')
        results.append((not bad, "on the screen each edge handle's circle sits on the frame line, with no step inside it", '; '.join(bad) or 'four edges'))
    menu_items = [l[0] for l in lines('ECTOLENS-AT') if l and l[0].startswith('align-')]
    results.append(('align-4' in menu_items and 'align-128' in menu_items, "the rail's Align menu opens with a real click, holding Center and Middle", str(menu_items)))
    results.append((tool_align(0) == (4 | 32) and tool_align(1) == (4 | 128),
                    'Center, then Middle, picked with real clicks become the Text tool\'s alignment', str([t[-1] for t in tools])))
    results.append((bool(centered) and not centered['box'] and centered['align'] == (4 | 128) and centered['chars'] == len('centered words')
                    and abs(centered['x'] + centered['w'] / 2 - (FX + 600)) <= 1,
                    'a centered label grows both ways from where it was clicked', str(centered)))
    results.append((bool(mid) and mid['box'] and mid['align'] == (4 | 128) and abs(mid['x'] - (FX + 60)) <= 1 and abs(mid['w'] - 240) <= 1,
                    'a box drawn after takes Center and Middle', str(mid)))
    try:
        im = _I.open(f'{out}/saved.png').convert('RGB')
    except Exception as e:
        im = None
    def lime(p):
        r, g, b = p
        return g > 200 and r > 150 and b < 110
    ok = im is not None
    detail = 'no saved picture' if im is None else ''
    if ok:
        # where the real drag and the handle drag put it, not where Ectolens says it is: drawn at
        # (40,40) 200 wide, its right edge then dragged 100 px in; its height read from the picture,
        # down its left padding (no words there) to where the green ends
        k = im.width / int(fr[2])  # picture pixels per screen pixel (1.75 on a 1.75 scale screen)
        bx, by, bw = round(40 * k), round(40 * k), round(100 * k)
        # (starting below the rounded corner)
        y = by + round(6 * k)
        while y + 1 < im.height and lime(im.getpixel((bx + round(4 * k), y + 1))):
            y += 1
        bh = y - by + 1
        def strip(x0, x1, y0, y1):
            ps = [im.getpixel((x, y)) for x in range(x0, x1) for y in range(y0, y1)]
            return sum(lime(p) for p in ps) / max(1, len(ps))
        inside_left = lime(im.getpixel((bx + round(3 * k), by + bh // 2)))
        inside_top = lime(im.getpixel((bx + bw // 2, by + 2)))
        inside_bottom = lime(im.getpixel((bx + bw // 2, by + bh - 2)))
        # just past each edge, the picture: mostly not green (the box reaches no further)
        g4, g12 = round(4 * k), round(12 * k)
        past = {'right': strip(bx + bw + g4, bx + bw + g12, by + 3, by + bh - 3), 'left': strip(bx - g12, bx - g4, by + 3, by + bh - 3),
                'top': strip(bx + 6, bx + bw - 6, by - g12, by - g4), 'bottom': strip(bx + 6, bx + bw - 6, by + bh + g4, by + bh + g12)}
        thirds = []
        for k in range(3):
            y0, y1 = by + k * bh // 3, by + (k + 1) * bh // 3
            thirds.append(any(sum(im.getpixel((x, y))) < 200 for y in range(y0, y1) for x in range(bx + 2, bx + bw - 2)))
        ok = inside_left and inside_top and inside_bottom and all(v < 0.2 for v in past.values()) and all(thirds) and bh >= 69 * k
        detail = f'{bh} px tall; green inside left/top/bottom {inside_left}/{inside_top}/{inside_bottom}; green past the edges {past}; words in each third {thirds}'
    results.append((ok, 'the saved picture has the narrowed box: Ecto green to its edges and no further, its words wrapped down it', detail))
    # the centered box's word, "mid", in the middle of the box both ways in the saved picture (dark ink on green)
    ok2, detail2 = im is not None and bool(mid), 'no saved picture or box'
    if ok2:
        k = im.width / int(fr[2])
        x0, y0, x1, y1 = round((mid['x'] - FX) * k), round((mid['y'] - FY) * k), round((mid['x'] - FX + mid['w']) * k), round((mid['y'] - FY + mid['h']) * k)
        ink = [(x, y) for x in range(x0 + 3, x1 - 3) for y in range(y0 + 3, y1 - 3) if sum(im.getpixel((x, y))) < 200]
        if ink:
            cx = (min(p[0] for p in ink) + max(p[0] for p in ink)) / 2
            cy = (min(p[1] for p in ink) + max(p[1] for p in ink)) / 2
            ok2 = abs(cx - (x0 + x1) / 2) <= 3 * k and abs(cy - (y0 + y1) / 2) <= 4 * k
            detail2 = f'ink centered at {cx:.0f},{cy:.0f}, the box at {(x0 + x1) / 2:.0f},{(y0 + y1) / 2:.0f}'
        else:
            ok2, detail2 = False, 'no ink in the box'
    results.append((ok2, "the saved picture has the centered box's word in its middle, both ways", detail2))
    report(results)

if scenario == 'textzoom':
    # The Ectolens window zoomed in and out: a box drawn with the real pointer is the size dragged on the
    # picture (the drag over the zoom), and each of its handles, dragged out with the real pointer, moves
    # only its own edges by the drag over the zoom; zoomed in, Ctrl+Z with the Select tool undoes the last.
    import re
    log = open(f'{out}/ectolens.log').read().splitlines()
    steps = open(f'{out}/window-steps.log').read().splitlines()
    zooms = [float(l.split()[1]) for l in log if l.startswith('ECTOLENS-ZOOM ')]
    def parse(t):
        p = t.split()[1:]
        if p[0] == 'none':
            return None
        return {'box': p[0] == 'box=1', 'x': int(p[1]), 'y': int(p[2]), 'w': int(p[3]), 'h': int(p[4]), 'chars': int(p[6].split('=')[1])}
    # the Text mark after each release, and after each step mark
    after_release, cur = {}, None
    after_step, step = {}, None
    for l in log:
        if l.startswith('ECTOLENS-RELEASE '):
            cur = int(l.split()[1])
        elif l.startswith('ECTOLENS-STEP '):
            step, cur = l.split()[1], None
        elif l.startswith('ECTOLENS-TEXT '):
            if cur is not None:
                after_release[cur] = parse(l)
                cur = None
            elif step is not None:
                after_step.setdefault(step, parse(l))
    results = [(len(zooms) == 2 and zooms[0] > 1.01 and zooms[1] < 0.99, 'the window zooms in past 100%, then out under it, with the real keys', str(zooms))]
    passes = [('drag-zbox', 'drag-handles', zooms[0] if zooms else 1), ('drag-zbox~2', 'drag-handles~2', zooms[1] if len(zooms) > 1 else 1)]
    befores = []
    drags = [l.split() for l in steps if l.startswith('hdrag ')]
    counts = [int(l.split()[1]) for l in steps if l.startswith('handles ')]
    results.append((len(counts) == 2 and all(c == 8 for c in counts), 'each box shows its handles, every one found by name', str(counts)))
    per_pass = [drags[:counts[0]] if counts else [], drags[counts[0]:] if counts else []]
    for (box_step, _, z), pdrags in zip(passes, per_pass):
        box = after_step.get(box_step)
        expect_w, expect_h = 200 / z, 70 / z
        results.append((bool(box) and box['box'] and abs(box['w'] - expect_w) <= 2 and abs(box['h'] - expect_h) <= 2,
                        f'at {z * 100:.0f}% a real drag of 200 by 70 draws a box {expect_w:.0f} by {expect_h:.0f} on the picture', str(box)))
        prev = box
        bad = []
        for d in pdrags:
            if d[2] == 'missing' or len(d) < 7:
                bad.append(f'{d[1]} missing')
                continue
            name, edges, dx, dy, rel = d[1], int(d[2]), int(d[3]), int(d[4]), int(d[6])
            now = after_release.get(rel)
            if not prev or not now:
                bad.append(f'{name}: no report')
                prev = now or prev
                continue
            want = dict(prev)
            if edges & 2:
                want['x'] = prev['x'] + dx / z
                want['w'] = prev['w'] - dx / z
            if edges & 4:
                want['w'] = prev['w'] + dx / z
            if edges & 1:
                want['y'] = prev['y'] + dy / z
                want['h'] = prev['h'] - dy / z
            if edges & 8:
                want['h'] = prev['h'] + dy / z
            off = [k for k in ('x', 'y', 'w', 'h') if abs(now[k] - want[k]) > 2]
            if off:
                bad.append(f'{name}: {now} after {prev}, wanted {want}')
            befores.append(prev)
            prev = now
        results.append((len(pdrags) == 8 and not bad, f'at {z * 100:.0f}% each handle dragged with the real pointer moves its own edges by the drag over the zoom',
                        '; '.join(bad) or f'{len(pdrags)} drags'))
        if box_step == 'drag-zbox':
            # zoomed in, still writing in the box: Ctrl+Z takes back the last drag, then the one before it,
            # and Ctrl+Shift+Z puts that one back; the word typed stays through all three
            same = lambda a, b: bool(a) and bool(b) and all(abs(a[k] - b[k]) <= 1 for k in ('x', 'y', 'w', 'h'))
            u1, u2, r1 = after_step.get('undo'), after_step.get('undo~2'), after_step.get('redo')
            b8, b7 = (befores[-1], befores[-2]) if len(befores) >= 2 else (None, None)
            results.append((same(u1, b8) and same(u2, b7) and same(r1, b8),
                            'Ctrl+Z while writing in the box takes back the last handle drag, then the one before; Ctrl+Shift+Z puts it back',
                            f'undo {u1} (wanted {b8}), undo {u2} (wanted {b7}), redo {r1} (wanted {b8})'))
            results.append((all(t and t['chars'] == 4 for t in (u1, u2, r1)), 'the word typed in the box stays through them', str([t and t['chars'] for t in (u1, u2, r1)])))
            after = after_step.get('type-after')
            results.append((bool(after) and after['box'] and after['chars'] == 7, 'keys typed after them, with no click, go into the same box', str(after)))
            befores = []
    # the window's own Align button: a real click opens its menu under the button, inside the window, and a
    # real click on Right makes it the Text tool's (Left and Top before)
    wat = [l.split()[1:] for l in log if l.startswith('ECTOLENS-WAT ') and len(l.split()) == 4]
    button = next(((int(x), int(y)) for n, x, y in wat if n == 'optionAlign'), None)
    items = {n: (int(x), int(y)) for n, x, y in wat if n.startswith('align-')}
    client = next((l.split()[1:] for l in steps if l.startswith('client ')), ['0', '0', '0', '0'])
    cw, ch = int(client[2]), int(client[3])
    below = bool(button) and len(items) == 6 and all(y > button[1] and 0 < x < cw and 0 < y < ch for x, y in items.values())
    results.append((below, "the window's Align button opens its menu under it, all six choices inside the window", f'button {button}, items {items}'))
    tools = [l.split()[-1] for l in log if l.startswith('ECTOLENS-TOOL ')]
    results.append((tools[-1:] == ['align=34'], "a real click on Right in the window's Align menu makes it the Text tool's", str(tools)))
    report(results)

if scenario == 'annotate':
    # The marking options stand to the right of the capture area, vertically,
    # in Ectolens's own colors, and Text and Number marks start as an Ecto green label and badge.
    from PIL import Image as _I
    def shot(n):
        return _I.open(f'{out}/{n}.png').convert('RGB')
    def near(p, q, tol=10):
        return all(abs(a - b) <= tol for a, b in zip(p, q))
    def luma(p):
        return (p[0] * 299 + p[1] * 587 + p[2] * 114) // 1000
    fr = [int(v) for v in (lines('ECTOLENS-FRAME')[-1:] or [['0'] * 4])[0][:4]]
    fx, fy, fw, fh = fr
    at = [(l[0], int(l[1]), int(l[2])) for l in lines('ECTOLENS-AT') if len(l) == 3]
    rails = [(x, y) for n, x, y in at if n == 'optionsRow']
    first = dict((n, (x, y)) for n, x, y in reversed(at))
    results = []
    results.append((len(rails) == 5, 'the options row shows for Text, Number, Rectangle, Blur and Arrow', str(rails)))
    pad = options_pad()
    places = options_places()
    bad = [m for m in (judge_place(pl, pad) for pl in places) if m]
    halves = {pl['tool'][1][0] + pl['tool'][1][2] / 2 > pl['top'][0] + pl['top'][2] / 2 for pl in places if pl['tool'] and pl['tool'][1] and pl['top']}
    results.append((not bad and len(places) == 7 and halves == {True, False}, 'for each tool, on both halves of the tool row, and on a small frame, the options lie above the tool row, lined up with the tool that called them',
                    '; '.join(bad) or f'{len(places)} places'))
    results.append(('optionStrength' in first and 'optionWidth' in first, "Blur's strength and Arrow's width show in it", str([n for n in first if n.startswith('option')])))
    # a small frame: the rows above and below reach past its sides; the rail must clear them
    small_rows, small_rail, cur = [], None, None
    for l in open(f'{out}/ectolens.log').read().splitlines():
        if l.startswith('ECTOLENS-STEP '):
            cur = l.split()[1]
        elif cur == 'small' and l.startswith('ECTOLENS-ROW ') and l.split()[1] != 'optionsRow':
            small_rows.append([int(v) for v in l.split()[2:6]])
        elif cur == 'small' and l.startswith('ECTOLENS-RECT optionsRow '):
            small_rail = [int(v) for v in l.split()[2:6]]
    def overlap(a, b):
        return a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]
    results.append((small_rail is not None and len(small_rows) >= 2 and not any(overlap(small_rail, r) for r in small_rows),
                    'on a small frame (300x120) the options row clears the rows above and below', f'row {small_rail}, rows {small_rows}'))
    bad, seen = judge_sliders()
    ends = {n: v for n, v in seen.items() if {0.0, 1.0} <= v}
    results.append((not bad and len(ends) >= 4, 'every slider and the color square, clicked at their far ends, keeps its thumb inside its track, and a slider in a row has room past its ends',
                    '; '.join(bad) or f'reached both ends: {sorted(ends)} of {sorted(seen)}'))
    maps = open(f'{out}/maps.txt').read().strip() if __import__('os').path.exists(f'{out}/maps.txt') else 'missing'
    results.append((maps == '0', "Plasma's KQuickImageEditor is not loaded in the running Ectolens", f'{maps} mappings'))
    for name, what in (('topRow', 'the row above the frame'), ('bottomRow', 'the row below the frame')):
        x = first.get(name, (-1, -1))[0]
        results.append((abs(x - (fx + fw / 2)) <= 1, f'{what} stays centered on the frame', f'{x} vs {fx + fw / 2}'))
    rail = shot('rail-text')
    text_place = next((pl for pl in places if pl['step'] == 'editing' or (pl['tool'] and pl['tool'][0] == 'textTool' and pl['step'] != 'small')), None)
    rx0, ry0, rw0, rh0 = text_place['row'] if text_place else (0, 0, 0, 0)
    # along the row's top, above its buttons: the row's own surface
    surface = [rail.getpixel((x, ry0 + 2)) for x in range(rx0 + rh0 // 2, rx0 + rw0 - rh0 // 2, 12)]
    results.append((bool(surface) and all(near(p, (17, 17, 19), 6) for p in surface), "the options row is Ectolens's own surface (bg-subtle, #111113)", str(surface[:4])))
    sx, sy = first.get('optionColor', (0, 0))
    results.append((near(rail.getpixel((sx, sy)), (200, 242, 46), 14), "the Text tool's color swatch shows Ecto green", str(rail.getpixel((sx, sy)))))
    menus = [l[0] for l in lines('ECTOLENS-MENUS')]
    results.append((menus[:2] == ['1', '1'], 'a click on the color and on Aa opens a menu in the overlay', str(menus)))
    gx, gy = first.get('markColorc8f22e', (0, 0))
    menu = shot('colormenu')
    results.append((gy > 0 and not (ry0 <= gy <= ry0 + rh0) and near(menu.getpixel((gx, gy)), (200, 242, 46), 14), "the color menu opens off the options row (above it when it fits), Ecto green first",
                    f'swatch at {gx},{gy} = {menu.getpixel((gx, gy)) if gx else None}, the row from {ry0} to {ry0 + rh0}'))
    for n in ('colormenu', 'fontmenu'):
        wins = open(f'{out}/windows-{n}.txt').read().splitlines() if __import__('os').path.exists(f'{out}/windows-{n}.txt') else ['missing']
        ours = [w for w in wins if len(w.split()) > 2 and 'ectolens' in w.split()[2].lower() and ('normal=true' in w or 'dialog=true' in w)]
        results.append((wins != ['missing'] and not ours, f'no KDE dialog window opens for the {n[:-4]} menu', '; '.join(ours) or 'probe missing'))
    try:
        pic = shot('saved')
    except Exception as e:
        pic = None
    results.append((pic is not None and pic.size == (fw, fh), 'the capture was saved, the size of the frame', str(pic.size if pic else 'no saved picture')))
    if pic is not None:
        for i, (dx, dy) in enumerate([(470, 330), (470, 390), (470, 450)]):
            x, y = dx - fx, dy - fy
            fill = pic.getpixel((x, y + 9))
            digit = min((pic.getpixel((x + a, y + b)) for a in range(-5, 6) for b in range(-6, 7)), key=luma)
            results.append((near(fill, (200, 242, 46), 12) and luma(digit) < 60, f'Number {i + 1} is an Ecto green badge with a dark digit', f'fill {fill}, digit {digit}'))
        lx, ly = 700 - fx, 300 - fy
        box = pic.getpixel((lx - 4, ly))
        words = min((pic.getpixel((lx + a, ly + b)) for a in range(0, 100) for b in range(-6, 7)), key=luma)
        results.append((near(box, (200, 242, 46), 12) and luma(words) < 60, 'the Text mark is an Ecto green label with dark words', f'box {box}, words {words}'))
        # a Text click with no words leaves nothing in the picture
        ex, ey = 900 - fx, 640 - fy
        greens = [(a, b) for a in range(ex - 20, ex + 40) for b in range(ey - 14, ey + 14) if near(pic.getpixel((a, b)), (200, 242, 46), 30)]
        results.append((not greens, 'an empty Text mark leaves no box in the picture', f'{len(greens)} green pixels'))
        # chosen with real clicks in the color and font menus: red, then Geist Mono, for the next Text mark
        tools = [l for l in lines('ECTOLENS-TOOL')]
        results.append((len(tools) >= 2 and tools[0][1] == 'fill=#ef4444/255' and tools[0][2] == 'words=#ef4444',
                        'a click on the red swatch makes the Text tool red', str(tools[:1])))
        results.append((len(tools) >= 2 and tools[1][3] == 'family=GeistMono', 'a click on Geist Mono in the font menu sets the font', str(tools[1:2])))
        sx2, sy2 = 700 - fx, 560 - fy
        box2 = pic.getpixel((sx2 - 4, sy2))
        words2 = max((pic.getpixel((sx2 + a, sy2 + b)) for a in range(0, 90) for b in range(-6, 7)), key=luma)
        results.append((near(box2, (239, 68, 68), 12) and luma(words2) > 200, 'the next Text mark is a red label with white words', f'box {box2}, words {words2}'))
    report(results)
if scenario == 'window':
    # Open with › Ectolens checked: Enter opens the window; its menus open with real clicks.
    menus = lines('ECTOLENS-MENUS')
    canvas = lines('ECTOLENS-CANVAS')
    im = Image.open(f'{out}/window.png').convert('RGB')
    # the title bar's icon: Ectolens's own, no red fringe (a window icon handed over by the app showed
    # its lime edge pixels as red, 2026-11-01). The window sits at x 260, y 90.
    box = [im.getpixel((x, y)) for x in range(262, 290) for y in range(92, 118)]
    green = sum(1 for p in box if lime(p))
    red = sum(1 for r, g, b in box if r > 150 and g < 90)
    saved = open(f'{out}/saved.txt').read().split() if __import__('os').path.exists(f'{out}/saved.txt') else []
    results = [
        (len(menus) == 5 and all(m == ['1'] for m in menus), "Save, Copy, zoom, the gear and New Capture each open their menu with a real click", str(menus)),
        (canvas[-1:] == [['370', '230']] if canvas else False, 'dragging a crop and clicking Apply crops the picture to 370×230', str(canvas)),
        (len(saved) == 1, 'Save wrote one file to Screenshots', ' '.join(saved)),
        # a real drag of 240 x 80 on the picture, shown at its own size: a box that size, a word in it
        (bool(lines('ECTOLENS-TEXT')) and lines('ECTOLENS-TEXT')[0][0] == 'box=1' and abs(int(lines('ECTOLENS-TEXT')[0][3]) - 240) <= 3
         and abs(int(lines('ECTOLENS-TEXT')[0][4]) - 80) <= 3 and 'chars=4' in lines('ECTOLENS-TEXT')[0],
         'a drag with the Text tool on the picture draws a text box the size dragged', str(lines('ECTOLENS-TEXT'))),
        (green > 40, f'the title bar shows the Ectolens icon ({green} lime pixels)', ''),
        (red == 0, f'no red fringe on the icon ({red} red pixels)', ''),
    ]
    report(results)
def recording_length(what):
    """the saved file (recording.mp4) lasts as long as the recording ran, from the app's ECTOLENS-REC
    lines: Recording to Rendering. A still picture gives the compositor's stream no new frames, and
    the file came out one frame long (2026-11-03), then, with one frame added at Stop, cut where the
    still stretch began (2026-11-04: H.264's reordering ends an MP4 at a frame's decode time).
    Gives (ok, name, why), as report() takes them."""
    text = open(f'{out}/ectolens.log').read()
    rec = [(p[1], int(p[2])) for p in (l.split() for l in text.splitlines()) if p[:1] == ['ECTOLENS-REC'] and len(p) == 3]
    began = next((t for st, t in rec if st == 'Recording'), None)
    ended = next((t for st, t in rec if st == 'Rendering' and began is not None and t > began), None)
    try:
        length = float(json.loads(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'json', f'{out}/recording.mp4'],
                                                 capture_output=True, text=True).stdout)['format']['duration'])
    except Exception:
        length = 0.0
    ran = (ended - began) / 1000 if began is not None and ended is not None else 0.0
    return (ran > 1 and length >= ran - 0.3, f'the recording of {what} lasts as long as it ran', f'recorded for {ran:.2f} s, the file lasts {length:.2f} s')


if scenario == 'video':
    # After a recording, with Open with › Ectolens checked, the window plays it (design proposal 2, gap 5).
    props = {l.split()[1] + '.' + l.split()[2]: ' '.join(l.split()[3:]) for l in open(f'{out}/ectolens.log').read().splitlines() if l.startswith('ECTOLENS-WPROP ')}
    size = props.get('windowSize.sizeText', '')
    report([
        ('ECTOLENS-SAVED ' in open(f'{out}/ectolens.log').read(), 'the recording was saved', ''),
        (size.split(' · ')[0] in ('799×508', '800×508') and ' · 0:0' in size, "the window's size row shows the size (even, as encoders need) and the length", size),
        (props.get('windowZoom.visible') == 'false', 'zoom is gone for a video', props.get('windowZoom.visible')),
        (props.get('windowExtract.visible') == 'false', 'Extract Text is gone for a video', props.get('windowExtract.visible')),
        recording_length('the frame'),
    ] + [(lambda bad, seen: (not bad and 0.0 in seen.get('scrubber', set()) and max(seen.get('scrubber', {0})) > 0.5,
                             'the scrubber, clicked at its far ends with the real pointer, keeps its thumb inside its track and room past its ends, '
                             'and its far right end goes to the end, not back to the start',
                             '; '.join(bad) or f"seen at {sorted(seen.get('scrubber', set()))}"))(*judge_sliders())])
if scenario == 'empty':
    menus = lines('ECTOLENS-MENUS')
    report([(menus == [['1']], "opened without a capture, New Capture's arrow opens its menu with a real click", str(menus))])
if scenario == 'tools':
    # the tooltip of a button just pressed hides (Hide Private Info's covered its own note),
    # and shows again once the pointer has left and come back: the tooltips anatomy saw at each step
    def tips_at(step):
        seen, now = 0, None
        for l in open(f'{out}/ectolens.log').read().splitlines():
            if l.startswith('ECTOLENS-STEP '):
                now = l.split()[1]
            elif now == step and l.startswith('ECTOLENS-ANATOMY FONT ') and ' tip ' in f' {l} ':
                seen += 1
        return seen
    results_tips = [(tips_at('hidden') == 0, "after a click on Hide Private Info, its tooltip is gone, so its note shows", f'{tips_at("hidden")} tooltip words'),
                    (tips_at('rehover') > 0, 'with the pointer off the button and back, its tooltip shows again', f'{tips_at("rehover")} tooltip words')]
    frames = lines('ECTOLENS-FRAME')
    menus = lines('ECTOLENS-MENUS')
    hidden = lines('ECTOLENS-HIDDEN')
    clip = lines('ECTOLENS-CLIPBOARD')
    kwin = open(f'{out}/kwin.log').read()
    konsole = [l.split()[-2:] for l in kwin.splitlines() if 'ECTOLENS-KONSOLE' in l]
    pin = [l for l in kwin.splitlines() if 'ECTOLENS-WINDOW ectolens' in l]
    n_hidden = int(hidden[-1][1]) if hidden and len(hidden[-1]) > 1 and hidden[-1][1].isdigit() else 0
    results = [
        (frames[:1] and frames[0] == ['400', '196', '799', '508', 'window'], 'a real click on Konsole frames it as a window', str(frames[:1])),
        (menus[:1] == [['1']], "a real click on the frame label opens the size menu", str(menus)),
        (lines('ECTOLENS-RESIZED') == [['1280', '720']], 'picking 1280×720 resizes Konsole through KWin', str(lines('ECTOLENS-RESIZED'))),
        (lines('ECTOLENS-STAYED-ON') == [['yes']], 'the overlay stays on while the window resizes', str(lines('ECTOLENS-STAYED-ON'))),
        (bool(lines('ECTOLENS-RETAKEN')) and lines('ECTOLENS-RETAKEN')[0][0] == '1' and int(lines('ECTOLENS-RETAKEN')[0][1]) < 800,
         "the resized window's picture is in the overlay within 0.8 seconds", str(lines('ECTOLENS-RETAKEN'))),
        ([r[0] for r in lines('ECTOLENS-RETAKEN')] == ['1', '2'], 'and is taken again a second later, so passing hints leave the picture', str(lines('ECTOLENS-RETAKEN'))),
        (len(frames) > 1 and frames[1][2:] == ['1280', '720', 'window'], 'the frame follows the window at 1280×720', str(frames[1:2])),
        (sum(Image.open(f'{out}/resized.png').convert('RGB').getpixel((1500, 860))) < 200,
         "the frozen picture shows the window at its new size (Konsole's dark where the wallpaper was)", str(Image.open(f'{out}/resized.png').convert('RGB').getpixel((1500, 860)))),
        (n_hidden >= 3, 'Hide Private Info blurs the email address, the key and the home folder', str(hidden)),
        (any(c[:1] == ['text'] and len(c) > 1 and c[1].startswith('#') and len(c[1]) == 7 for c in clip), 'Pick Color copies a hex color with a real click', str(clip)),
        (bool(lines('ECTOLENS-AT openQr')), 'Open QR Code appears when the frame holds a QR code', ''),
        ([m[0] for m in lines('ECTOLENS-MODE')][-2:] == ['measure', 'none'], 'Esc leaves Measure (whichever screen has the keyboard)', str(lines('ECTOLENS-MODE'))),
        (menus[1:3] == [['1'], ['1']], "Capture's arrow and the gear open their menus with real clicks", str(menus)),
        (bool(lines('ECTOLENS-PINNED')), 'Pin pins what is framed', ''),
        (bool(pin) and all('normal=false' in l for l in pin), 'the pin floats as a layer surface, not a normal window', '; '.join(pin)),
        (lines('ECTOLENS-REVERTED') == [['799', '508']], 'after the capture Konsole is put back to its old size', str(lines('ECTOLENS-REVERTED'))),
        (konsole[-1:] == [['799', '508']], 'and KWin reports it at 799×508 again', str(konsole)),
    ]
    results += results_tips
    report(results)
if scenario == 'share':
    menus = lines('ECTOLENS-MENUS')
    notes = notifications()
    note = notes[-1] if notes else {}
    clip = lines('ECTOLENS-CLIPBOARD')
    results = [
        (bool(lines('ECTOLENS-SHARE-ASKED')), 'with Share set to ask each time, Enter asks where to share first', ''),
        (bool(lines('ECTOLENS-SHARETARGETS')) and 'clipboardplugin' not in lines('ECTOLENS-SHARETARGETS')[0][0].split(',')
         and len(lines('ECTOLENS-SHARETARGETS')[0][0].split(',')) > 1,
         "Share lists KDE's services, without Copy to clipboard (After Capture has its own row)", str(lines('ECTOLENS-SHARETARGETS'))),
        (menus == [['1']], 'the share services open over Capture', str(menus)),
        (any(c[:1] == ['image'] for c in clip), "Esc skips sharing and the capture still finishes (copied)", str(clip)),
        (note.get('text') == 'Copied to the clipboard.', 'and the notification says so', str(note)),
    ]
    report(results)
if scenario == 'tour':
    sys.exit(0)  # captures for the design review, not a check
if scenario == 'launch':
    act = open(f'{out}/activate.log').read() if __import__('os').path.exists(f'{out}/activate.log') else ''
    print(('PASS  ' if 'activated' in act else 'FAIL  ') + 'the shortcut action reached Ectolens by its D-Bus name' + ('' if 'activated' in act else f' ({act.strip()[-160:]})'))
    if 'activated' not in act:
        sys.exit(1)
X, Y, W, H = 300, 150, 800, 500
fails = 0


def steps_of(text):
    """the test step each log line belongs to (the last ECTOLENS-STEP before it)"""
    step, out = None, []
    for l in text.splitlines():
        if l.startswith('ECTOLENS-STEP '):
            step = l.split()[1]
        out.append(step)
    return out


def check(name, ok, why=''):
    global fails
    # CHECK_WHY=1 prints what each pass measured too
    print(('PASS  ' if ok else 'FAIL  ') + name + (f' ({why})' if not ok or os.environ.get('CHECK_WHY') else ''))
    fails += 0 if ok else 1


def img(name):
    return Image.open(f'{out}/{name}.png').convert('RGB')


def handle_at_corners(im):
    # a handle is a filled disk (radius 7) around each corner; 3 px out from the corner, diagonally, is inside it
    hits = 0
    for cx, cy, dx, dy in ((X, Y, -3, -3), (X + W, Y, 3, -3), (X, Y + H, -3, 3), (X + W, Y + H, 3, 3)):
        hits += 1 if lime(im.getpixel((cx + dx, cy + dy))) else 0
    return hits


def toolbar_width(im):
    # widest run of toolbar-colored pixels in the band under the frame
    best = 0
    for y in range(Y + H + 12, Y + H + 40, 4):
        run = 0
        for x in range(0, im.width):
            p = im.getpixel((x, y))
            run = run + 1 if 20 < sum(p) < 400 else 0
            best = max(best, run)
    return best


def red_dot_below(im):
    n = 0
    for y in range(Y + H + 8, Y + H + 48):
        for x in range(0, im.width):
            r, g, b = im.getpixel((x, y))
            n += 1 if (r > 200 and g < 150 and b < 150 and r - g > 80) else 0  # the record dot, #f87171
    return n


def outlined(im, x, y, w, h):
    # share of sampled points along the rectangle's edges drawn in the highlight color
    pts = [(x + i * w // 20, y) for i in range(1, 20)] + [(x + i * w // 20, y + h - 1) for i in range(1, 20)]
    pts += [(x, y + i * h // 20) for i in range(1, 20)] + [(x + w - 1, y + i * h // 20) for i in range(1, 20)]
    hit = 0
    for px, py in pts:
        best = 0
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if 0 <= px + dx < im.width and 0 <= py + dy < im.height:
                    best = max(best, 1 if lime(im.getpixel((px + dx, py + dy))) else 0)
        hit += best
    return hit / len(pts)


log = open(f'{out}/ectolens.log').read()
if scenario == 'settings':
    settings = img('settings')
    menu = img('menu')
    # the dialog is a large undimmed window over the frozen screen: count bright, undimmed
    # window-colored pixels (Breeze Dark window color ~ (32,35,38)) outside the frame
    def window_pixels(im):
        return sum(1 for x in range(0, im.width, 4) for y in range(0, im.height, 4)
                   if 25 <= im.getpixel((x, y))[0] <= 50 and abs(im.getpixel((x, y))[0] - im.getpixel((x, y))[2]) < 12)
    check('Recording Settings… opens the settings dialog where it can be seen', window_pixels(settings) > window_pixels(menu) + 4000,
          f'{window_pixels(settings) - window_pixels(menu)} new window pixels')
    sys.exit(1 if fails else 0)

if scenario == 'desktops':
    moved = 'ECTOLENS-MOVED' in open(f'{out}/kwin.log').read()
    check('the test window was moved to the second virtual desktop', moved, 'the move script did not run')
    took = [l.split() for l in log.splitlines() if l.startswith('ECTOLENS-TOOK ')]
    kind = took[0][1] if took else 'nothing'
    check("pointing where a window sits on another desktop takes the screen, not that window", kind == 'screen', f'took {kind}')
    hv = img('hover-elsewhere')
    check('and outlines the whole screen', outlined(hv, 0, 0, hv.width, hv.height) > 0.8, f'{outlined(hv, 0, 0, hv.width, hv.height):.0%} of its edge')
    sys.exit(1 if fails else 0)

if scenario == 'input':
    wins = open(f'{out}/windows.txt').read().splitlines() if __import__('os').path.exists(f'{out}/windows.txt') else []
    # lines read 'js: ECTOLENS-WINDOW <resourceClass> normal=.. dialog=..'; judge by the class
    ours = [w for w in wins if len(w.split()) > 2 and 'ectolens' in w.split()[2].lower()]
    check('the Ectolens overlay is among them', len(ours) > 0, 'no Ectolens window listed')
    check('KWin saw the windows while the overlay was up', len(wins) > 0, 'the window probe printed nothing')
    check('no Ectolens overlay is a normal window, so KWin never animates it open', all('normal=true' not in w and 'dialog=true' not in w for w in ours), '; '.join(ours))
    framed = img('framed')
    check('dragging with the real pointer drew the frame', handle_at_corners(framed) == 4, f'{handle_at_corners(framed)} of 4 handles')
    check('the row with Capture and Record sits under the drawn frame', toolbar_width(framed) > 400, f'width {toolbar_width(framed)}')
    menus = [l.split()[1] for l in log.splitlines() if l.startswith('ECTOLENS-MENUS ')]
    check("clicking Record's arrow opens its panel over the overlay", menus[:1] == ['1'], str(menus))
    # every row stays centered on the frame, the recording bar too (it slid to the
    # frame's left corner). The rows are every EctoRow the overlay shows, as the app reports them.
    fr = [int(v) for v in (lines('ECTOLENS-FRAME')[-1:] or [['0'] * 4])[0][:4]]
    mid = fr[0] + fr[2] / 2
    batches, current = {}, None
    for l in log.splitlines():
        if l.startswith('ECTOLENS-STEP '):
            current = l.split()[1]
        elif l.startswith('ECTOLENS-ROW '):
            n, x, y, w, h = l.split()[1:6]
            batches.setdefault(current, []).append((n, int(x), int(y), int(w), int(h)))
    framed_rows, rec_rows = batches.get('framed', []), batches.get('menu', [])
    check('the overlay reported its rows when framed and while recording', len(framed_rows) >= 2 and len(rec_rows) >= 1, f'{framed_rows} / {rec_rows}')
    for n, x, y, w, h in framed_rows + rec_rows:
        if n == 'optionsRow':
            # lined up with the tool that called it instead (judge_place, in the toolclicks and annotate runs)
            continue
        check(f'the row {n} is centered on the frame', abs(x + w / 2 - mid) <= 1, f'its middle at {x + w / 2}, the frame middle at {mid}')
    for n, x, y, w, h in rec_rows:
        check(f'while recording, the row {n} sits outside the recorded area', y >= fr[1] + fr[3] or y + h <= fr[1], f'y {y}..{y + h}, the frame {fr[1]}..{fr[1] + fr[3]}')
    rec = img('recording')
    check('the R key started a recording, with the recording toolbar shown', 80 < toolbar_width(rec) < 400 and red_dot_below(rec) > 20,
          f'width {toolbar_width(rec)}, {red_dot_below(rec)} red pixels')
    check('clicking Stop with the real pointer saved the recording', 'ECTOLENS-SAVED ' in log, 'no ECTOLENS-SAVED line')
    try:
        probe = json.loads(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'stream=width,height', '-of', 'json', f'{out}/recording.mp4'],
                                          capture_output=True, text=True).stdout)['streams'][0]
    except Exception as e:
        probe = {'error': str(e)}
    check('the file holds the dragged frame', abs(probe.get('width', 0) - W) <= 2 and abs(probe.get('height', 0) - H) <= 2, json.dumps(probe))
    # From Stop until the overlay closes it shows nothing: it used to fall back to the frame, with its
    # grips, Record and the gear, for about half a second while the file was finished and saved.
    # Judged on KWin's screenshots taken one after another right after the click (afterstop-N), placed
    # in time by the app's ECTOLENS-REC lines: none may show the frame's grips.
    rec_at = {p[1]: int(p[2]) for p in (l.split() for l in log.splitlines()) if p[:1] == ['ECTOLENS-REC'] and len(p) == 3}
    times = {}
    if os.path.exists(f'{out}/afterstop-times.txt'):
        times = {int(a): int(b) for a, b in (l.split() for l in open(f'{out}/afterstop-times.txt') if l.strip())}
    shots, showing = [], []
    for n, t in sorted(times.items()):
        try:
            im = Image.open(f'{out}/afterstop-{n}.png').convert('RGB')
        except Exception:
            continue
        grips = handle_at_corners(im)
        shots.append((n, t - rec_at.get('Rendering', t), grips))
        if grips > 0:
            showing.append((n, t - rec_at.get('Rendering', t), grips))
    check('screenshots were taken from Stop on, the first within a quarter second of it',
          'Rendering' in rec_at and any(-100 <= dt <= 250 for _, dt, _ in shots) and len(shots) >= 4, f'shot, ms after Stop, grips: {shots}; states {rec_at}')
    check('after Stop the overlay never showed the frame again', not showing, f'shot, ms after Stop, grips seen: {showing}')
    sys.exit(1 if fails else 0)

if scenario in ('agent', 'agentoff'):
    # An AI agent asks through ectolens-mcp (agent-client.py plays the agent, inner.sh the person).
    # Every answer is the agent's view; the pictures it got are read back from what it wrote.
    import os
    path = f'{out}/agent-client.json'
    res = json.load(open(path)) if os.path.exists(path) else {}
    check('the made-up agent finished', os.path.exists(f'{out}/agent-client.done'), open(f'{out}/agent-client.log').read()[-400:] if os.path.exists(f'{out}/agent-client.log') else 'no log')
    def result(name):
        return (res.get(name) or {}).get('result') or {}
    def texts(name):
        return ' '.join(c.get('text', '') for c in result(name).get('content', []) if c.get('type') == 'text')
    def images(name):
        return [c for c in result(name).get('content', []) if c.get('type') == 'image']
    init = result('initialize')
    check('initialize answers in the version the agent asked for, with the tools capability', init.get('protocolVersion') == '2025-06-18' and 'tools' in init.get('capabilities', {}), json.dumps(init)[:300])
    names = [t.get('name') for t in result('tools').get('tools', [])]
    check('tools/list names the four tools', names == ['list_windows', 'capture', 'record_start', 'record_stop'], str(names))
    if scenario == 'agentoff':
        check('with agents off in Settings › Model Context Protocol (MCP), list_windows says agents are off', result('windows').get('isError') and 'off' in texts('windows'), texts('windows'))
        check('and capture says the same, opening nothing', result('capture_off').get('isError') and 'off' in texts('capture_off') and 'ECTOLENS-AGENT asking' not in log, texts('capture_off'))
        sys.exit(1 if fails else 0)
    wins = (result('windows').get('structuredContent') or {}).get('windows', [])
    konsole = [w for w in wins if 'konsole' in (w.get('app', '') + w.get('title', '')).lower()]
    check('list_windows lists the terminal, with its id and size', len(konsole) == 1 and konsole[0].get('id') and konsole[0].get('width', 0) > 0, json.dumps(wins)[:400])
    before = open(f'{out}/before-send.txt').read().strip() if os.path.exists(f'{out}/before-send.txt') else '?'
    check('nothing reached the agent while the overlay asked (its folder was empty before Send)', before == '0', f'{before} files')
    imgs = images('capture_window')
    got = Image.open(imgs[0]['file']) if imgs and os.path.exists(imgs[0].get('file', '')) else None
    k = konsole[0] if konsole else {}
    check('Send gave the agent a picture of the window it named, at the window\'s size', got is not None and abs(got.width - k.get('width', 0)) <= 2 and abs(got.height - k.get('height', 0)) <= 2,
          f'{got.size if got else None} for a {k.get("width")}x{k.get("height")} window')
    words = texts('capture_window')
    check('the words it asked for came too', 'notes.md' in words, words[:300])
    check('with the private things blurred: no email address, key or home folder in the words', all(p not in words for p in ('dana.reyes@example.com', 'sk-proj-9fK2', '/home/dana')), words[:300])
    # the request pops up in the middle of the screen (the bottom row's line was easy to miss)
    cards = []
    step = None
    for l in log.splitlines():
        if l.startswith('ECTOLENS-STEP '):
            step = l.split()[1]
        elif l.startswith('ECTOLENS-RECT agentCard '):
            cards.append((step, [int(v) for v in l.split()[2:6]]))
    shown = [(st, r) for st, r in cards if r[2] > 0 and st in ('agent1', 'agent2')]
    check('each request opened a dialog in the middle of the screen', len(shown) == 2 and all(abs(r[0] + r[2] / 2 - 800) <= 1 and abs(r[1] + r[3] / 2 - 450) <= 1 for _, r in shown), str(cards))
    check("Adjust First closed the dialog (the bottom row's Record then started the recording)", any(st == 'adjust4' and r[2] == 0 for st, r in cards) or not any(st == 'adjust4' for st, _ in cards), str(cards))
    # The screen dims while an agent asks, and the dialog is a raised gray, not black (the screen dims
    # more when an agent is making the call). Judged on KWin's screenshots: the dialog's (consent.png) against the
    # same frame after Adjust First (consent-record.png), where the dim has lifted.
    at, tool_after, nudges, step = {}, None, None, None
    for l in log.splitlines():
        p = l.split()
        if p[:1] == ['ECTOLENS-STEP']:
            step = p[1]
        elif p[:1] == ['ECTOLENS-AT'] and len(p) == 4 and p[2].lstrip('-').isdigit():
            at[(step, p[1])] = (int(p[2]), int(p[3]))
        elif p[:1] == ['ECTOLENS-TOOL'] and step == 'dimclick2':
            tool_after = p[1]
        elif p[:3] == ['ECTOLENS-PROP', 'agentCard', 'nudges'] and step == 'dimclick2':
            nudges = p[3]
    card1 = next((r for st, r in cards if st == 'agent1' and r[2] > 0), None)
    card2 = next((r for st, r in cards if st == 'agent2' and r[2] > 0), None)
    def inside(pt, r, pad=0):
        return pt is not None and r is not None and r[0] - pad <= pt[0] < r[0] + r[2] + pad and r[1] - pad <= pt[1] < r[1] + r[3] + pad
    try:
        asked, adjusted = Image.open(f'{out}/consent.png').convert('RGB'), Image.open(f'{out}/consent-record.png').convert('RGB')
    except Exception as e:
        asked = adjusted = None
        check('the dialog and the frame after Adjust First were both shot', False, str(e))
    if asked and adjusted and card1 and k.get('width'):
        sc = asked.width / 1600
        fx, fy, fw, fh = k['x'], k['y'], k['width'], k['height']
        def mean(im, x, y, w, h, lum=True):
            # a box filter down to one pixel is the patch's average color
            r, g, b = im.crop((round(x * sc), round(y * sc), round((x + w) * sc), round((y + h) * sc))).resize((1, 1), Image.BOX).getpixel((0, 0))
            return (r, g, b) if not lum else 0.2126 * r + 0.7152 * g + 0.0722 * b
        def clear_of_dialog(x, y, w, h):
            cx, cy, cw, ch = card1[0] - 80, card1[1] - 80, card1[2] + 160, card1[3] + 160
            return x + w <= cx or x >= cx + cw or y + h <= cy or y >= cy + ch
        # around the frame: the screen's patches away from the frame, the rows above and below it, and the dialog
        out_p = [(x, y, 40, 40) for x in range(10, 1560, 80) for y in range(10, 860, 80)
                 if clear_of_dialog(x, y, 40, 40) and not (fy - 70 <= y + 40 and y <= fy + fh + 70)]
        # inside the frame: its lower part, where the terminal is empty, left of the dialog
        in_p = [(x, y, 30, 30) for x in range(fx + 20, fx + fw - 50, 60) for y in range(round(fy + fh * 0.6), fy + fh - 50, 40)
                if clear_of_dialog(x, y, 30, 30)]
        def ratio(ps):
            a = sum(mean(asked, *p) for p in ps)
            b = sum(mean(adjusted, *p) for p in ps)
            return a / b if b else 0
        r_out, r_in = ratio(out_p), ratio(in_p)
        check('while the dialog asked, the screen around the frame was dimmed to half again (and Adjust First lifted it)',
              len(out_p) >= 8 and 0.38 <= r_out <= 0.62, f'{len(out_p)} patches, asking/adjusting brightness {r_out:.2f}')
        # the frame itself stays bright (the captured window is not
        # darkened by the shadow): the window being asked about is what the person judges
        check('the frame stayed bright while the screen around it dimmed', len(in_p) >= 3 and 0.92 <= r_in <= 1.08,
              f'{len(in_p)} patches, asking/adjusting brightness {r_in:.2f}')
        # the frame line, just outside the frame, stays bright with it (its top edge, left of the dialog)
        line_p = [(x, fy - 1, 4, 1) for x in range(fx + 20, fx + fw - 20, 40) if clear_of_dialog(x, fy - 1, 4, 1)]
        r_line = ratio(line_p)
        check('the frame line stayed bright with the frame', len(line_p) >= 3 and 0.9 <= r_line <= 1.1, f'{len(line_p)} patches, asking/adjusting brightness {r_line:.2f}')
        handles = {st: l.split()[3] for l, st in zip(log.splitlines(), steps_of(log)) if l.startswith('ECTOLENS-PROP frameHandles visible ')}
        # the test's prop command finds only visible items, so no line in agent1 is the grips hidden;
        # the adjust5 line proves the name still finds them
        check('the frame shows no grips while the dialog asks, and has them back after Adjust First',
              handles.get('agent1', 'hidden') in ('hidden', 'false') and handles.get('adjust5') == 'true', str(handles))
        surface = mean(asked, card1[0] + 24, card1[1] + card1[3] - 40, 12, 12, lum=False)
        check("the dialog is the family's lift gray (#2a2a2e), not black", all(abs(c - v) <= 4 for c, v in zip(surface, (0x2a, 0x2a, 0x2e))), str(surface))
        send, adjust = at.get(('agent1', 'agentCardSend')), at.get(('agent1', 'agentCardAdjust'))
        send_fill = mean(asked, send[0] - 2, send[1] - 11, 4, 2, lum=False) if send else None
        adjust_fill = mean(asked, adjust[0] - 2, adjust[1] - 11, 4, 2, lum=False) if adjust else None
        check('Send is the filled default (white); Adjust First is outlined, the dialog showing through',
              send_fill is not None and all(c >= 240 for c in send_fill) and adjust_fill is not None and all(abs(c - v) <= 6 for c, v in zip(adjust_fill, (0x2a, 0x2a, 0x2e))),
              f'Send {send_fill}, Adjust First {adjust_fill}')
    # the frame's keys wait while the dialog asks (only Enter and Esc answer it)
    frames_at = {}
    step = None
    for l in log.splitlines():
        p = l.split()
        if p[:1] == ['ECTOLENS-STEP']:
            step = p[1]
        elif p[:1] == ['ECTOLENS-FRAME'] and step:
            frames_at[step] = tuple(int(v) for v in p[1:5])
    check('an arrow key while the dialog asked left the frame where its preview showed it',
          'dimclick2' in frames_at and frames_at.get('dimkey2') == frames_at['dimclick2'], f"{frames_at.get('dimclick2')} -> {frames_at.get('dimkey2')}")
    # a recording is made from the live screen, which the overlay's blurs never reach: its dialog
    # says so, and nothing is read for it
    note4 = next((l.split(None, 3)[3] for l, st in zip(log.splitlines(), steps_of(log)) if st == 'agent4' and l.startswith('ECTOLENS-PROP agentCardNote text ')), '')
    # each request from its start ("asking", before its framing and any reading) to the next one's
    blocks = log.split('ECTOLENS-AGENT asking')
    read4 = blocks[4].count('ECTOLENS-HIDING') if len(blocks) > 4 else None
    check("a recording's dialog says recordings are not blurred, and no blur was made for it", 'not blurred' in note4 and read4 == 0, f'note {note4!r}, readings {read4}')
    # Adjust First, then the frame moved over private words and sent: the new part is read and
    # blurred before the picture goes
    f5a, f5b = frames_at.get('adjust5'), frames_at.get('drag5')
    w5 = texts('capture_adjusted')
    check('after Adjust First the frame was dragged up over the private lines', f5a is not None and f5b is not None and f5b[1] <= f5a[1] - 80, f'{f5a} -> {f5b}')
    # judged on the picture the agent got: the frame now holds the email line, the "$ echo" line and
    # the key line whole. Each whole line of text is a run of rows with edges in them (a line cut by
    # the frame's top or bottom is left out); a blurred line keeps only its blur box's soft edge.
    def line_edges(name):
        lines = []
        img = images(name)
        if img and os.path.exists(img[0].get('file', '')):
            g = Image.open(img[0]['file']).convert('L')
            px = g.load()
            edge = [max(abs(px[x + 1, y] - px[x, y]) for x in range(g.width - 1)) for y in range(g.height)]
            run = []
            for y, e in enumerate(edge + [0]):
                if e >= 40:
                    run.append(y)
                elif run:
                    if run[0] > 0 and run[-1] < g.height - 1:
                        lines.append(sorted(edge[run[0]:run[-1] + 1])[len(run) // 2])
                    run = []
        return lines
    lines5 = line_edges('capture_adjusted')
    check('and Send read the moved frame before it went: in the picture the agent got, the email and key lines are blurred and the line between them sharp',
          'OPENAI' in w5 and 'ECTOLENS-AGENT reading' in log and len(lines5) == 3 and lines5[0] <= 90 and lines5[1] >= 120 and lines5[2] <= 90
          and all(p not in w5 for p in ('dana.reyes@example.com', 'sk-proj-9fK2', '/home/dana')),
          f'whole lines\' edge strength {lines5}; words {w5[:200]!r}')
    # the dialog's Blur private information: checked to start, as Settings
    # says; unchecked with a real click, the email and key lines went as they are, words too
    def prop_at(step, obj, name):
        return next((l.split(None, 3)[3] for l, st in zip(log.splitlines(), steps_of(log))
                     if st == step and l.startswith(f'ECTOLENS-PROP {obj} {name} ')), None)
    w6, lines6 = texts('capture_unblurred'), line_edges('capture_unblurred')
    check("a picture's dialog starts with Blur private information checked, as Settings says, and a real click unchecks it",
          prop_at('agent6', 'agentCardBlur', 'checked') == 'true' and prop_at('unblur6', 'agentCardBlur', 'checked') == 'false'
          and 'as they are' in (prop_at('unblur6', 'agentCardBlurNote', 'text') or ''),
          f"checked {prop_at('agent6', 'agentCardBlur', 'checked')} then {prop_at('unblur6', 'agentCardBlur', 'checked')}, "
          f"note {prop_at('agent6', 'agentCardBlurNote', 'text')!r} then {prop_at('unblur6', 'agentCardBlurNote', 'text')!r}")
    check('and unchecked, the agent got the email and key lines as they are: sharp in the picture, whole in the words',
          'ECTOLENS-AGENT blur off' in log and len(lines6) == 3 and min(lines6) >= 120
          and 'reyes@example.com' in w6 and 'sk-proj' in w6 and '[hidden]' not in w6,
          f'whole lines\' edge strength {lines6}; words {w6[:200]!r}')
    w7, lines7 = texts('capture_reblurred'), line_edges('capture_reblurred')
    check('the next request starts checked again, and unchecked then checked again, its picture went blurred',
          prop_at('agent7', 'agentCardBlur', 'checked') == 'true' and prop_at('reblur7', 'agentCardBlur', 'checked') == 'true'
          and log.count('ECTOLENS-AGENT blur on') >= 1
          and len(lines7) == 3 and lines7[0] <= 90 and lines7[1] >= 120 and lines7[2] <= 90
          and all(p not in w7 for p in ('dana.reyes@example.com', 'sk-proj-9fK2', 'reyes@example')),
          f"checked {prop_at('agent7', 'agentCardBlur', 'checked')} then {prop_at('reblur7', 'agentCardBlur', 'checked')}; "
          f'whole lines\' edge strength {lines7}; words {w7[:200]!r}')
    tool_at = at.get(('agent2', 'numberTool'))
    check('a click on a dimmed tool while the dialog asked reached nothing under the dim, and nudged the dialog',
          tool_at is not None and card2 is not None and not inside(tool_at, card2) and tool_after == 'NoTool' and nudges == '1'
          and any(st == 'dimclick2' and r[2] > 0 for st, r in cards),
          f'click at {tool_at} (dialog {card2}), tool after {tool_after}, nudges {nudges}')
    check("Don't Send told the agent no", result('capture_refused').get('isError') and 'chose not to send' in texts('capture_refused'), texts('capture_refused'))
    t3 = texts('capture_text')
    check('Enter sent the screen, as words only', not images('capture_text') and 'notes.md' in t3 and 'dana.reyes' not in t3, t3[:300])
    disc = result('discover')
    check('server/discover answers the 2026-07-28 era', '2026-07-28' in disc.get('supportedVersions', []) and disc.get('resultType') == 'complete', json.dumps(disc)[:300])
    check('a call naming 2026-07-28 in its _meta works', result('windows_modern').get('resultType') == 'complete' and not result('windows_modern').get('isError'), json.dumps(result('windows_modern'))[:200])
    check('record_start began once the person pressed Record', 'Recording started' in texts('record_start'), texts('record_start'))
    stop = texts('record_stop')
    saved = stop.split(' at ')[-1].strip() if ' at ' in stop else ''
    try:
        probe = json.loads(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'stream=codec_name,width,height,nb_frames', '-of', 'json', f'{out}/recording.mp4'],
                                          capture_output=True, text=True).stdout)['streams'][0]
    except Exception as e:
        probe = {'error': str(e)}
    check('record_stop gave the saved file: a recording of the window, at its size', saved.endswith('.mp4') and abs(probe.get('width', 0) - k.get('width', 0)) <= 2 and abs(probe.get('height', 0) - k.get('height', 0)) <= 2,
          f'{stop} {json.dumps(probe)}')
    ok, name, why = recording_length('the still window')
    check(name, ok, why)
    sys.exit(1 if fails else 0)

if scenario == 'frameundo':
    # Undo and Redo, with the real keys and the toolbar's button, step back and forth through the frame's
    # moves and resizes and the marks, in the order they happened. The states come
    # from the app's own reports after each step: the frame (framerect) and both histories (framehistory).
    states, step = {}, None
    for l in log.splitlines():
        p = l.split()
        if p[:1] == ['ECTOLENS-STEP']:
            step = 'framed' if p[1] == 'overlay' else p[1]
        elif p[:1] == ['ECTOLENS-FRAME'] and step:
            states.setdefault(step, {})['rect'] = tuple(int(v) for v in p[1:5])
        elif p[:1] == ['ECTOLENS-FRAMEHISTORY'] and step:
            states.setdefault(step, {})['h'] = tuple(int(v) for v in p[1:7])
    def rect(n):
        return states.get(n, {}).get('rect')
    def marks(n):
        return (states.get(n, {}).get('h') or (0,) * 6)[4]
    def hist(n):
        return states.get(n, {}).get('h')
    R0, R1, R2, R3 = rect('framed'), rect('move'), rect('resize'), rect('nudge')
    check('every step was reported', all(n in states for n in ['framed', 'move', 'resize', 'nudge', 'undo1', 'redo4', 'newmove', 'click-undoButton']), sorted(states))
    check('framing for the first time is no step to undo', hist('framed') is not None and hist('framed')[0] == 0 and hist('framed')[2] == 0, str(hist('framed')))
    check('dragging the frame with the real pointer moved it, keeping its size', R1 is not None and R0 is not None and R1[:2] != R0[:2] and R1[2:] == R0[2:], f'{R0} -> {R1}')
    check('pulling its corner handle resized it', R2 is not None and R1 is not None and R2[2:] != R1[2:], f'{R1} -> {R2}')
    check('the arrow keys moved it, one step for the run of presses', R3 is not None and R2 is not None and R3[:2] != R2[:2] and hist('nudge') is not None and hist('nudge')[0] == 3, f'{R2} -> {R3}, {hist("nudge")}')
    check('the Number click made a mark', marks('nudge') == 1, str(hist('nudge')))
    want = [('undo1', R2, 1, 'Ctrl+Z takes back the arrow keys first'),
            ('undo2', R2, 0, 'then the mark, made after the resize'),
            ('undo3', R1, 0, 'then the resize'),
            ('undo4', R0, 0, 'then the move, back to the frame as first framed'),
            ('undo5', R0, 0, 'with nothing left, Ctrl+Z changes nothing'),
            ('redo1', R1, 0, 'Ctrl+Shift+Z puts the move back'),
            ('redo2', R2, 0, 'then the resize'),
            ('redo3', R2, 1, 'then the mark'),
            ('redo4', R3, 1, 'then the arrow keys'),
            ('undo6', R2, 1, 'undone again: the arrow keys'),
            ('undo7', R2, 0, 'and the mark')]
    for n, r, m, words in want:
        check(f'{words} (frame {r}, {m} marks)', rect(n) == r and marks(n) == m, f'frame {rect(n)}, {marks(n)} marks')
    check('Ctrl+Z with nothing left leaves nothing to undo', hist('undo5') is not None and hist('undo5')[2] == 0, str(hist('undo5')))
    R4 = rect('newmove')
    check('a new move after undoing leaves nothing to redo', R4 is not None and R4 != R2 and hist('newmove') is not None and hist('newmove')[3] == 0, f'{R4}, {hist("newmove")}')
    check('so Ctrl+Shift+Z then changes nothing', rect('redo5') == R4 and marks('redo5') == 0, f'{rect("redo5")}, {marks("redo5")} marks')
    check("the toolbar's Undo button, clicked with the real pointer, takes back the new move", rect('click-undoButton') == R2, f'{rect("click-undoButton")}, wanted {R2}')
    sys.exit(1 if fails else 0)

if scenario == 'soundtracks':
    # Recording Settings › Sound with system audio, the microphone and separate tracks on: the app
    # hands the choice to the recorder, and the saved file holds the mix (default), then each source
    # alone, told apart by the stand-ins' tones (standin-sound.sh, soundtracks.py)
    check('Stop saved the recording', 'ECTOLENS-SAVED ' in log, 'no ECTOLENS-SAVED line')
    ok, _, why = recording_length('the framed area with sound')
    rec = [(p[1], int(p[2])) for p in (l.split() for l in log.splitlines()) if p[:1] == ['ECTOLENS-REC'] and len(p) == 3]
    began = next((t for st, t in rec if st == 'Recording'), None)
    ended = next((t for st, t in rec if st == 'Rendering' and began is not None and t > began), None)
    ran = (ended - began) / 1000 if began is not None and ended is not None else 0
    judged = subprocess.run(['python3', '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'soundtracks.py'),
                             f'{out}/recording.mp4', 'separate', f'{ran:.2f}'], capture_output=True, text=True)
    for line in judged.stdout.splitlines():
        if line.startswith('PASS  '):
            check(line[6:], True, '')
        elif line.startswith('FAIL  '):
            name, _, why = line[6:].rpartition(' (')
            check(name, False, why.rstrip(')'))
    check('the sound judge ran', judged.returncode in (0, 1) and 'sound track' in judged.stdout, judged.stderr[-300:])
    sys.exit(1 if fails else 0)

if scenario == 'fullrecord':
    # A frame filling the screen leaves the recording bar no room outside it: it sits inside the frame,
    # on screen, and KWin leaves Ectolens's own windows out of its recording (on
    # screen, in the frame, but not captured by it). Judged from KWin's screenshot and the file.
    rects = lines('ECTOLENS-RECT')
    bar = next(([int(v) for v in r[1:5]] for r in rects if r and r[0] == 'recordingBar'), None)
    check('the recording bar showed while recording a frame that fills the screen', bar is not None and bar[2] > 0, str(rects))
    check('Stop saved the recording', 'ECTOLENS-SAVED ' in log, 'no ECTOLENS-SAVED line')
    if bar:
        bx, by, bw, bh = bar
        check('the bar sits inside the frame, the whole screen', bx >= 0 and by >= 0 and bx + bw <= 1600 and by + bh <= 900, str(bar))
        rec = img('recording')
        still = f'{out}/recording-frame.png'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', '1.2', '-i', f'{out}/recording.mp4', '-frames:v', '1', still])
        try:
            vid = Image.open(still).convert('RGB')
        except Exception:
            vid = None
        check('the recording holds the whole screen', vid is not None and vid.size == (1600, 900), str(vid.size if vid else 'no picture'))
        if vid is not None and vid.size == (1600, 900):
            from PIL import ImageChops, ImageStat
            def mean(im, box):
                return ImageStat.Stat(im.crop(box).convert('L')).mean[0]
            def diff(a, b, box):
                return ImageStat.Stat(ImageChops.difference(a.crop(box).convert('L'), b.crop(box).convert('L'))).mean[0]
            inner = (bx + 6, by + 6, bx + bw - 6, by + bh - 6)
            above = (bx, max(0, by - 110), bx + bw, max(1, by - 40))
            check('on screen, the bar is drawn there', mean(rec, inner) < 50, f'mean {mean(rec, inner):.0f}')
            check('in the recording, the bar is not there', diff(rec, vid, inner) > 25, f'differs by {diff(rec, vid, inner):.1f}')
            check('just above the bar, the screen and the recording agree (the two are lined up)', diff(rec, vid, above) < 12, f'differs by {diff(rec, vid, above):.1f}')
    sys.exit(1 if fails else 0)

if scenario == 'pick':
    took = [l.split() for l in log.splitlines() if l.startswith('ECTOLENS-TOOK ')]
    kind, rx, ry, rw, rh = (took[0][1:] + ['0'] * 5)[:5] if took else ('nothing', 0, 0, 0, 0)
    rx, ry, rw, rh = int(rx), int(ry), int(rw), int(rh)
    check('a click on the window takes that window', kind == 'window' and rw > 0, f'took {kind}')
    hw, hs = img('hover-window'), img('hover-screen')
    check('pointing at the window outlines exactly the window', rw > 0 and outlined(hw, rx, ry, rw, rh) > 0.8, f'{outlined(hw, rx, ry, rw, rh):.0%} of its edge')
    check('pointing at the desktop outlines the whole screen', outlined(hs, 0, 0, hs.width, hs.height) > 0.8, f'{outlined(hs, 0, 0, hs.width, hs.height):.0%} of its edge')
    check('Ectolens reported the recording saved', 'ECTOLENS-SAVED ' in log, 'no ECTOLENS-SAVED line')
    try:
        probe = json.loads(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'stream=codec_name,width,height', '-of', 'json', f'{out}/recording.mp4'],
                                          capture_output=True, text=True).stdout)['streams'][0]
    except Exception as e:
        probe = {'error': str(e)}
    # a window recording is the window's own buffer; allow for odd sizes rounded to even
    check('the recording is the window', abs(probe.get('width', 0) - rw) <= 1 and abs(probe.get('height', 0) - rh) <= 1, f'{json.dumps(probe)} for a {rw}x{rh} window')
    sys.exit(1 if fails else 0)

framed, recording, paused = img('framed'), img('recording'), img('paused')
check('the framed overlay shows the selection handles', handle_at_corners(framed) == 4, f'{handle_at_corners(framed)} of 4')
check('the row with Capture and Record sits under the frame', toolbar_width(framed) > 400, f'width {toolbar_width(framed)}')
check('while recording the handles are hidden', handle_at_corners(recording) == 0, f'{handle_at_corners(recording)} handles')
check('while recording the picture toolbar is replaced by the recording toolbar', 80 < toolbar_width(recording) < 400, f'width {toolbar_width(recording)}')
check('the recording toolbar shows the red recording dot', red_dot_below(recording) > 20, f'{red_dot_below(recording)} red pixels')
check('while paused the recording toolbar stays, without the red dot', 80 < toolbar_width(paused) < 450 and red_dot_below(paused) < 10,
      f'width {toolbar_width(paused)}, {red_dot_below(paused)} red pixels')
check('Ectolens reported the recording saved', 'ECTOLENS-SAVED ' in log, 'no ECTOLENS-SAVED line')
try:
    probe = json.loads(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'stream=codec_name,width,height', '-of', 'json', f'{out}/recording.mp4'],
                                      capture_output=True, text=True).stdout)['streams'][0]
except Exception as e:
    probe = {'error': str(e)}
check('the file holds exactly the framed area', probe.get('width') == W and probe.get('height') == H, json.dumps(probe))
sys.exit(1 if fails else 0)
