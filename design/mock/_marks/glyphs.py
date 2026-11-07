"""Ectolens mark candidates in the Ecto family's 64-unit box: the lime splat with one solid dark green
symbol, its weight centred by eye on the splat's body (about (31.8, 31.8), as the mountain and the play).
Family rules kept: symbols are solid with no seam or window cut through the ink; a lone central dot and
a plain ring belong to other family marks; a dot inside a curve reads as an eye on the splat.
The splat, ink color and line weights come from family.py."""
import math
import family as H

INK = H.INK
weight = H.weight

def lens(H_=26.0, W=12.4, cx=31.8, cy=31.8, round_=1.6, ink=INK, turn=0):
    """A lens as optics draws it: two equal arcs meeting in soft points (a biconvex lens, side on)."""
    a, b = W / 2, H_ / 2
    R = (a * a + b * b) / (2 * a)
    d = (f'M {cx:.3f} {cy - b:.3f} A {R:.3f} {R:.3f} 0 0 1 {cx:.3f} {cy + b:.3f} '
         f'A {R:.3f} {R:.3f} 0 0 1 {cx:.3f} {cy - b:.3f} Z')
    t = f' transform="rotate({turn} {cx} {cy})"' if turn else ''
    return f'<path d="{d}" fill="{ink}" stroke="{ink}" stroke-width="{round_}" stroke-linejoin="round"{t}/>'

def frame(px=64, w=26.0, h=17.0, r=3.4, nub=None, cx=31.8, cy=31.8, ink=INK):
    """The capture frame: a rounded outline with a round handle at each corner, the overlay's own frame."""
    sw = weight(px)
    nub = nub if nub is not None else sw * 0.55 + 2.2
    x0, y0 = cx - w / 2, cy - h / 2
    out = f'<rect x="{x0:.3f}" y="{y0:.3f}" width="{w:.3f}" height="{h:.3f}" rx="{r:.3f}" fill="none" stroke="{ink}" stroke-width="{sw}"/>'
    for x, y in ((x0, y0), (x0 + w, y0), (x0, y0 + h), (x0 + w, y0 + h)):
        out += f'<circle cx="{x:.3f}" cy="{y:.3f}" r="{nub:.3f}" fill="{ink}"/>'
    return out

# Set aside (drawn on the page with the reason)
def aperture(cx=31.8, cy=31.8, ink=INK):
    pts = ' '.join(f'{cx + 5.2*math.cos(math.radians(60*i+30)):.2f},{cy + 5.2*math.sin(math.radians(60*i+30)):.2f}' for i in range(6))
    return (f'<circle cx="{cx}" cy="{cy}" r="11.5" fill="{ink}"/>'
            f'<polygon points="{pts}" fill="#b4e41e"/>')
def spectacles(ink=INK):
    return (f'<circle cx="23.6" cy="32.4" r="6.4" fill="none" stroke="{ink}" stroke-width="4"/>'
            f'<circle cx="40" cy="32.4" r="6.4" fill="none" stroke="{ink}" stroke-width="4"/>'
            f'<path d="M29.6 31.2 q2.2 -2.4 4.4 0" fill="none" stroke="{ink}" stroke-width="3" stroke-linecap="round"/>')
def magnifier(px=64, ink=INK):
    sw = weight(px)
    return (f'<circle cx="29.2" cy="29.2" r="8.6" fill="none" stroke="{ink}" stroke-width="{sw}"/>'
            f'<path d="M35.8 35.8 L42.6 42.6" stroke="{ink}" stroke-width="{sw + 1.4}" stroke-linecap="round"/>')
def crop(px=64, ink=INK):
    sw = weight(px)
    return (f'<path d="M24 17.5 V36.5 H43" fill="none" stroke="{ink}" stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round"/>'
            f'<path d="M20 27 H39.5 V46" fill="none" stroke="{ink}" stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round" transform="translate(0 -9)"/>')
def lens_flat(ink=INK):
    return lens(H_=26.0, W=12.4, turn=90, ink=ink)
def lens_beam(ink=INK):
    return lens(cx=25.6, ink=ink) + f'<polygon points="31.5,24 31.5,39.6 44.6,31.8" fill="{ink}" stroke="{ink}" stroke-width="2.6" stroke-linejoin="round"/>'

