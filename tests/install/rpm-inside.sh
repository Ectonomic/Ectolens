#!/bin/bash
# Run by rpm-check.sh as the user "stranger" in a container where the Ectolens RPM was just
# installed: a headless KWin session, then the installed Ectolens judged from outside.
set -u
if [ "${1:-}" != session ]; then
    export XDG_RUNTIME_DIR=/tmp/rt-stranger XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wl-stranger QT_QPA_PLATFORM=wayland
    mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
    # a copy of KWin: the installed one carries file capabilities, which a rootless container can't grant
    cp "$(command -v kwin_wayland)" /tmp/kwin_wayland
    cd "$HOME"
    exec dbus-run-session -- bash "$0" session
fi

bad=0
pass() { echo "PASS  $*"; }
fail() { echo "FAIL  $*"; bad=1; }
kga() { gdbus call --session --dest org.kde.kglobalaccel "$@" 2>/dev/null; }
keys() { local o; o=$(kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.shortcutKeys "['$1', '$2', '', '']"); o=${o#(}; echo "${o%,)}"; }
SP=org.kde.spectacle.desktop EL=com.ectonomic.ectolens.desktop
STATE=$HOME/.local/state/ectolens-install

pipewire >/tmp/pipewire.log 2>&1 &
sleep 0.5
wireplumber >/tmp/wireplumber.log 2>&1 &
/tmp/kwin_wayland --virtual --no-lockscreen --width 1280 --height 720 --socket "$WAYLAND_DISPLAY" >/tmp/kwin.log 2>&1 &
for _ in $(seq 100); do kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.allMainComponents | grep -q "'$SP'" && break; sleep 0.2; done
kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.allMainComponents | grep -q "'$SP'" || { fail "the test session never came up"; exit 1; }

# the desktop file names the installed program, as KWin 6.7 needs for screenshots
exe=$(sed -n 's/^Exec=\([^ ]*\).*/\1/p' /usr/share/applications/com.ectonomic.ectolens.desktop | head -1)
[ "$exe" = /usr/bin/ectolens ] && pass "the menu entry starts /usr/bin/ectolens" || fail "the menu entry's Exec is $exe"
ldd /usr/bin/ectolens | grep -q "/usr/lib64/ectolens/lib/libKPipeWireRecord" \
    && pass "Ectolens uses its own KPipeWire (/usr/lib64/ectolens), not the system's" || fail "KPipeWire: $(ldd /usr/bin/ectolens | grep KPipeWire)"

rm -f /tmp/shot.png
timeout 60 ectolens -b -n -f -o /tmp/shot.png >/tmp/shot.log 2>&1
size=$( [ -f /tmp/shot.png ] && od -An -tu1 -j16 -N8 /tmp/shot.png | awk '{print $1*16777216+$2*65536+$3*256+$4 "x" $5*16777216+$6*65536+$7*256+$8}')
[ "$size" = 1280x720 ] && pass "the installed Ectolens took a 1280x720 screenshot through KWin" || { fail "screenshot: '${size:-none}'"; tail -5 /tmp/shot.log; }

touch /tmp/before-recording
ECTOLENS_TEST_SCRIPT="frame:200,150,800,450;wait:800;record;wait:5000;stop;wait:6000" QT_FORCE_STDERR_LOGGING=1 ectolens -r >/tmp/record.log 2>&1 &
rec=$! video=
for _ in $(seq 120); do
    name=$(sed -n 's/^ECTOLENS-SAVED //p' /tmp/record.log | head -1); name=${name##*/}
    [ -n "$name" ] && video=$(find "$HOME" -name "$name" -newer /tmp/before-recording 2>/dev/null | head -1)
    [ -n "$video" ] && [ -s "$video" ] && break
    kill -0 "$rec" 2>/dev/null || break
    sleep 0.25
done
sleep 0.5
probe=$( [ -n "$video" ] && ffprobe -v error -select_streams v:0 -show_entries stream=codec_name:format=duration -of default=nw=1 "$video" 2>&1 | tr '\n' ' ')
dur=$(sed -n 's/.*duration=\([0-9.]*\).*/\1/p' <<<"$probe")
[ -n "$dur" ] && awk -v d="$dur" 'BEGIN{exit !(d >= 4)}' && grep -q codec_name=h264 <<<"$probe" \
    && pass "a 5-second recording was saved and plays back: $probe" \
    || { fail "recording: '${video:-none}' '$probe'"; grep -vE "OCR|Tesseract|tessdata|locale|UTF-8|languages|pts:" /tmp/record.log | tail -15 | sed 's/^/  record.log: /'; }
kill "$rec" 2>/dev/null; sleep 1

# the shortcuts: ectolens-shortcuts take, Print Screen, give-back
sp=$(kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.getComponent "$SP" | sed -n "s/^(objectpath '\(.*\)',)$/\1/p")
for a in $(kga --object-path "$sp" --method org.kde.kglobalaccel.Component.shortcutNames | sed "s/^(\[//; s/\],)$//" | tr ',' '\n' | tr -d "' "); do
    printf '%s\t%s\n' "$a" "$(keys "$SP" "$a")"
done > /tmp/before
ectolens-shortcuts take > /tmp/take.log 2>&1 && grep -q "shortcuts moved" /tmp/take.log \
    && [ "$(keys "$EL" _launch)" = "$(grep -P '^_launch\t' /tmp/before | cut -f2)" ] && [ "$(keys "$SP" _launch)" = '@a(ai) []' ] \
    && pass "ectolens-shortcuts take gives Print Screen to Ectolens: $(grep 'shortcuts moved' /tmp/take.log)" || { fail "ectolens-shortcuts take"; cat /tmp/take.log; }
p=$(kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.getComponent "$EL" | sed -n "s/^(objectpath '\(.*\)',)$/\1/p")
kga --object-path "$p" --method org.kde.kglobalaccel.Component.invokeShortcut _launch default >/dev/null
pid=
for _ in $(seq 75); do pid=$(pgrep -u stranger -x ectolens | head -1); [ -n "$pid" ] && break; sleep 0.2; done
[ -n "$pid" ] && [ "$(readlink /proc/$pid/exe)" = /usr/bin/ectolens ] && pass "Print Screen starts the installed Ectolens" || fail "Print Screen started no Ectolens"
[ -n "$pid" ] && kill "$pid"; sleep 1
ectolens-shortcuts give-back > /tmp/give.log 2>&1; rc=$?
ok=1
while IFS=$'\t' read -r a k; do [ "$(keys "$SP" "$a")" = "$k" ] || ok=0; done < /tmp/before
[ "$rc" = 0 ] && [ "$ok" = 1 ] && [ ! -e "$STATE/spectacle-shortcuts" ] \
    && pass "ectolens-shortcuts give-back gives Spectacle every shortcut it had" || { fail "ectolens-shortcuts give-back (exit $rc)"; cat /tmp/give.log; }
kill $(jobs -p) 2>/dev/null
exit $bad
