"""Shared drawing constants for Ectolens's mark, in the Ecto family's 64-unit box.

The family: one lime splat, one dark green symbol on it, centred by eye on the splat's body
(its largest inscribed circle sits at about (31.8, 31.3)). Lines follow the family's mark weights by
size: 4.4 at 96 px and up, 5 from 40, 6 from 20, 7 below 20 (`weight`).

glyphs.py draws the symbol (`g(px, ink)` returns SVG for the 64 box; px is the size it will be shown
at, so lines can change with size) and build_icons.py writes the icons from it."""
import pathlib

HERE = pathlib.Path(__file__).parent
SPLAT = (HERE / 'splat.txt').read_text().strip()
INK = '#163d0a'; LIME = '#c8f22e'; LIME2 = '#9be012'


def weight(px):
    """The family's mark line weights, by the size the mark is shown at."""
    return 4.4 if px >= 96 else 5.0 if px >= 40 else 6.0 if px >= 20 else 7.0


def k_of(px):
    """The older family rule some siblings use: strokes 1.3 times heavier at 32 px and below."""
    return 1.3 if px <= 32 else 1.0
