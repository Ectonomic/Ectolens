# Holds every row and bar Ectolens reports to one set of spacing rules (spacing, padding, margin and alignment
# are equalized and standardized). The app finds them itself (the `anatomy` test command, src/TestScript.cpp): rows by their
# type (EctoRow), the Ectolens window's bars by `ectoBar`, the parts in them by what they are built on.
# The sizes come from the rows and bars themselves, never from numbers typed here, so a new row or
# part is held to the rules without anyone adding it.
#
#  1. A part stands as far below its row's top as above its bottom (words to a whole pixel).
#  2. A row's first and last parts are controls (a button, swatch, label, slider, key), each as far
#     from the row's end as from its top and bottom: concentric with the round end. Words, an icon, a
#     color, a separator or an empty item at a row's end is a fault (words stand in an EctoLabel).
#  3. Neighbors in a row stand one gap apart, the same in every row; a split button's arrow overlaps
#     its button by the same amount everywhere.
#  4. A row that follows its tool (its options, Crop's row) lines its first or last control up with
#     the tool's button, edge to edge, unless it is kept inside the screen or the window; and stands
#     the same distance from the tool's row in every report.
#  5. The window's bars: the controls at their sides stand the same distance from the side, left and
#     right, in every bar; the first words, or a notice's dot, of every bar start in one column.
#
# The open menus (EctoMenu, by type; the `anatomy` test command reports each line's surface and marks):
#  6. Every line's surface (an item's hover, a field, a well, a separator's line) stands as far from the
#     menu's left as from its right, the same in every menu, and a first or last line's surface as far
#     from the menu's top or bottom; a surface's corners are concentric with the menu's.
#  7. Every line starts where every other line starts and ends its end (a key, a reason, an arrow, a
#     choice, a stepper) where every other line ends its end, as far from the menu's right as its start
#     from the left; a form's labels end in one column and their choices start in one; nothing a line
#     holds stands outside it.
#  8. In a menu, the words after a line's first mark (a tick, an icon, a check's box) start in one column.
#  9. A line's marks stand centered up and down in it, or in the field or well they are in.
# 10. A segmented control's choices stand as far from its sides as from its top and bottom, with
#     corners concentric with its own.
# 11. Every word in the overlay and the window (rows, bars, menus, tooltips) is in the look's two faces
#     (EctoTheme.sans and .mono), but for words that say they are drawn in a face of their own (the
#     font menu's names); the picture's own marks are the person's and are left out.
#
# usage: anatomy.py OUT_DIR   (prints PASS/FAIL lines; exit 1 on any failure; nothing if not asked for)
import collections, os, sys

out = sys.argv[1]
TOL = 0.5
CONTROLS = {'EctoButton', 'EctoSwatch', 'FrameLabel', 'EctoSlider', 'EctoLabel', 'EctoKey'}

log = open(f'{out}/ectolens.log').read().splitlines()
script = open(f'{out}/script.txt').read() if os.path.exists(f'{out}/script.txt') else ''
asked = [c for c in script.strip().split(';') if c == 'anatomy']

