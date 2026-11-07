#!/bin/bash
# Run by stranger-check.sh inside each container, as the user "stranger": a headless KWin
# session, then install.sh and uninstall.sh run the way the README tells a person to,
# each judged from outside (KWin's shortcut service, the files, a real screenshot).
#   inside.sh installs|refused
set -u
expect=$1
if [ "${2:-}" != session ]; then
    export XDG_RUNTIME_DIR=/tmp/rt-stranger XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wl-stranger
    export QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software
    mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
    # a copy of KWin: the installed one carries file capabilities, which a rootless container can't grant
    cp "$(command -v kwin_wayland)" /tmp/kwin_wayland
    exec dbus-run-session -- bash "$0" "$expect" session
fi

bad=0
pass() { echo "PASS  $*"; }
fail() { echo "FAIL  $*"; bad=1; }
kga() { gdbus call --session --dest org.kde.kglobalaccel "$@" 2>/dev/null; }
keys() { local o; o=$(kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.shortcutKeys "['$1', '$2', '', '']"); o=${o#(}; echo "${o%,)}"; }
actions() {
    local p; p=$(kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.getComponent "$1" | sed -n "s/^(objectpath '\(.*\)',)$/\1/p")
    [ -n "$p" ] && kga --object-path "$p" --method org.kde.kglobalaccel.Component.shortcutNames \
        | sed "s/^(@as \[\],)$//; s/^(\[//; s/\],)$//" | tr ',' '\n' | tr -d "' " | sed '/^$/d'
}
SP=org.kde.spectacle.desktop EL=com.ectonomic.ectolens.desktop
PREFIX=$HOME/.local/opt/ectolens STATE=$HOME/.local/state/ectolens-install
cd "$HOME/Ectolens"

# PipeWire and its session manager first: KWin's screen cast hands recordings over through them
pipewire >/tmp/pipewire.log 2>&1 &
sleep 0.5
wireplumber >/tmp/wireplumber.log 2>&1 &
/tmp/kwin_wayland --virtual --no-lockscreen --width 1280 --height 720 --socket "$WAYLAND_DISPLAY" >/tmp/kwin.log 2>&1 &
for _ in $(seq 100); do kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.allMainComponents | grep -q "'$SP'" && break; sleep 0.2; done
if ! kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.allMainComponents | grep -q "'$SP'"; then
    fail "the test session: KWin's shortcut service with Spectacle's shortcuts never came up"; tail -20 /tmp/kwin.log; exit 1
fi
. /etc/os-release; echo "system: $PRETTY_NAME, KWin $(ls /usr/lib64/libkwin.so.6.* /usr/lib/libkwin.so.6.* /usr/lib/*/libkwin.so.6.* 2>/dev/null | head -1 | sed 's/.*\.so\.//')"
# Spectacle's keys before anything happens, every action it has
for a in $(actions "$SP"); do printf '%s\t%s\n' "$a" "$(keys "$SP" "$a")"; done > /tmp/before
echo "Spectacle's shortcuts before:"; sed 's/^/  /' /tmp/before
snapshot() { find "$HOME" -path "$HOME/Ectolens" -prune -o -print 2>/dev/null | sort; }
snapshot > /tmp/home-before

if [ "$expect" = refused ]; then
    ./install.sh --yes > /tmp/install.log 2>&1; rc=$?
    cat /tmp/install.log
    [ "$rc" != 0 ] && grep -q "too old for Ectolens" /tmp/install.log \
        && pass "install.sh refuses this system and names what it lacks: $(grep -m1 -A0 '  - ' /tmp/install.log | sed 's/^ *- //')" \
        || fail "install.sh did not refuse a system older than the build's minimums (exit $rc)"
    [ ! -e "$PREFIX" ] && [ ! -e "$STATE" ] && pass "nothing was installed" || fail "something was installed anyway"
    snapshot > /tmp/home-after
    diff -q /tmp/home-before /tmp/home-after >/dev/null && pass "the home folder is as it was" \
        || { fail "the home folder changed:"; diff /tmp/home-before /tmp/home-after | head; }
    for a in $(cut -f1 /tmp/before); do [ "$(keys "$SP" "$a")" = "$(grep -P "^$a\t" /tmp/before | cut -f2)" ] || fail "Spectacle's $a changed"; done
    exit $bad
fi

# ---- folders an install must never take: uninstall.sh deletes the install folder whole
./install.sh --yes --prefix "$HOME/.local" > /tmp/refuse1.log 2>&1; rc1=$?
mkdir -p "$HOME/apps"; echo "the stranger's own file" > "$HOME/apps/notes.txt"
./install.sh --yes --prefix "$HOME/apps" > /tmp/refuse2.log 2>&1; rc2=$?
if [ "$rc1" != 0 ] && grep -q "session's own folders" /tmp/refuse1.log && [ "$rc2" != 0 ] && grep -q "aren't an Ectolens install" /tmp/refuse2.log \
   && [ "$(cat "$HOME/apps/notes.txt")" = "the stranger's own file" ] && [ ! -e "$STATE" ]; then
    pass "install.sh refuses ~/.local and a folder holding someone's files, and changes nothing"
else
    fail "install.sh into ~/.local (exit $rc1) or a folder with files (exit $rc2)"; tail -3 /tmp/refuse1.log /tmp/refuse2.log
fi
rm -rf "$HOME/apps"
snapshot > /tmp/home-before

# ---- install, as the README says
start=$(date +%s)
./install.sh --yes > /tmp/install.log 2>&1; rc=$?
sed -n '/^== /p;/: Plasma [0-9]/p;/packages from/,+2p;/Ectolens needs/,+3p;/shortcuts moved/p;/files linked/p' /tmp/install.log
[ "$rc" = 0 ] && pass "install.sh --yes finished ($(( $(date +%s) - start )) s, packages and build)" \
    || { fail "install.sh --yes failed (exit $rc)"; grep -nE "error:|Error|FAILED:|^E: |ectolens: |cannot find|undefined reference|Problem" /tmp/install.log | head -30
         echo "  ..."; tail -25 /tmp/install.log; exit 1; }

# every link it made leads to a file in the install
n=0; broken=0
while IFS= read -r l; do n=$((n+1)); [ -e "$l" ] || { broken=1; echo "  broken: $l"; }; done < "$STATE/links"
[ "$n" -gt 0 ] && [ "$broken" = 0 ] && pass "all $n links into ~/.local/share lead into the install" || fail "links: $n made, some broken"
sed "s|^$HOME/.local/share/|  linked: |" "$STATE/links"
# only Ectolens's own files: each link's name is Ectolens's, or an icon named for it
odd=$(sed "s|^$HOME/.local/share/||" "$STATE/links" | grep -viE 'ectolens' || true)
[ -z "$odd" ] && pass "every link is one of Ectolens's own files" || fail "links that aren't Ectolens's own: $odd"
d=$HOME/.local/share/applications/com.ectonomic.ectolens.desktop
exe=$(sed -n 's/^Exec=\([^ ]*\).*/\1/p' "$d" | head -1)
[ "$exe" = "$PREFIX/bin/ectolens" ] && [ -x "$exe" ] && pass "the menu entry starts $exe" || fail "the menu entry's Exec is $exe"

# the shortcuts: every key Spectacle had for an action Ectolens has is Ectolens's now
moved=$(wc -l < "$STATE/spectacle-shortcuts")
grep -qP '^_launch\t' "$STATE/spectacle-shortcuts" && [ "$moved" -ge 2 ] \
    && pass "$moved of Spectacle's shortcuts were saved, Print Screen's among them" || fail "saved shortcuts: $(cat "$STATE/spectacle-shortcuts")"
ok=1
while IFS=$'\t' read -r a k; do
    [ "$(keys "$EL" "$a")" = "$k" ] || { ok=0; echo "  Ectolens's $a is $(keys "$EL" "$a"), wanted $k"; }
    [ "$(keys "$SP" "$a")" = '@a(ai) []' ] || { ok=0; echo "  Spectacle still has $a: $(keys "$SP" "$a")"; }
done < "$STATE/spectacle-shortcuts"
[ "$ok" = 1 ] && pass "KWin's shortcut service gives those keys to Ectolens and none to Spectacle" || fail "the shortcuts didn't move"

# KWin lets the installed program take a screenshot (on Plasma 6.7 by its desktop file)
rm -f /tmp/shot.png
timeout 60 "$PREFIX/bin/ectolens" -b -n -f -o /tmp/shot.png >/tmp/shot.log 2>&1
size=$( [ -f /tmp/shot.png ] && od -An -tu1 -j16 -N8 /tmp/shot.png | awk '{print $1*16777216+$2*65536+$3*256+$4 "x" $5*16777216+$6*65536+$7*256+$8}')
[ "$size" = 1280x720 ] && pass "the installed Ectolens took a 1280x720 screenshot through KWin" \
    || { fail "screenshot: '${size:-none}'"; tail -5 /tmp/shot.log; }

# Print Screen, pressed through the shortcut service, starts Ectolens over D-Bus
p=$(kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.getComponent "$EL" | sed -n "s/^(objectpath '\(.*\)',)$/\1/p")
kga --object-path "$p" --method org.kde.kglobalaccel.Component.invokeShortcut _launch default >/dev/null
pid=
for _ in $(seq 75); do pid=$(pgrep -u stranger -x ectolens | head -1); [ -n "$pid" ] && break; sleep 0.2; done
if [ -n "$pid" ] && [ "$(readlink /proc/$pid/exe)" = "$PREFIX/bin/ectolens" ]; then
    pass "Print Screen's action starts the installed Ectolens"
    kill "$pid"; sleep 1
else
    fail "Print Screen's action started no Ectolens"
fi

touch /tmp/before-recording
# a recording through KWin's screen cast and KPipeWire, encoded with what this distribution's
# FFmpeg has: an area framed and Record pressed by Ectolens's own test script (the pipeline is the
# real one; Start/Stop Screen Recording would wait for a person to pick the screen), then the
# saved file read back from the save folder
ECTOLENS_TEST_SCRIPT="frame:200,150,800,450;wait:800;record;wait:5000;stop;wait:6000" QT_FORCE_STDERR_LOGGING=1 \
    "$PREFIX/bin/ectolens" -r >/tmp/record.log 2>&1 &
rec=$!
video= probe= dur=
for _ in $(seq 120); do
    # the recorder's file, moved out of Ectolens's temporary folder into the save folder by then
    name=$(sed -n 's/^ECTOLENS-SAVED //p' /tmp/record.log | head -1); name=${name##*/}
    [ -n "$name" ] && video=$(find "$HOME" -path "$HOME/Ectolens" -prune -o -name "$name" -newer /tmp/before-recording -print 2>/dev/null | head -1)
    [ -n "$video" ] && [ -s "$video" ] && break
    kill -0 "$rec" 2>/dev/null || break
    sleep 0.25
done
if [ -n "$video" ] && [ -s "$video" ]; then
    sleep 0.5
    probe=$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height:format=duration -of default=nw=1 "$video" 2>&1 | tr '\n' ' ')
    dur=$(sed -n 's/.*duration=\([0-9.]*\).*/\1/p' <<<"$probe")
    grep -qa "x264 - core" "$video" && by=x264 || by="not x264"
fi
if [ -n "$dur" ] && awk -v d="$dur" 'BEGIN{exit !(d >= 4)}' && grep -q "codec_name=h264" <<<"$probe"; then
    pass "a 5-second recording of an area was saved and plays back: $probe(${video#"$HOME"/}, encoded by $by)"
else
    fail "recording: file '${video:-none}', ffprobe '${probe:-nothing}'"
    grep -vE "OCR|Tesseract|tessdata|locale|UTF-8|languages|pts:" /tmp/record.log | tail -15 | sed 's/^/  record.log: /'
fi
grep -i "could not be opened" /tmp/record.log | sed 's/^/  /'
kill "$rec" 2>/dev/null; sleep 1

# the MCP server answers an agent's first message
r=$(printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"stranger","version":"1"}}}' \
    | timeout 10 "$PREFIX/bin/ectolens-mcp" 2>/dev/null | head -1)
