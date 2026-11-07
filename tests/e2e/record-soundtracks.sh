#!/bin/bash
# Sound tracks end to end, with stand-in devices only: a private PipeWire and WirePlumber
# (their own runtime folder and D-Bus, WirePlumber's hardware monitors off, and /dev/snd
# hidden, so no real speaker or microphone can be reached or changed) with a stand-in
# speaker playing 440 Hz and a stand-in microphone hearing 880 Hz. ectolens-record-synthetic
# records the synthetic screen with both, and each sound track in the file is read back
# and its tones measured.
#
#   tests/e2e/record-soundtracks.sh BIN_DIR OUT mixed|separate
#     mixed:    one track, both tones
#     separate: track 1 both tones (the default track, named Mix), track 2 only 440 Hz
#               (System sound), track 3 only 880 Hz (Microphone)
set -euo pipefail
if [ "${1:-}" != inner ]; then
    BIN=$(realpath "$1"); OUT=$(realpath -m "$2"); MODE=${3:?mixed or separate}
    mkdir -p "$OUT"
    W=$(mktemp -d "${TMPDIR:-/tmp}/ectolens-sound.XXXXXX"); trap 'rm -rf "$W"' EXIT
    mkdir -p "$W/rt" "$W/config" "$W/state" "$W/home"; chmod 700 "$W/rt"
    cat > "$W/session.conf" <<CONF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><listen>unix:dir=$W/rt</listen><auth>EXTERNAL</auth>
<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
CONF
    # the real session's sockets are hidden too: only the private PipeWire can be reached
    exec env -i HOME="$W/home" PATH="$PATH" LANG=C.UTF-8 XDG_RUNTIME_DIR="$W/rt" XDG_CONFIG_HOME="$W/config" \
        XDG_STATE_HOME="$W/state" QT_QPA_PLATFORM=offscreen \
        bwrap --dev-bind / / --unshare-pid --proc /proc --die-with-parent --tmpfs /dev/snd \
        --tmpfs "/run/user/$(id -u)" --bind "$W" "$W" \
        -- dbus-run-session --config-file="$W/session.conf" -- bash "$0" inner "$W" "$BIN" "$OUT" "$MODE"
fi

W=$2 BIN=$3 OUT=$4 MODE=$5
FAILS=0
check() { if eval "$2"; then echo "PASS  $1"; else echo "FAIL  $1 ($3)"; FAILS=$((FAILS+1)); fi; }

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/standin-sound.sh"
start_standin_sound "$OUT" "$W" 12 || exit 1

coproc SCREEN { exec "$BIN/ectolens-screen-source" 640 360 60; }
read -t 10 -r NODE <&"${SCREEN[0]}" || { echo "FAIL  the test screen never appeared"; exit 1; }
FILE=$OUT/sound-$MODE.mp4; rm -f "$FILE"
AUDIO=system,mic; [ "$MODE" = separate ] && AUDIO=system,mic,separate
ECTOLENS_E2E_AUDIO=$AUDIO timeout 60 "$BIN/ectolens-record-synthetic" "$NODE" "$FILE" 4 60 high >"$OUT/recorder-$MODE.log" 2>&1
check "the recorder finished cleanly" '[ $? -eq 0 ] && [ -s "$FILE" ]' "see $OUT/recorder-$MODE.log"
[ -s "$FILE" ] || exit 1

# what the file holds, track by track (recorded for 4 s)
python3 -I "$HERE/soundtracks.py" "$FILE" "$MODE" 4 || FAILS=$((FAILS + 1))
kill $(jobs -p) 2>/dev/null || true
exit $FAILS