# the reports, each with the step (the last mark) it came at
reports, step, cur, screens = [], '-', None, []
for l in log:
    p = l.split()
    if p[:1] == ['ECTOLENS-STEP'] and len(p) > 1:
        step = p[1]
    elif p[:1] == ['ECTOLENS-SCREEN'] and len(p) >= 5:
        screens.append([float(v) for v in p[1:5]])
    elif p[:2] == ['ECTOLENS-ANATOMY', 'BEGIN']:
        cur = {'step': step, 'rows': {}, 'parts': collections.defaultdict(list), 'tools': {}, 'windows': {},
               'menus': {}, 'lines': collections.defaultdict(dict), 'surfaces': collections.defaultdict(list),
               'marks': collections.defaultdict(list), 'choices': [], 'fonts': [], 'look': None}
        reports.append(cur)
    elif p[:1] == ['ECTOLENS-ANATOMY'] and cur is not None:
        if p[1] == 'ROW':
            win, rid, kind, name = p[2], p[3], p[4], p[5]
            x, y, w, h = (float(v) for v in p[6:10])
            cur['rows'][(win, rid)] = {'win': win, 'kind': kind, 'name': name, 'x': x, 'y': y, 'w': w, 'h': h, 'follows': p[10] == '1'}
        elif p[1] == 'PART':
            win, rid, kind, name = p[2], p[3], p[4], p[5]
            x, y, w, h, ix, iw, gx, gw = (float(v) for v in p[6:14])
            cur['parts'][(win, rid)].append({'kind': kind, 'name': name, 'x': x, 'y': y, 'w': w, 'h': h, 'ix': ix, 'iw': iw, 'gx': gx, 'gw': gw})
        elif p[1] == 'TOOL':
            x, y, w, h, rx, rw = (float(v) for v in p[3:9])
            cur['tools'][p[2]] = {'x': x, 'y': y, 'w': w, 'h': h, 'rx': rx, 'rw': rw}
        elif p[1] == 'WINDOW':
            cur['windows'][p[2]] = (float(p[3]), float(p[4]))
        elif p[1] == 'MENU':
            win, mid, kind, name = p[2], p[3], p[4], p[5]
            x, y, w, h, radius = (float(v) for v in p[6:11])
            cur['menus'][(win, mid)] = {'win': win, 'kind': kind, 'name': name, 'x': x, 'y': y, 'w': w, 'h': h, 'r': radius}
        elif p[1] == 'LINE':
            win, mid, lid, kind, name = p[2], p[3], int(p[4]), p[5], p[6]
            x, y, w, h = (float(v) for v in p[7:11])
            cur['lines'][(win, mid)][lid] = {'kind': kind, 'name': name, 'x': x, 'y': y, 'w': w, 'h': h}
        elif p[1] == 'SURFACE':
            win, mid, lid = p[2], p[3], int(p[4])
            x, y, w, h, radius = (float(v) for v in p[5:10])
            cur['surfaces'][(win, mid)].append({'line': lid, 'x': x, 'y': y, 'w': w, 'h': h, 'r': radius})
        elif p[1] == 'MARK':
            win, mid, lid, kind, name = p[2], p[3], int(p[4]), p[5], p[6]
            x, y, w, h = (float(v) for v in p[7:11])
            cur['marks'][(win, mid)].append({'line': lid, 'kind': kind, 'name': name, 'x': x, 'y': y, 'w': w, 'h': h, 'align': int(p[11])})
        elif p[1] == 'CHOICES':
            win, mid = p[2], p[3]
            x, y, w, h, radius, fx, lr, top, bottom, inner = (float(v) for v in p[4:14])
            cur['choices'].append({'win': win, 'menu': mid, 'x': x, 'y': y, 'w': w, 'h': h, 'r': radius, 'first': fx, 'last': lr,
                                   'top': top, 'bottom': bottom, 'inner': inner})
        elif p[1] == 'FONT':
            cur['fonts'].append({'win': p[2], 'family': p[3], 'own': p[4] == '1', 'in': p[5], 'name': p[6]})
        elif p[1] == 'LOOK':
            cur['look'] = (p[2], p[3])

if not asked and not reports:
    sys.exit(0)

results = []
def check(ok, name, why=''):
    results.append((ok, name, why))

check(len(reports) == len(asked) and all(r['rows'] for r in reports),
      'the app answered every anatomy request with its rows', f'{len(reports)} reports, {sum(1 for r in reports if r["rows"])} with rows, of {len(asked)} asked')

def label(r, row, part=None):
    n = row['name'] if row['name'] != '-' else f"a {row['kind']}"
    return f"{r['step']}: {row['win']} {n}" + (f" {part['kind']} {part['name'] if part['name'] != '-' else ''}".rstrip() if part else '')

def is_words(part):
    # a control with words in it, not an icon alone
    return part['kind'] in ('FrameLabel', 'EctoLabel') or (part['kind'] == 'EctoButton' and part['iw'] > part['h'] * 0.6 and part['w'] > part['h'])

