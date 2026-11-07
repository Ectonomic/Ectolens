# Saves the private KWin's screenshot of its virtual screen (ScreenShot2, as Spectacle uses) as a PNG.
import dbus, os, sys
import numpy as np
from PIL import Image
out = sys.argv[1]
bus = dbus.SessionBus()
shot = dbus.Interface(bus.get_object('org.kde.KWin', '/org/kde/KWin/ScreenShot2'), 'org.kde.KWin.ScreenShot2')
r, w = os.pipe()
fd = dbus.types.UnixFd(w)
os.close(w)
res = shot.CaptureWorkspace(dbus.Dictionary({'native-resolution': True, 'include-cursor': False}, signature='sv'), fd)
del fd
res = {str(k): v for k, v in res.items()}
W, H, S, F = int(res['width']), int(res['height']), int(res['stride']), int(res['format'])
buf = bytearray()
while len(buf) < S * H:
    c = os.read(r, 1 << 20)
    if not c:
        break
    buf.extend(c)
a = np.frombuffer(bytes(buf[:S * H]), dtype=np.uint8).reshape(H, S)[:, :W * 4].reshape(H, W, 4)
# QImage::Format_ARGB32 / RGB32 / ARGB32_Premultiplied are BGRA in memory on little-endian
Image.fromarray(a[:, :, [2, 1, 0]].copy()).save(out)
print(out, 'format', F, W, H)
