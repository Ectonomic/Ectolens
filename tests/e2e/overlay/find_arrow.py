# Finds Record's arrow on the picture toolbar: Record's red dot, then rightward along the
# toolbar to its end; the arrow is the last button. Prints "X Y" of the arrow's middle.
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
W, H = im.size
dots = [(x, y) for y in range(0, H, 2) for x in range(0, W, 2)
        if (lambda p: p[0] > 190 and p[1] < 100 and p[2] < 110)(im.getpixel((x, y)))]
if not dots:
    print('0 0'); sys.exit(1)
# the toolbar's dot: the red cluster nearest the bottom of the frame region (largest y wins ties poorly; take the median y of the lowest group)
ys = sorted(set(y for _, y in dots))
y = ys[len(ys) // 2]
xs = sorted(x for x, yy in dots if abs(yy - y) <= 4)
x = xs[-1]
gap = 0
end = x
while x < W - 1 and gap < 12:
    x += 1
    r, g, b = im.getpixel((x, y))
    if 20 < r + g + b < 400:
        end = x; gap = 0
    else:
        gap += 1
print(end - 14, y)
