#!/bin/bash
# Ectolens end-to-end recording check, with no screen involved.
# ectolens-screen-source stands in for KWin's screen cast (variable frame rate
# with a ceiling, monotonic timestamps, damage); Ectolens's recorder records it
# with a quality choice; ffprobe checks the file. Prints PASS/FAIL lines and
# exits non-zero on any failure.
# usage: tests/e2e/record-synthetic.sh BUILD_BIN_DIR OUT_DIR PRESET SOURCE_FPS CAP_FPS SECONDS [crash]
#   ECTOLENS_E2E_PAUSE="AT,LENGTH" pauses for LENGTH seconds at AT; the file must be LENGTH shorter.
#   ECTOLENS_E2E_SIZE="WxH" sets the source size (default 1280x720).
#   PRESET: small | high | best | none (KPipeWire's own defaults), optionally :ENCODER (high:libsvtav1)
set -u
BIN=$1 OUT=$2 PRESET=${3%%:*} SRCFPS=$4 CAP=$5 SECS=$6 CRASH=${7:-}
ENCODER=libx264; [[ $3 == *:* ]] && ENCODER=${3#*:}
mkdir -p "$OUT"
FAILS=0
check() { if eval "$2"; then echo "PASS  $1"; else echo "FAIL  $1 ($3)"; FAILS=$((FAILS+1)); fi; }

SIZE=${ECTOLENS_E2E_SIZE:-1280x720}
coproc SRC { exec "$BIN/ectolens-screen-source" "${SIZE%x*}" "${SIZE#*x}" "$SRCFPS"; }
SRCPID=$SRC_PID
trap 'kill $SRCPID 2>/dev/null' EXIT
read -t 10 -r NODE <&"${SRC[0]}" || { echo "FAIL  test source never appeared"; exit 1; }
FILE="$OUT/$PRESET-$ENCODER-$SRCFPS-$CAP${CRASH:+-crash}.mp4"; rm -f "$FILE"
# ECTOLENS_E2E_UNWRITABLE=1: a file that can't be written (its folder is missing): the
# recorder must say so and end, not crash (upstream KPipeWire wrote a trailer with no header
# and crashed, 2026-11-05)
if [ -n "${ECTOLENS_E2E_UNWRITABLE:-}" ]; then
  BAD=$OUT/no-such-folder/$PRESET.mp4; rm -rf "$OUT/no-such-folder"
  timeout 60 "$BIN/ectolens-record-synthetic" "$NODE" "$BAD" "$SECS" "$CAP" "$PRESET" "$ENCODER" >"$OUT/recorder.log" 2>&1; RC=$?
  check "the recorder said it couldn't write the file" 'grep -q "^error:" "$OUT/recorder.log"' "no error reported"
  check "and ended without crashing" '[ $RC -lt 128 ]' "exit $RC"
  exit $FAILS
fi
if [ -n "$CRASH" ]; then
  "$BIN/ectolens-record-synthetic" "$NODE" "$FILE" 60 "$CAP" "$PRESET" "$ENCODER" >"$OUT/recorder.log" 2>&1 &
  REC=$!; sleep "$SECS"; kill -9 $REC; wait $REC 2>/dev/null
else
  timeout 60 "$BIN/ectolens-record-synthetic" "$NODE" "$FILE" "$SECS" "$CAP" "$PRESET" "$ENCODER" >"$OUT/recorder.log" 2>&1
  check "recorder finished cleanly" '[ $? -eq 0 ]' "see $OUT/recorder.log"
fi
check "a file was written" '[ -s "$FILE" ]' "no file"
[ -s "$FILE" ] || exit 1

PROBE=$(ffprobe -v error -count_frames -show_entries stream=codec_name,profile,pix_fmt,nb_read_frames:format=duration -of default=nw=1 "$FILE" 2>&1)
FRAMES=$(sed -n 's/^nb_read_frames=//p' <<<"$PROBE"); DUR=$(sed -n 's/^duration=//p' <<<"$PROBE")
PROFILE=$(sed -n 's/^profile=//p' <<<"$PROBE"); PIX=$(sed -n 's/^pix_fmt=//p' <<<"$PROBE"); CODEC=$(sed -n 's/^codec_name=//p' <<<"$PROBE")
X264=$(strings "$FILE" | grep -m1 -o "x264 - core.*")
CRF=$(grep -oE "crf=[0-9.]+" <<<"$X264" | cut -d= -f2); KEYINT=$(grep -oE "keyint=[0-9]+" <<<"$X264" | cut -d= -f2)
echo "      $ENCODER: $CODEC, $FRAMES frames in ${DUR}s, $PROFILE, $PIX, crf $CRF, keyint $KEYINT"

DROPPED=$(sed -n 's/^dropped //p' "$OUT/recorder.log")
echo "      dropped frames reported: ${DROPPED:-none}"
if [ -n "${ECTOLENS_E2E_PAUSE:-}" ] && [ -z "$CRASH" ]; then
  PLEN=${ECTOLENS_E2E_PAUSE#*,}
  check "the ${PLEN}s pause is cut out of the file" "python3 -I -c 'import sys; sys.exit(0 if abs($DUR - ($SECS - $PLEN)) <= 0.35 else 1)'" "file is ${DUR}s for ${SECS}s with a ${PLEN}s pause"
fi
check "the file plays" '[[ "$FRAMES" =~ ^[0-9]+$ ]] && [ "$FRAMES" -gt 0 ]' "ffprobe read no frames"
if [ -z "$CRASH" ] && [[ "$FRAMES" =~ ^[0-9]+$ ]]; then
  EXPECT=$(( SRCFPS < CAP ? SRCFPS : CAP ))
  FPS=$(python3 -I -c "print(round($FRAMES/$DUR,1))")
  check "frame rate holds $EXPECT per second" "python3 -I -c 'import sys; sys.exit(0 if abs($FRAMES/$DUR - $EXPECT) <= $EXPECT*0.05 else 1)'" "got $FPS"
fi
case $ENCODER in
  libx264|h264_nvenc) WANTCODEC=h264;; libx265|hevc_nvenc) WANTCODEC=hevc;; libsvtav1|av1_nvenc) WANTCODEC=av1;;
  no_such_encoder) WANTCODEC=h264;;   # one this computer lacks: still recorded, in its own H.264
esac
check "encoded as $WANTCODEC by $ENCODER" '[ "$CODEC" = "$WANTCODEC" ]' "got $CODEC"
if [ "$ENCODER" = libx264 ]; then   # only x264 writes its settings into the file
  case $PRESET in
    small) WANT=23.0;; high) WANT=16.0;; best) WANT=12.0;; none) WANT=23.0;;
  esac
  check "quality level is $WANT" '[ "$CRF" = "$WANT" ]' "got $CRF"
  if [ "$PRESET" != none ]; then
    check "a keyframe every 2 seconds" '[ "$KEYINT" = "$((CAP*2))" ]' "got $KEYINT"
  fi
fi
if [ "$PRESET" = best ] && [ "$ENCODER" != libsvtav1 ] && [ "$ENCODER" != av1_nvenc ]; then
  check "full color detail (4:4:4)" '[ "$PIX" = yuv444p ]' "got $PIX, $PROFILE"
else
  check "standard color (4:2:0)" '[ "$PIX" = yuv420p ]' "got $PIX"
fi
exit $FAILS