centered, ends, gaps, follow_aligned, follow_gaps = [], [], collections.Counter(), [], collections.Counter()
bar_sides, bar_columns = [], []
gap_where = collections.defaultdict(list)
for r in reports:
    for key, row in r['rows'].items():
        parts = sorted(r['parts'][key], key=lambda q: q['x'])
        if not parts:
            continue
        # 1. centered up and down
        for q in parts:
            top, bottom = q['y'] - row['y'], row['y'] + row['h'] - q['y'] - q['h']
            # words' boxes center to a whole pixel (an odd difference splits 10 and 11): a pixel's rounding
            if abs(top - bottom) > (1 + 0.01 if q['kind'] == 'Text' else TOL):
                centered.append(f"{label(r, row, q)} stands {top:.1f} below the top, {bottom:.1f} above the bottom")
        if row['kind'] == 'row':
            # 2. concentric ends
            for q, side in ((parts[0], 'start'), (parts[-1], 'end')):
                if q['kind'] not in CONTROLS:
                    ends.append(f"{label(r, row, q)} at the row's {side}: words, an icon or a color there stand in an EctoLabel")
                    continue
                inset = (row['h'] - q['h']) / 2
                gap = q['x'] - row['x'] if side == 'start' else row['x'] + row['w'] - q['x'] - q['w']
                if abs(gap - inset) > TOL:
                    ends.append(f"{label(r, row, q)} stands {gap:.2f} from the row's {side}, {inset:.2f} from its top and bottom")
            # 3. neighbors one gap apart
            for a, b in zip(parts, parts[1:]):
                g = round(b['x'] - a['x'] - a['w'], 2)
                gaps[g] += 1
                gap_where[g].append(f"{label(r, row)} between {a['kind']} {a['name']} and {b['kind']} {b['name']}")
            # 4. a tool's own row lines up with the tool
            if row['follows']:
                tool = r['tools'].get(row['win'])
                if not tool:
                    follow_aligned.append(f"{label(r, row)} follows its tool, but no tool in use was reported")
                    continue
                controls = [q for q in parts if q['kind'] in CONTROLS]
                first, last = controls[0], controls[-1]
                left = abs(first['x'] - tool['x']) <= TOL
                right = abs(last['x'] + last['w'] - tool['x'] - tool['w']) <= TOL
                if row['win'] == 'overlay':
                    bounds = next(([sx, sx + sw] for sx, sy, sw, sh in screens
                                   if sx <= tool['x'] < sx + sw and sy <= tool['y'] < sy + sh), None)
                else:
                    bounds = [0, r['windows'].get(row['win'], (0, 0))[0]]
                kept = bounds is not None and (row['x'] - bounds[0] < row['h'] or bounds[1] - row['x'] - row['w'] < row['h'])
                if not (left or right or kept):
                    follow_aligned.append(f"{label(r, row)}: its controls run {first['x']:.2f}..{last['x'] + last['w']:.2f}, "
                                          f"the tool's button {tool['x']:.2f}..{tool['x'] + tool['w']:.2f}")
                # the distance from the tool's row: the row holding the tool (the overlay's tool row)
                holder = next((rr for k, rr in r['rows'].items() if rr['win'] == row['win'] and k != key and
                               any(abs(q['x'] - tool['x']) <= TOL and abs(q['y'] - tool['y']) <= TOL for q in r['parts'][k])), None)
                if holder:
                    d = holder['y'] - row['y'] - row['h'] if row['y'] < holder['y'] else row['y'] - holder['y'] - holder['h']
                    follow_gaps[round(d, 2)] += 1
        else:
            # 5. the window's bars: the sides and the first column
            groups = {}
            for q in parts:
                groups.setdefault((q['gx'], q['gw']), []).append(q)
            sided = [q for (gx, gw), qs in groups.items() if abs(gx + gw / 2 - (row['x'] + row['w'] / 2)) > 1 for q in qs]
            if not sided:
                continue
            sided.sort(key=lambda q: q['x'])
            first, last = sided[0], sided[-1]
            if first['x'] + first['w'] / 2 < row['x'] + row['w'] / 2:
                if first['kind'] in CONTROLS:
                    bar_sides.append((round(first['x'] - row['x'], 2), label(r, row, first) + ' (left)'))
                if first['kind'] not in CONTROLS or is_words(first):
                    bar_columns.append((round(first['ix'] - row['x'], 2), label(r, row, first)))
            if last['x'] + last['w'] / 2 > row['x'] + row['w'] / 2:
                if last['kind'] in CONTROLS:
                    bar_sides.append((round(row['x'] + row['w'] - last['x'] - last['w'], 2), label(r, row, last) + ' (right)'))
                else:
                    ends.append(f"{label(r, row, last)} at the bar's right side: words or an icon there stand in a control")

check(not centered, 'every part stands centered up and down in its row or bar', '; '.join(centered[:6]))
check(not ends, "every row's ends are controls, as far from the end as from the row's top and bottom (concentric)", '; '.join(ends[:6]))
pos = [g for g in gaps if g >= 0]
neg = [g for g in gaps if g < 0]
odd = []
if pos:
    usual = max(pos, key=lambda g: gaps[g])
    odd += [f"{g} ({gap_where[g][0]})" for g in pos if abs(g - usual) > 0.01]
