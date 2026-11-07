#!/bin/bash
# Makes the README's pictures again: films every scene in tests/e2e/overlay/readme-scenes.sh in
# the off-screen KWin (nothing opens on your desktop, your screen is never captured), then cuts
# the films into docs/pictures/ with tools/readme-pictures.py. Run it after `cmake --install`
# into build/prefix, whenever a part the pictures show changes.
# usage: tools/readme-pictures.sh [SCENE ...]      (the films stay in build/readme-films/)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
FILMS=$ROOT/build/readme-films
SCENES=${*:-$(sed -n 's/^SCENES="\(.*\)"$/\1/p' tests/e2e/overlay/readme-scenes.sh)}
[ -x build/prefix/bin/ectolens ] || { echo "build and install into build/prefix first (README, Build)" >&2; exit 1; }
mkdir -p "$FILMS"
for scene in $SCENES; do
  echo "== $scene"
  rm -rf "${FILMS:?}/$scene"
  SCENARIO=readme-$scene TEST_WIDTH=2560 TEST_HEIGHT=1440 TEST_LAYOUT="output.Virtual-0.scale.2" \
    TOUR_WALLPAPER=$ROOT/design/mock/img/wallpaper.jpg timeout 240 tests/e2e/overlay/run.sh build/prefix/bin "$FILMS/$scene" \
    > "$FILMS/$scene.log" 2>&1 || { echo "$scene: the run failed (build/readme-films/$scene.log)" >&2; exit 1; }
  [ -e "$FILMS/$scene/film/stopped" ] || { echo "$scene: the film never stopped (build/readme-films/$scene/)" >&2; exit 1; }
  if [ -s "$FILMS/$scene/readme.log" ] && grep -q "no place\|never came" "$FILMS/$scene/readme.log"; then
    echo "$scene: $(grep -m1 "no place\|never came" "$FILMS/$scene/readme.log")" >&2; exit 1
  fi
done
python3 -I tools/readme-pictures.py "$FILMS" docs/pictures --scenes $SCENES
