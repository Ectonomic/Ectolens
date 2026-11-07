# Finds the last item of the open Record menu (Recording Settings…): the menu panel is the
# dark block that opens upward above the arrow; its last item sits just above its bottom edge.
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
ax, ay = int(sys.argv[2]), int(sys.argv[3])
x = ax + 60  # the menu opens to the right of the arrow, upward
bottom = None
for y in range(ay - 20, 0, -1):
    r, g, b = im.getpixel((x, y))
    if 40 < r + g + b < 200 and abs(r - b) < 20:
        bottom = y
        break
print(x, (bottom or ay) - 16)