if neg:
    overlap = max(neg, key=lambda g: gaps[g])
    odd += [f"{g} ({gap_where[g][0]})" for g in neg if abs(g - overlap) > 0.01]
check(not odd, 'neighbors in every row stand one gap apart (a split button overlaps by one amount)', '; '.join(odd[:6]))
if any(row['follows'] for r in reports for row in r['rows'].values()):
    check(not follow_aligned, "a tool's own row lines up its first or last control with the tool's button, edge to edge", '; '.join(follow_aligned[:6]))
    check(len(follow_gaps) <= 1, "a tool's own row stands one distance from the tool's row", str(dict(follow_gaps)))
if bar_sides:
    values = collections.Counter(v for v, _ in bar_sides)
    usual = values.most_common(1)[0][0]
    check(len(values) == 1, "the window's bars keep their side controls one distance from the window's sides, left and right",
          '; '.join(f'{v} at {w}' for v, w in bar_sides if v != usual)[:600] + f' (most: {usual})')
if bar_columns:
    values = collections.Counter(v for v, _ in bar_columns)
    usual = values.most_common(1)[0][0]
    check(len(values) == 1, "the window's bars start their first words in one column",
          '; '.join(f'{v} at {w}' for v, w in bar_columns if v != usual)[:600] + f' (most: {usual})')

# ── the menus ──
LEAD = 17          # a line's first mark: an icon (16), a check's box (15), a tick
END_GAP = 16       # a mark this far past the one before it is the line's end
m_surface_sides, m_surface_where = collections.Counter(), collections.defaultdict(list)
m_concentric, m_starts, m_start_where, m_ends, m_end_where = [], collections.Counter(), collections.defaultdict(list), collections.Counter(), collections.defaultdict(list)
m_forms, m_outside, m_words, m_centered, m_choices = [], [], [], [], []
menus_seen = 0
def mlabel(r, menu, line=None, mark=None):
    n = menu['name'] if menu['name'] != '-' else menu['kind']
    out = f"{r['step']}: {menu['win']} {n}"
    if line is not None:
        out += f" line {line}"
    if mark is not None:
        out += f" {mark['kind']}" + (f" {mark['name']}" if mark['name'] != '-' else '')
    return out
