# Films the private KWin's screen for the README's moving pictures: KWin's own screenshots
# (ScreenShot2, as Spectacle takes them), one after another, with the pointer drawn in.
# KWin grants ScreenShot2 by desktop file, so readme-scenes.sh gives this Python one, in the
# sandbox only. Commands come one per line on the FIFO:
#   area X Y W H        what to film, in KWin's screen coordinates (default: the whole screen)
#   start NAME [FPS]    film into OUT/film/NAME/ until stop (default 25 frames a second)
#   stop
#   mark NAME           the time into the film's marks.txt, as this reads it (readme-scenes.sh stamps
#                       its marks with the wall clock instead: a line read late would mark late)
#   still NAME          one picture into OUT/film/NAME.still
#   quit
# Each frame is the raw picture KWin sent (BGRA), compressed, with the time it was taken
# (frames.txt: index, seconds since start, width, height, stride). tools/readme-pictures.py
# cuts the frames into the README's pictures.
# usage: python3 -I film.py FIFO OUT
import dbus, os, queue, sys, threading, time, zlib

fifo, out = sys.argv[1], sys.argv[2]
bus = dbus.SessionBus()
shot = dbus.Interface(bus.get_object('org.kde.KWin', '/org/kde/KWin/ScreenShot2'), 'org.kde.KWin.ScreenShot2')
log = open(os.path.join(out, 'film.log'), 'a', buffering=1)
area = None


def capture():
    r, w = os.pipe()
    fd = dbus.types.UnixFd(w)
    os.close(w)
    options = dbus.Dictionary({'native-resolution': True, 'include-cursor': True}, signature='sv')
    if area:
        res = shot.CaptureArea(area[0], area[1], dbus.UInt32(area[2]), dbus.UInt32(area[3]), options, fd)
    else:
        res = shot.CaptureWorkspace(options, fd)
    del fd
    res = {str(k): v for k, v in res.items()}
    width, height, stride = int(res['width']), int(res['height']), int(res['stride'])
    chunks, size = [], 0
    while size < stride * height:
        c = os.read(r, 1 << 22)
        if not c:
            break
        chunks.append(c)
        size += len(c)
    os.close(r)
    return width, height, stride, b''.join(chunks)[:stride * height]


writes = queue.Queue(maxsize=64)


def writer():
    while True:
        job = writes.get()
        if job is None:
            return
        path, data = job
        with open(path, 'wb') as f:
            f.write(zlib.compress(data, 1))
        writes.task_done()


writers = [threading.Thread(target=writer, daemon=True) for _ in range(4)]
for t in writers:
    t.start()

filming = threading.Event()
film_thread = None
film_now = {}   # the film being made: its folder and when it started


def film(name, fps):
    folder = os.path.join(out, 'film', name)
    os.makedirs(folder, exist_ok=True)
    index = open(os.path.join(folder, 'frames.txt'), 'w', buffering=1)
    start = time.monotonic()
    # the wall clock at the film's start, for the marks readme-scenes.sh stamps with date
    with open(os.path.join(folder, 'start.txt'), 'w') as f:
        f.write(f'{time.time() - (time.monotonic() - start):.6f}\n')
    film_now.update(folder=folder, start=start)
    n = 0
    while filming.is_set():
        due = start + n / fps
        now = time.monotonic()
        if now < due:
            time.sleep(due - now)
        taken = time.monotonic() - start
        try:
            width, height, stride, data = capture()
        except dbus.DBusException as e:
            log.write(f'{name}: capture failed: {e}\n')
            time.sleep(0.2)
            continue
        writes.put((os.path.join(folder, f'{n:05d}.zraw'), data))
        index.write(f'{n} {taken:.4f} {width} {height} {stride}\n')
        n += 1
        # behind by more than a frame: take the next one now rather than catching up in a burst
        if time.monotonic() - start > (n + 1) / fps:
            start = time.monotonic() - n / fps
    index.close()
    log.write(f'{name}: {n} frames in {time.monotonic() - start:.2f} s\n')


fd = os.open(fifo, os.O_RDWR)
with os.fdopen(fd, 'r', buffering=1) as commands:
    for line in commands:
        words = line.split()
        if not words:
            continue
        log.write(' '.join(words) + '\n')
        if words[0] == 'area':
            area = [int(v) for v in words[1:5]]
        elif words[0] == 'start':
            filming.set()
            film_thread = threading.Thread(target=film, args=(words[1], float(words[2]) if len(words) > 2 else 25.0))
            film_thread.start()
        elif words[0] == 'stop':
            filming.clear()
            if film_thread:
                film_thread.join()
            writes.join()
            open(os.path.join(out, 'film', 'stopped'), 'w').close()
        elif words[0] == 'mark' and film_now:
            with open(os.path.join(film_now['folder'], 'marks.txt'), 'a') as f:
                f.write(f"{words[1]} {time.monotonic() - film_now['start']:.4f}\n")
        elif words[0] == 'still':
            width, height, stride, data = capture()
            os.makedirs(os.path.join(out, 'film'), exist_ok=True)
            with open(os.path.join(out, 'film', words[1] + '.still'), 'wb') as f:
                f.write(f'{width} {height} {stride}\n'.encode() + zlib.compress(data, 1))
            open(os.path.join(out, 'film', words[1] + '.done'), 'w').close()
        elif words[0] == 'quit':
            break
writes.join()
for _ in writers:
    writes.put(None)
