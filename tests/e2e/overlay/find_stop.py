# Finds the Stop button of the recording toolbar in a screenshot: the toolbar is the
# dark strip holding the red recording dot below the frame; Stop is its last button.
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
W, H = im.size
for y in range(H - 1, 0, -1):
    row = [im.getpixel((x, y)) for x in range(W)]
    reds = [x for x, (r, g, b) in enumerate(row) if r > 190 and g < 100 and b < 110]
    if len(reds) >= 4:
        x0 = reds[0]
        right = x0
        x = x0
        gap = 0
        while x < W - 1 and gap < 30:
            x += 1
            gap = gap + 1 if sum(row[x]) < 20 else 0
            if gap == 0:
                right = x
        print(right - 30, y)
        sys.exit(0)
print('0 0')
sys.exit(1)
