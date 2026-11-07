#!/bin/bash
# Ectolens off-screen overlay test. A headless KWin (virtual screen, private D-Bus,
# config in a throwaway folder, the NVIDIA card hidden so Mesa on the AMD iGPU
# draws it) runs a test window and Ectolens in region mode. Ectolens's test script
# frames an area, presses Record, pauses, resumes and stops; KWin's own screenshots
# of the virtual screen are saved at each step. Nothing opens on the desktop and
# the real screen is never captured. Adapted from a KWin replay harness for HDR patterns.
# usage: [SCENARIO=frame|launch|pick|input|settings|desktops] tests/e2e/overlay/run.sh INSTALLED_BIN_DIR OUT_DIR   (build/prefix/bin, after cmake --install)
#   startup: Ectolens started cold by D-Bus five times, as Print Screen starts it, while KWin's answers are timed (a frozen desktop).
#   desktops: a window on another virtual desktop must not be outlined or taken.
#   settings: Record's menu > Recording Settings… must show the dialog above the overlay.
#   input: started as Print Screen does, then real pointer and keys: drag a frame, R, click Stop.
#   frame: frame an area, Record, Pause, Resume, Stop.  launch: the same, started the way Print Screen starts it (D-Bus).  pick: point at a window and the desktop, click the window, Record, Stop.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$(realpath "$1"); OUT=$(realpath -m "$2"); export FAKE_INPUT=$(realpath "$HERE/../../../build/spectacle/bin/ectolens-fake-input"); export COMPOSITOR_WATCH=$(realpath "$HERE/../../../build/spectacle/bin/ectolens-compositor-watch"); mkdir -p "$OUT"; rm -f "$OUT"/*.png "$OUT"/*.log
W=$(mktemp -d /tmp/ectolens-ov.XXXXXX); chmod 700 "$W"; mkdir -p "$W/rt" "$W/ack" "$W/services" "$W/home" "$W/xdg"/{config,data,cache,state}; chmod 700 "$W/rt"
trap 'rm -rf "$W"' EXIT
cat > "$W/session.conf" <<CONF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><listen>unix:dir=$W/rt</listen><auth>EXTERNAL</auth>
<servicedir>$W/services</servicedir><policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
CONF
# TEST_GPU=nvidia: KWin draws on the NVIDIA card, as on the real desktop (the card stays visible)
DRI=(); [ "${TEST_GPU:-}" = nvidia ] || for n in /sys/class/drm/renderD* /sys/class/drm/card[0-9]; do [ "$(cat $n/device/vendor 2>/dev/null)" = 0x10de ] || DRI+=(--dev-bind /dev/dri/$(basename $n) /dev/dri/$(basename $n)); done
HIDE=(--tmpfs /dev/dri "${DRI[@]}"); [ "${TEST_GPU:-}" = nvidia ] && HIDE=()
# soundtracks: no real sound device and no way to the real session's PipeWire (standin-sound.sh)
[ "${SCENARIO:-frame}" = soundtracks ] && HIDE+=(--tmpfs /dev/snd --tmpfs "/run/user/$(id -u)")
# its own process namespace: every program the test starts ends with it, even after a timeout
# (2026-11-02: a timed-out run left a test KWin, Konsole and Ectolens running for hours)
bwrap --dev-bind / / --unshare-pid --proc /proc --die-with-parent "${HIDE[@]}" -- dbus-run-session --config-file="$W/session.conf" -- "$HERE/inner.sh" "$W" "$BIN" "$OUT"
# readme-*: the README's pictures; tools/readme-pictures.sh checks the film and cuts it
case "${SCENARIO:-frame}" in readme-*) exit 0;; esac
# both judges run whatever the first says (set -e would stop at its failure)
checked=0; python3 -I "$HERE/check.py" "$OUT" "${SCENARIO:-frame}" || checked=$?
# every row and bar reported by the `anatomy` test command, held to the one set of spacing rules
anatomy=0; python3 -I "$HERE/anatomy.py" "$OUT" || anatomy=$?
exit $(( checked || anatomy ))
