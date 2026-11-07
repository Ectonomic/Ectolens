# The text box's handles as the Ectolens test script last reported them (inner.sh, textzoom):
# "NAME X Y" per handle. With a release number, the report made after that mouse release
# (ECTOLENS-RELEASE N, then the handles, then "ECTOLENS-WAT textBoxHandle end"); with "last",
# the latest complete report. Prints nothing while that report isn't complete.
import sys

log, which = sys.argv[1], sys.argv[2]
blocks, cur, rel = [], None, None
for line in open(log, errors='replace').read().splitlines():
    p = line.split()
    if p[:1] == ['ECTOLENS-RELEASE'] and len(p) == 2:
        rel, cur = int(p[1]), []
    elif p[:1] == ['ECTOLENS-WAT'] and len(p) >= 3 and p[1].startswith('textBoxHandle'):
        if p[1] == 'textBoxHandle' and p[2] == 'end':
            blocks.append((rel, cur or []))
            cur, rel = None, None
        elif len(p) == 4:
            cur = (cur or []) + [(p[1], p[2], p[3])]
if which == 'last':
    chosen = blocks[-1][1] if blocks else []
else:
    chosen = next((b for r, b in blocks if r == int(which)), [])
for name, x, y in chosen:
    print(name, x, y)