def svg(sym, px=None, gid='g'):
    size = f' width="{px}" height="{px}"' if px else ''
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"{size}>'
            f'<defs><linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="4" y1="4" x2="60" y2="60">'
            f'<stop offset="0" stop-color="#c8f22e"/><stop offset="1" stop-color="#9be012"/></linearGradient></defs>'
            f'<path d="{H.SPLAT}" fill="url(#{gid})"/>{sym}</svg>')

def lens_barrel(H_=26.0, W=13.0, cut=0.2, cx=31.8, cy=31.8, ink=INK):
    """The lens with flat rims top and bottom, as a real lens element is ground."""
    a, b = W / 2, H_ / 2
    R = (a * a + b * b) / (2 * a)
    yc = b * (1 - cut)
    # x of the arc at height yc: the arc's centre sits at (cx -/+ (R - a), cy)
    xr = math.sqrt(R * R - yc * yc) - (R - a)
    d = (f'M {cx - xr:.3f} {cy - yc:.3f} H {cx + xr:.3f} A {R:.3f} {R:.3f} 0 0 1 {cx + xr:.3f} {cy + yc:.3f} '
         f'H {cx - xr:.3f} A {R:.3f} {R:.3f} 0 0 1 {cx - xr:.3f} {cy - yc:.3f} Z')
    return f'<path d="{d}" fill="{ink}" stroke="{ink}" stroke-width="1.6" stroke-linejoin="round"/>'

def lens_outline(px=64, H_=27.0, W=14.4, cx=31.8, cy=31.8, ink=INK):
    """The lens as optics diagrams draw it: the same two arcs, as a line of the family's weight (the ring's sibling)."""
    sw = weight(px)
    a, b = W / 2, H_ / 2
    R = (a * a + b * b) / (2 * a)
    d = (f'M {cx:.3f} {cy - b:.3f} A {R:.3f} {R:.3f} 0 0 1 {cx:.3f} {cy + b:.3f} '
         f'A {R:.3f} {R:.3f} 0 0 1 {cx:.3f} {cy - b:.3f} Z')
    return f'<path d="{d}" fill="none" stroke="{ink}" stroke-width="{sw}" stroke-linejoin="round"/>'

def frame_handles(px=64, w=27.0, h=19.0, cx=31.8, cy=31.8, ink=INK):
    """The capture frame as the overlay draws it: a band with a three-quarter handle on each corner that
    curves into the band (the overlay's frame handles). Plain paths, so KDE's SVG renderer draws it."""
    t = weight(px) * 0.74
    R = t * 0.62 + 2.0
    f = R * 0.45
    xf = math.sqrt((R + f) ** 2 - f ** 2)
    def on(x, y):
        d = math.hypot(x, y); return x * R / d, y * R / d
    (a1, b1), (c1, d1) = on(xf, -f), on(-f, xf)
    corner = (f'M {xf:.3f} 0 A {f:.3f} {f:.3f} 0 0 1 {a1:.3f} {b1:.3f} A {R:.3f} {R:.3f} 0 1 0 {c1:.3f} {d1:.3f} '
              f'A {f:.3f} {f:.3f} 0 0 1 0 {xf:.3f} L {t:.3f} {xf:.3f} L {t:.3f} {t:.3f} L {xf:.3f} {t:.3f} Z')
    x0, y0 = cx - w / 2, cy - h / 2
    band = (f'M {x0:.3f} {y0:.3f} h {w:.3f} v {h:.3f} h {-w:.3f} Z '
            f'M {x0 + t:.3f} {y0 + t:.3f} v {h - 2*t:.3f} h {w - 2*t:.3f} v {-(h - 2*t):.3f} Z')
    out = f'<path d="{band}" fill="{ink}" fill-rule="evenodd"/>'
    for x, y, r in ((x0, y0, 0), (x0 + w, y0, 90), (x0 + w, y0 + h, 180), (x0, y0 + h, 270)):
        out += f'<path transform="translate({x:.3f} {y:.3f}) rotate({r})" d="{corner}" fill="{ink}"/>'
    return out

