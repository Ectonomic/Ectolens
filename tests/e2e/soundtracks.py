"""A recording's sound tracks, judged by their tones (standin-sound.sh plays 440 Hz on the
stand-in speakers and 880 Hz into the stand-in microphone):

    python3 -I soundtracks.py FILE mixed|separate [SECONDS]

mixed: one track, both tones. separate: track 1 both tones, the default track, named Mix;
track 2 only 440 Hz, named System sound; track 3 only 880 Hz, named Microphone. Each track
lasts about SECONDS (the recording's length) when it is given. Prints PASS/FAIL lines and
exits 1 on any FAIL."""
import json
import subprocess
import sys

import numpy as np

path, mode = sys.argv[1], sys.argv[2]
seconds = float(sys.argv[3]) if len(sys.argv) > 3 else None
streams = json.loads(subprocess.run(
    ['ffprobe', '-v', 'error', '-select_streams', 'a', '-show_entries',
     'stream=index,codec_name:stream_tags=handler_name,title:stream_disposition=default', '-of', 'json', path],
    capture_output=True, text=True).stdout or '{"streams": []}').get('streams', [])


def tones(n):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-map', f'0:a:{n}', '-ac', '1', '-ar', '48000', '-f', 'f32le', '-'],
                         capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype=np.float32)
    length = len(x) / 48000
    x = x[24000:]  # past the first half second
    if len(x) < 4800:
        return 0.0, 0.0, length
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1 / 48000)
    level = lambda f: float(spec[(freqs > f - 5) & (freqs < f + 5)].max())
    return level(440), level(880), length


fails = 0


def check(name, ok, why):
    global fails
    print(('PASS  ' if ok else 'FAIL  ') + name + ('' if ok else f' ({why})'))
    fails += not ok


want = [('Mix', True, True), ('System sound', True, False), ('Microphone', False, True)] if mode == 'separate' else [(None, True, True)]
check(f'the file has {len(want)} sound track{"s" if len(want) > 1 else ""}', len(streams) == len(want), f'{len(streams)} found')
measured = [tones(n) for n in range(len(streams))]
loudest = max([max(m[:2]) for m in measured] or [0]) or 1
for n, (title, has440, has880) in enumerate(want[:len(streams)]):
    s, (t440, t880, secs) = streams[n], measured[n]
    tags = s.get('tags') or {}
    name = tags.get('handler_name') or tags.get('title')
    hears = lambda present, level: level > 0.05 * loudest if present else level < 0.01 * loudest
    label = title or 'the one track'
    check(f'{label}: 440 Hz (the speakers) {"heard" if has440 else "absent"}, 880 Hz (the microphone) {"heard" if has880 else "absent"}',
          hears(has440, t440) and hears(has880, t880), f'440 Hz at {t440 / loudest:.3f}, 880 Hz at {t880 / loudest:.3f} of the loudest')
    if title:
        check(f'{label}: named {title!r}', name == title, f'named {name!r}')
    if seconds:
        check(f'{label}: as long as the recording ({seconds:.1f} s)', abs(secs - seconds) <= 0.7, f'{secs:.2f} s')
    check(f'{label}: {"the default track" if n == 0 else "not the default track"}', s['disposition']['default'] == (1 if n == 0 else 0),
          f'default={s["disposition"]["default"]}')
sys.exit(1 if fails else 0)