grep -q '"serverInfo"' <<<"$r" && pass "ectolens-mcp answers initialize" || fail "ectolens-mcp: $r"

# a move cut short (Print Screen's keys saved, not yet moved): the next install.sh finishes it
cp "$STATE/spectacle-shortcuts" /tmp/saved1
launch=$(grep -P '^_launch\t' /tmp/saved1 | cut -f2)
gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel --method org.kde.KGlobalAccel.setForeignShortcutKeys "['$EL', '_launch', '', '']" '@a(ai) []' >/dev/null
gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel --method org.kde.KGlobalAccel.setForeignShortcutKeys "['$SP', '_launch', '', '']" "$launch" >/dev/null
touch "$STATE/moving"
./install.sh --yes > /tmp/install2.log 2>&1; rc=$?
[ "$rc" = 0 ] && cmp -s /tmp/saved1 "$STATE/spectacle-shortcuts" && [ ! -e "$STATE/moving" ] \
    && [ "$(keys "$EL" _launch)" = "$launch" ] && [ "$(keys "$SP" _launch)" = '@a(ai) []' ] \
    && pass "after a move cut short, the next install.sh finishes it and keeps the saved shortcuts" || { fail "install.sh after a move cut short (exit $rc)"; tail -20 /tmp/install2.log; }