def play(H_=19.8, ratio=0.866, w=4.2, cx=32.6, cy=31.8, ink=INK):
    """A play triangle, for the family row."""
    W = H_ * ratio; x0 = cx - W / 3
    return f'<polygon points="{x0:.3f},{cy - H_/2:.3f} {x0:.3f},{cy + H_/2:.3f} {x0 + W:.3f},{cy:.3f}" fill="{ink}" stroke="{ink}" stroke-width="{w}" stroke-linejoin="round"/>'
def viewfinder(px=64, ink=INK):
    sw = weight(px); d = ''
    for (x, y, dx, dy) in ((19, 21, 1, 1), (45, 21, -1, 1), (19, 43, 1, -1), (45, 43, -1, -1)):
        d += f'M {x} {y + 6*dy} V {y} H {x + 6*dx} '
    return f'<path d="{d}" fill="none" stroke="{ink}" stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round"/>'

def screen(px=64, w=30.0, h=19.5, r=3.2, cx=31.8, cy=29.6, stand=True, ink=INK):
    """A solid rounded screen on a short stand, the family's soft geometry."""
    x0, y0 = cx - w / 2, cy - h / 2
    out = f'<rect x="{x0:.3f}" y="{y0:.3f}" width="{w:.3f}" height="{h:.3f}" rx="{r:.3f}" fill="{ink}"/>'
    if stand:
        sy = y0 + h
        out += f'<rect x="{cx - 2.4:.3f}" y="{sy - 1:.3f}" width="4.8" height="4.6" fill="{ink}"/>'
        out += f'<rect x="{cx - 7.2:.3f}" y="{sy + 3.2:.3f}" width="14.4" height="3.4" rx="1.7" fill="{ink}"/>'
    return out

def screen_frame(px=64, cx=31.8, cy=29.6, ink=INK):
    """The screen with the capture frame drawn into it in lime: a frame and its four round handles."""
    out = screen(px, cx=cx, cy=cy, ink=ink)
    fw, fh = 15.0, 9.0
    x0, y0 = cx - fw / 2 + 0.6, cy - fh / 2 - 0.2
    sw = 2.2 if px >= 40 else 2.8 if px >= 20 else 3.4
    hr = sw * 0.95 + 0.5
    lime = '#c8f22e'
    out += f'<rect x="{x0:.3f}" y="{y0:.3f}" width="{fw:.3f}" height="{fh:.3f}" fill="none" stroke="{lime}" stroke-width="{sw}"/>'
    for x, y in ((x0, y0), (x0 + fw, y0), (x0, y0 + fh), (x0 + fw, y0 + fh)):
        out += f'<circle cx="{x:.3f}" cy="{y:.3f}" r="{hr:.3f}" fill="{lime}"/>'
    return out

def corners(px=64, w=None, h=None, arm=None, cx=31.8, cy=31.8, ink=INK):
    """Viewfinder corners: four rounded L's at the family's line weight. Below 32 px the frame opens wider and
    the arms grow, so each corner stays an L instead of a dot."""
    small = px < 32
    w = w if w is not None else (32.0 if small else 30.0)
    h = h if h is not None else (26.0 if small else 24.0)
    arm = arm if arm is not None else (8.6 if small else 7.6)
    sw = weight(px); x0, y0, x1, y1 = cx - w/2, cy - h/2, cx + w/2, cy + h/2
    d = (f'M {x0} {y0+arm} V {y0} H {x0+arm} M {x1-arm} {y0} H {x1} V {y0+arm} '
         f'M {x1} {y1-arm} V {y1} H {x1-arm} M {x0+arm} {y1} H {x0} V {y1-arm}')
    return f'<path d="{d}" fill="none" stroke="{ink}" stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round"/>'

def corners_screen(px=64, ink=INK):
    """Viewfinder corners around a small solid screen: the screen, framed for capture."""
    out = corners(px, w=33.0, h=27.0, arm=7.0)
    sw = weight(px)
    sw2, sh2 = (15.0, 9.8) if px >= 32 else (16.5, 11.0)
    out += f'<rect x="{31.8 - sw2/2:.3f}" y="{31.0 - sh2/2:.3f}" width="{sw2}" height="{sh2}" rx="2.2" fill="{ink}"/>'
    out += f'<rect x="{31.8 - 4.6:.3f}" y="{31.0 + sh2/2 + 1.6:.3f}" width="9.2" height="2.6" rx="1.3" fill="{ink}"/>'
    return out
