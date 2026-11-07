#!/bin/bash
# Encode the test screen with one setting and report size, speed and SSIM (in RGB, what you see).
# usage: run.sh NAME FPS EXT -- encoder args...
. ./src.sh
name=$1 fps=$2 ext=$3; shift 4
out=out/$name.$ext
start=$(date +%s.%N)
nice -n 19 ffmpeg -hide_banner -v error -y $(src_inputs $fps) -filter_complex "$SRC_GRAPH" -an "$@" "$out" || { echo "$name FAILED"; exit 1; }
end=$(date +%s.%N)
secs=$(echo "$end - $start" | bc -l)
bytes=$(stat -c %s "$out")
ssim=$(nice -n 19 ffmpeg -hide_banner -v info $(src_inputs $fps) -i "$out" -filter_complex "$SRC_GRAPH[s];[2:v]format=rgb24[e];[e][s]ssim" -f null - 2>&1 | grep -oE "All:[0-9.]+ \([0-9.inf]+\)" | tail -1)
printf "%-14s fps=%-4s size=%7.2f MB  per-min=%7.1f MB  avg=%6.2f Mb/s  encode=%5.1fs (%.2fx realtime)  SSIM %s\n" \
  "$name" "$fps" "$(echo "$bytes/1000000" | bc -l)" "$(echo "$bytes*6/1000000" | bc -l)" "$(echo "$bytes*8/10/1000000" | bc -l)" "$secs" "$(echo "10/$secs" | bc -l)" "$ssim"