# and running it again updates in place and leaves the shortcuts as they are: one the
# person cleared since (another saved action than Print Screen's) stays cleared
other=$(grep -vP '^_launch\t' /tmp/saved1 | head -1 | cut -f1)
gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel --method org.kde.KGlobalAccel.setForeignShortcutKeys "['$EL', '$other', '', '']" '@a(ai) []' >/dev/null
./install.sh --yes > /tmp/install3.log 2>&1; rc=$?
[ "$rc" = 0 ] && cmp -s /tmp/saved1 "$STATE/spectacle-shortcuts" && grep -q "left as they are" /tmp/install3.log \
    && [ "$(keys "$EL" "$other")" = '@a(ai) []' ] && [ "$(keys "$EL" _launch)" = "$launch" ] \
    && pass "a third install.sh updates in place and leaves a shortcut the person cleared cleared" || { fail "third install.sh (exit $rc)"; tail -20 /tmp/install3.log; }
# its keys back, so the uninstall below has the whole record to hand back
gdbus call --session --dest org.kde.kglobalaccel --object-path /kglobalaccel --method org.kde.KGlobalAccel.setForeignShortcutKeys "['$EL', '$other', '', '']" "$(grep -P "^$other\t" /tmp/saved1 | cut -f2)" >/dev/null