for r in reports:
    for key, menu in r['menus'].items():
        menus_seen += 1
        lines, surfaces, marks = r['lines'][key], r['surfaces'][key], r['marks'][key]
        mx, my, mr, mb = menu['x'], menu['y'], menu['x'] + menu['w'], menu['y'] + menu['h']
        used = sorted({s['line'] for s in surfaces} | {m['line'] for m in marks})
        first_line, last_line = (used[0], used[-1]) if used else (None, None)
        # 6. surfaces: sides, top and bottom, concentric corners
        for s in surfaces:
            left, right = s['x'] - mx, mr - s['x'] - s['w']
            m_surface_sides[round(left, 1)] += 1
            m_surface_where[round(left, 1)].append(mlabel(r, menu, s['line']))
            if abs(left - right) > TOL:
                m_concentric.append(f"{mlabel(r, menu, s['line'])} surface stands {left:.2f} from the left, {right:.2f} from the right")
            if s['h'] >= 8 and abs(s['r'] - (menu['r'] - left)) > TOL:
                m_concentric.append(f"{mlabel(r, menu, s['line'])} surface's corners {s['r']:.1f}, the menu's {menu['r']:.1f} less {left:.1f} is {menu['r'] - left:.1f}")
            if s['line'] == first_line and s['h'] >= 8 and abs((s['y'] - my) - left) > TOL:
                m_concentric.append(f"{mlabel(r, menu, s['line'])} first line's surface stands {s['y'] - my:.2f} from the top, {left:.2f} from the side")
            if s['line'] == last_line and s['h'] >= 8 and abs((mb - s['y'] - s['h']) - left) > TOL:
                m_concentric.append(f"{mlabel(r, menu, s['line'])} last line's surface stands {mb - s['y'] - s['h']:.2f} from the bottom, {left:.2f} from the side")
        # the lines held whole by a choice across them (the font menu's weight): their layout is the choice's (rule 10)
        across = {s['line'] for s in surfaces for c in r['choices'] if c['menu'] == key[1] and abs(c['x'] - s['x']) < 0.01 and abs(c['w'] - s['w']) < 0.01}
        words_cols = collections.Counter()
        words_where = collections.defaultdict(list)
        form_ends, form_starts = collections.Counter(), collections.Counter()
        for lid in used:
            line = lines.get(lid)
            ms = sorted((m for m in marks if m['line'] == lid), key=lambda m: m['x'])
            if not line or not ms:
                continue
            # 7. nothing outside its line or the menu
            for m in ms:
                if m['x'] < max(line['x'], mx) - TOL or m['x'] + m['w'] > min(line['x'] + line['w'], mr) + TOL:
                    m_outside.append(f"{mlabel(r, menu, lid, m)} runs {m['x'] - mx:.1f}..{m['x'] + m['w'] - mx:.1f} in a line {line['x'] - mx:.1f}..{line['x'] + line['w'] - mx:.1f}")
            # a form's line (a label set right, then its choices, Record's panel): the label stands centered on
            # the choice beside it
            form = ms[0]['kind'] == 'Text' and ms[0]['align'] & 2
            if form:
                label = ms[0]
                beside = next((m for m in ms[1:] if m['x'] >= label['x'] + label['w'] and m['y'] < label['y'] + label['h'] and label['y'] < m['y'] + m['h']), None)
                if beside and abs((label['y'] + label['h'] / 2) - (beside['y'] + beside['h'] / 2)) > 0.5 + 0.01:
                    m_centered.append(f"{mlabel(r, menu, lid, label)} centered {label['y'] + label['h'] / 2 - my:.1f} from the top, its choice {beside['y'] + beside['h'] / 2 - my:.1f}")
            # 9. centered up and down, in a field or well when in one; a header's words stand nearer the
            # lines they head (its own padding, as the mock draws it)
            if line['h'] <= 44 and line['kind'] != 'EctoMenuHeader' and not form:
                for m in ms:
                    box = next((s for s in surfaces if s['line'] == lid and s['h'] >= 8 and s['y'] - TOL <= m['y'] and m['y'] + m['h'] <= s['y'] + s['h'] + TOL), line)
                    top, bottom = m['y'] - box['y'], box['y'] + box['h'] - m['y'] - m['h']
                    # words center to a whole pixel, and so does a mark an odd number of pixels shorter
                    odd = round(box['h'] - m['h']) % 2 == 1
                    if abs(top - bottom) > (1 + 0.01 if m['kind'] in ('Text', 'Input') or odd else TOL):
                        m_centered.append(f"{mlabel(r, menu, lid, m)} stands {top:.1f} below the top, {bottom:.1f} above the bottom")
            if lid in across:
                continue
            first = ms[0]
            start = first['x'] - mx
            if first['kind'] == 'Text' and first['align'] & 2:
                # a form's label, set right: its end, and where its choice starts
                form_ends[round(first['x'] + first['w'] - mx, 1)] += 1
                after = next((m for m in ms[1:] if m['x'] > first['x'] + first['w']), None)
                if after:
                    form_starts[round(after['x'] - mx, 1)] += 1
                continue
            # the words after the line's first marks (a tick, an icon, a check's box and its tick): the first
            # words wider than a mark, after marks no wider than one
            k = next((i for i, m in enumerate(ms) if m['kind'] == 'Text' and m['w'] > LEAD), None)
            if k and all(m['w'] <= LEAD for m in ms[:k]):
                words_cols[round(ms[k]['x'] - mx, 1)] += 1
                words_where[round(ms[k]['x'] - mx, 1)].append(mlabel(r, menu, lid, ms[k]))
            m_starts[round(start, 1)] += 1
            m_start_where[round(start, 1)].append((r, menu, lid, first, key))
            # the end: a mark well past the one before it, in the line's right part
            ends = []
            for i, m in enumerate(ms[1:], 1):
                before = [o for o in ms[:i] if abs((o['y'] + o['h'] / 2) - (m['y'] + m['h'] / 2)) < max(o['h'], m['h']) / 2]
                reach = max((o['x'] + o['w'] for o in before), default=None)
                if reach is not None and m['x'] - reach >= END_GAP and m['x'] + m['w'] > mx + 0.6 * menu['w']:
                    ends.append(m)
            if ends:
                e = max(ends, key=lambda m: m['x'] + m['w'])
                m_ends[round(mr - e['x'] - e['w'], 1)] += 1
                m_end_where[round(mr - e['x'] - e['w'], 1)].append(mlabel(r, menu, lid, e))
        menu['words'] = words_cols.most_common(1)[0][0] if words_cols else None
        # 8. one words column in the menu
        if len(words_cols) > 1:
            usual = words_cols.most_common(1)[0][0]
            m_words.append(f"{mlabel(r, menu)}: " + '; '.join(f"{v} at {words_where[v][0]}" for v in words_cols if v != usual) + f" (most: {usual})")
        if len(form_ends) > 1 or len(form_starts) > 1:
            m_forms.append(f"{mlabel(r, menu)}: labels end at {dict(form_ends)}, their choices start at {dict(form_starts)}")
    # 10. the segmented controls
    for c in r['choices']:
        sides = [c['first'], c['w'] - c['last'], c['top'], c['h'] - c['bottom']]
        if max(sides) - min(sides) > TOL or abs(c['inner'] - (c['r'] - sides[0])) > TOL:
            m_choices.append(f"{r['step']}: {c['win']} choices stand {sides[0]:.1f} from the left, {sides[1]:.1f} from the right, "
                             f"{sides[2]:.1f} from the top, {sides[3]:.1f} from the bottom, corners {c['inner']:.1f} in {c['r']:.1f}")