# uninstall.sh deletes only a folder that carries install.sh's mark
mv "$PREFIX/share/ectolens-install/installed-by" /tmp/marker
"$PREFIX/share/ectolens-install/uninstall.sh" --yes > /tmp/uninstall0.log 2>&1; rc=$?
[ "$rc" != 0 ] && grep -q "doesn't carry install.sh's mark" /tmp/uninstall0.log && [ -x "$PREFIX/bin/ectolens" ] && [ "$(keys "$EL" _launch)" = "$launch" ] \
    && pass "uninstall.sh refuses a folder without install.sh's mark and changes nothing" || { fail "uninstall.sh without the mark (exit $rc)"; cat /tmp/uninstall0.log; }
mv /tmp/marker "$PREFIX/share/ectolens-install/installed-by"

# ---- uninstall, from the copy the install carries
"$PREFIX/share/ectolens-install/uninstall.sh" --yes > /tmp/uninstall.log 2>&1; rc=$?
cat /tmp/uninstall.log | sed 's/^/  | /'
[ "$rc" = 0 ] && pass "uninstall.sh --yes finished" || fail "uninstall.sh failed (exit $rc)"
ok=1
while IFS=$'\t' read -r a k; do [ "$(keys "$SP" "$a")" = "$k" ] || { ok=0; echo "  Spectacle's $a is $(keys "$SP" "$a"), was $k"; }; done < /tmp/before
[ "$ok" = 1 ] && pass "Spectacle has every shortcut it had before the install" || fail "Spectacle's shortcuts aren't back"
[ ! -e "$PREFIX" ] && [ ! -e "$STATE" ] && pass "the install folder and its records are gone" || fail "left behind: $(ls -d "$PREFIX" "$STATE" 2>/dev/null)"
left=$(find "$HOME/.local" -lname "$PREFIX/*" 2>/dev/null; find -L "$HOME/.local" -type l 2>/dev/null)
[ -z "$left" ] && pass "no link into the install is left in ~/.local" || { fail "links left:"; echo "$left"; }
gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.ListActivatableNames \
    | grep -qi ectolens && fail "D-Bus can still start Ectolens" || pass "D-Bus no longer knows how to start Ectolens"
# the shortcut service itself, and its saved settings, forget Ectolens's actions
left_actions=$(actions "$EL" | tr '\n' ' ')
if [ -n "$left_actions" ] || grep -q "^\[$EL\]" "$HOME/.config/kglobalshortcutsrc" 2>/dev/null; then
    fail "the shortcut service still has Ectolens's actions: ${left_actions:-none}; settings: $(sed -n "/^\[$EL\]/,/^\[/p" "$HOME/.config/kglobalshortcutsrc" 2>/dev/null | head -3 | tr '\n' ' ')"
else
    pass "the shortcut service and its settings no longer list Ectolens"
fi
# what is left in the home folder: only Ectolens's own settings (kept unless asked) and KDE's caches
snapshot > /tmp/home-after
extra=$(comm -13 /tmp/home-before /tmp/home-after | grep -i ectolens | grep -v "/.config/ectolensrc$" || true)
[ -z "$extra" ] && pass "nothing named Ectolens is left in the home folder but its settings" || { fail "left in the home folder:"; echo "$extra"; }
exit $bad