if menus_seen:
    inset = m_surface_sides.most_common(1)[0][0] if m_surface_sides else None
    odd = [f"{v} at {m_surface_where[v][0]}" for v in m_surface_sides if v != inset]
    check(not odd and not m_concentric, "every menu line's surface stands one distance inside the menu, all round, concentric with its corners",
          '; '.join(odd[:3] + m_concentric[:5]) + (f' (most: {inset})' if odd else ''))
    # the column lines start in: of the lines that don't start with words in their menu's words column
    # (a choice not ticked keeps the tick's room)
    marked = collections.Counter()
    for v, where in m_start_where.items():
        for (r, menu, lid, first, key) in where:
            if not (first['kind'] == 'Text' and menu.get('words') is not None and abs(v - menu['words']) <= TOL):
                marked[v] += 1
    col = marked.most_common(1)[0][0] if marked else None
    # every line starts at one column; a line whose first mark is its words may start in its menu's words
    # column instead (a choice not ticked keeps the tick's room)
    bad_starts = []
    for v, where in m_start_where.items():
        for (r, menu, lid, first, key) in where:
            if abs(v - col) <= TOL or (first['kind'] == 'Text' and menu.get('words') is not None and abs(v - menu['words']) <= TOL):
                continue
            bad_starts.append(f"{v} at {mlabel(r, menu, lid, first)}")
    check(not bad_starts, "every menu line starts in one column, the same in every menu", '; '.join(bad_starts[:6]) + f' (most: {col})')
    end_odd = [f"{v} at {m_end_where[v][0]}" for v in m_ends if col is None or abs(v - col) > TOL]
    check(not end_odd and not m_forms and not m_outside,
          "every menu line ends its end (a key, a reason, an arrow, a choice) as far from the menu's right as its start from the left; nothing stands outside its line",
          '; '.join(end_odd[:4] + m_forms[:2] + m_outside[:4]) + f' (start {col}, ends {dict(m_ends)})')
    check(not m_words, "in every menu, the words after a line's first mark start in one column", '; '.join(m_words[:4]))
    check(not m_centered, "every menu line's marks stand centered up and down", '; '.join(m_centered[:6]))
for_choices = [c for r in reports for c in r['choices']]
if for_choices:
    check(not m_choices, "every segmented control's choices stand as far from its sides as from its top and bottom, concentric",
          '; '.join(m_choices[:4]))
fonts = [(r, f) for r in reports for f in r['fonts']]
if fonts:
    wrong = []
    for r, f in fonts:
        if r['look'] and f['family'] not in r['look'] and not f['own']:
            wrong.append(f"{r['step']}: {f['win']} {f['in'] if f['in'] != '-' else ''} {f['name']} in {f['family']}".replace('  ', ' '))
    tips = sum(1 for _, f in fonts if f['in'] == 'tip')
    check(not wrong, f"every word in the overlay and the window is in the look's faces ({tips} tooltip words seen)", '; '.join(sorted(set(wrong))[:8]))

for ok, name, why in results:
    print(('PASS  ' if ok else 'FAIL  ') + name + ('' if ok else f' ({why})'))
sys.exit(0 if all(r[0] for r in results) else 1)
