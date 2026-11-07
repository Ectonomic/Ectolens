# The README's pictures, filmed in run.sh's private KWin: sourced by inner.sh for SCENARIO=readme-NAME.
# Each scene is driven as a person would drive it, with the real pointer and keys (fake input), and
# filmed by film.py through KWin's own screenshots. tools/readme-pictures.sh runs every scene in
# SCENES and tools/readme-pictures.py cuts the films into docs/pictures/ (its CUTS name the pictures).
# Nothing private is on screen: KDE's Waterfall wallpaper, KDE's Breeze Dark colors, made-up
# words in the windows. Every scene runs on one virtual screen of 1280×720 at scale 2 (2560×1440
# pixels, tools/readme-pictures.sh sets it), so the pictures stay sharp on a dense display.

# the scenes, in the order the README shows them
SCENES="hero marks private size tools menus window light ectolook kdelook settings spectacle agent"

README_SCENE=${SCENARIO#readme-}
R_PX=1180; R_PY=660   # where the pointer is
# the terminal's place, and the frame around it in the scenes that frame it from the script
KONSOLE="70, 150, 660, 400"; FRAME="70,150,660,400"
[ "$README_SCENE" = size ] && { KONSOLE="70, 90, 660, 400"; FRAME="70,90,660,400"; }

film() { echo "$*" > "$W/film.fifo"; }
film_wait() { for i in $(seq 200); do [ -e "$OUT/film/$1" ] && return 0; sleep 0.05; done; echo "film: $1 never came" >> "$OUT/readme.log"; }
film_end() { film stop; film_wait stopped; touch "$W/readme-done"; }
# a moment in the film, by the wall clock (film.py's start.txt says when the film began)
film_mark() { echo "$1 $(date +%s.%N)" >> "$OUT/film/marks-wall.txt"; }
# the pointer travels as a hand moves it: eased, on a slight curve, a step every 16 ms
glide() {
  local x1=$1 y1=$2 ms=${3:-500} n x y
  n=$((ms / 16)); [ "$n" -lt 2 ] && n=2
  while read -r x y; do feed "move $x $y"; feed "sleep 16"; done < <(awk -v x0="$R_PX" -v y0="$R_PY" -v x1="$x1" -v y1="$y1" -v n="$n" 'BEGIN {
    dx = x1 - x0; dy = y1 - y0; bend = 0.08
    for (i = 1; i <= n; i++) { t = i / n; s = t * t * (3 - 2 * t); b = sin(3.14159265 * t) * bend
      printf "%.1f %.1f\n", x0 + dx * s - dy * b, y0 + dy * s + dx * b } }')
  R_PX=$x1; R_PY=$y1
}
press() { feed "down"; feed "sleep 70"; feed "up"; }
hold() { feed "sleep $1"; }
# a drag: press here, travel, let go
drag_to() { feed "down"; feed "sleep 90"; glide "$1" "$2" "${3:-600}"; feed "sleep 90"; feed "up"; }
# where the overlay said a control is (locate:NAME in the scene's script)
at() { grep "^ECTOLENS-AT $1 " "$OUT/ectolens.log" | tail -1 | cut -d' ' -f3-; }
glide_to() { local x y; read -r x y < <(at "$1"); [ -n "$x" ] || { echo "no place for $1" >> "$OUT/readme.log"; return 1; }; glide "$x" "$y" "${2:-550}"; }
# words typed as real keys, capitals and the usual signs too (Linux key codes, a US layout)
declare -A KEYCODE=([a]=30 [b]=48 [c]=46 [d]=32 [e]=18 [f]=33 [g]=34 [h]=35 [i]=23 [j]=36 [k]=37 [l]=38 [m]=50
  [n]=49 [o]=24 [p]=25 [q]=16 [r]=19 [s]=31 [t]=20 [u]=22 [v]=47 [w]=17 [x]=45 [y]=21 [z]=44 [1]=2 [2]=3 [3]=4
  [4]=5 [5]=6 [6]=7 [7]=8 [8]=9 [9]=10 [0]=11 [-]=12 [=]=13 [.]=52 [,]=51 [/]=53 [\;]=39 ["'"]=40 [" "]=57)
type_text() {
  local s=$1 i c
  for ((i = 0; i < ${#s}; i++)); do
    c=${s:i:1}
    if [[ $c == [A-Z] ]]; then feed "press 42"; feed "key ${KEYCODE[${c,,}]}"; feed "release 42"
    elif [ "$c" = "?" ]; then feed "press 42"; feed "key 53"; feed "release 42"
    elif [ "$c" = ":" ]; then feed "press 42"; feed "key 39"; feed "release 42"
    elif [ -n "${KEYCODE[$c]:-}" ]; then feed "key ${KEYCODE[$c]}"
    fi
    feed "sleep $((15 + RANDOM % 30))"
  done
}
# where KWin put the Ectolens window's inside: CX CY CW CH
window_client() {
  cat > "$W/rwhere.js" <<'JS'
for (const w of workspace.windowList()) {
    if (w.resourceClass.indexOf("ectolens") >= 0 && w.normalWindow) {
        print("ECTOLENS-CLIENT " + Math.round(w.clientGeometry.x) + " " + Math.round(w.clientGeometry.y) + " " + Math.round(w.clientGeometry.width) + " " + Math.round(w.clientGeometry.height));
    }
}
JS
  qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$W/rwhere.js" ectolens-rwhere-$RANDOM > /dev/null 2>&1
  qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.start > /dev/null 2>&1; sleep 0.4
  read -r CX CY CW CH < <(grep -o "ECTOLENS-CLIENT .*" "$OUT/kwin.log" | tail -1 | cut -d' ' -f2-)
  echo "window client $CX $CY $CW $CH" >> "$OUT/readme.log"
}

readme_setup() {
  # KDE's own Breeze Dark, not this computer's colors and fonts (light: Breeze Light, below)
  cp /usr/share/color-schemes/BreezeDark.colors "$XDG_CONFIG_HOME/kdeglobals"
  # what the terminal shows
  case "$README_SCENE" in
    private|agent)
      printf '%s\n' '$ git config user.email' 'dana.reyes@example.com' '$ echo $OPENAI_API_KEY' \
        'sk-proj-9fK2xQ7mL4vR8tY1wZ3bN6cD5hJ0' '$ pwd' '/home/dana/projects/launch-notes' '$ ls' 'notes.md  slides.key  budget.ods' '$ ' > "$W/term.txt";;
    marks|window|light|ectolook|kdelook|spectacle*)
      printf '%s\n' '$ git clone --recurse-submodules \' '      https://github.com/Ectonomic/Ectolens.git' '$ cd Ectolens' \
        '$ cmake -S 3rdparty/kpipewire -B build/kpipewire -G Ninja' '$ cmake --build build/kpipewire && cmake --install build/kpipewire' \
        '$ cmake -S . -B build/spectacle -G Ninja' '$ cmake --build build/spectacle' '$ ' > "$W/term.txt";;
  esac
  if [[ " tools size menus settings " == *" $README_SCENE "* ]]; then
    { printf '%s\n' '$ tests/e2e/record-synthetic.sh build/spectacle/bin out high 60 60 3'
      for l in 'recorder finished cleanly' 'a file was written' 'the file plays' 'frame rate holds 60 per second' \
        'encoded as h264 by libx264' 'quality level is 16.0' 'a keyframe every 2 seconds' 'standard color (4:2:0)'; do
        printf '\e[30;42m PASS \e[0m %s\n' "$l"; done; printf '$ \n'; } > "$W/term.txt"
  fi
  if [ "$README_SCENE" = hero ]; then
    # the command is typed; its lines come while the scene records (term-go)
    cat > "$W/term.sh" <<SH
#!/bin/bash
printf '%s\n' '\$ tests/e2e/record-synthetic.sh build/spectacle/bin out high 60 60 3'
while [ ! -e '$W/term-go' ]; do sleep 0.05; done
sleep 0.5
while IFS= read -r line; do printf '\e[30;42m PASS \e[0m %s\n' "\$line"; sleep 0.3; done <<'LINES'
recorder finished cleanly
a file was written
the file plays
frame rate holds 60 per second
encoded as h264 by libx264
quality level is 16.0
a keyframe every 2 seconds
standard color (4:2:0)
LINES
printf '\$ '
while read -r _; do :; done
SH
  else
    printf '#!/bin/bash\ncat %q\nwhile read -r _; do :; done\n' "$W/term.txt" > "$W/term.sh"
  fi
  TERM_CMD="bash '$W/term.sh'"
  # a document in the editor beside the terminal
  mkdir -p "$HOME/notes"
  printf '%s\n' '# Launch notes' '' '## Before the demo' '- Record the setup at 60 fps' '- Blur the key on slide 4' \
    '- Number the steps in the screenshots' '' '## After' '- Send the recording to the team' > "$HOME/notes/launch-notes.md"
  printf '[General]\nShow Url Nav Bar=false\nShow welcome view for new window=false\n[Kate Plugins]\nlspclientplugin=false\n' > "$XDG_CONFIG_HOME/katerc"
  # Ectolens's settings for the scene
  case "$README_SCENE" in
    hero|window|light) printf '[General]\nafterCaptureOpen=true\n' >> "$XDG_CONFIG_HOME/ectolensrc";;
    kdelook) printf '[Appearance]\nlook=KdeLook\naccent=KdeAccent\n' >> "$XDG_CONFIG_HOME/ectolensrc";;
    agent) printf '[General]\nagentAccess=true\n' >> "$XDG_CONFIG_HOME/ectolensrc";;
    # both sources on, on separate tracks: the Sound section's picture shows the choice in use
    settings) printf '[GuiConfig]\nvideoRecordSystemAudio=true\nvideoRecordMicrophone=true\nvideoSeparateAudioTracks=true\n' >> "$XDG_CONFIG_HOME/ectolensrc";;
  esac
  # Breeze Light: the light window, and the KDE look as Plasma first draws it
  case "$README_SCENE" in light|kdelook) export TEST_LIGHT=1;; esac
  # the film: KWin grants its screenshots by desktop file, so this sandbox's Python gets one
  mkfifo "$W/film.fifo"
  printf '[Desktop Entry]\nType=Application\nName=Ectolens README film\nExec=%s\nNoDisplay=true\nX-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2\n' \
    "$(python3 -I -c 'import os, sys; print(os.path.realpath(sys.executable))')" > "$XDG_DATA_HOME/applications/ectolens-readme-film.desktop"
  mkdir -p "$OUT/film"
  python3 -I "$HERE/film.py" "$W/film.fifo" "$OUT" 2>> "$OUT/film.err" &
  FILM=$!
}

readme_windows() {
  kate -n "$HOME/notes/launch-notes.md" > /dev/null 2>&1 &
  DLG=$!
  [ "$README_SCENE" = tools ] && { /usr/lib64/qt6/bin/qml "$HERE/qr.qml" > "$OUT/qr.log" 2>&1 & }
}

readme_place() {
  cat > "$W/rplace.js" <<JS
for (const w of workspace.windowList()) {
    if (w.resourceClass.indexOf("konsole") >= 0) { w.frameGeometry = {x: ${KONSOLE%%,*}, y: $(echo "$KONSOLE" | cut -d, -f2), width: $(echo "$KONSOLE" | cut -d, -f3), height: $(echo "$KONSOLE" | cut -d, -f4)}; print("ECTOLENS-PLACED konsole"); }
    if (w.resourceClass.indexOf("kate") >= 0 && w.normalWindow) { w.frameGeometry = {x: 770, y: 196, width: 440, height: 320}; print("ECTOLENS-PLACED kate"); }
    if (w.caption.indexOf("QR test") >= 0) { w.frameGeometry = {x: 520, y: 330, width: 170, height: 170}; w.keepAbove = true; print("ECTOLENS-PLACED qr"); }
}
JS
  for i in $(seq 30); do
    qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$W/rplace.js" ectolens-rplace-$i > /dev/null 2>&1
    qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.start > /dev/null 2>&1
    sleep 0.5
    grep -q "ECTOLENS-PLACED konsole" "$OUT/kwin.log" && grep -q "ECTOLENS-PLACED kate" "$OUT/kwin.log" \
      && { [ "$README_SCENE" != tools ] || grep -q "ECTOLENS-PLACED qr" "$OUT/kwin.log"; } && break
  done
  sleep 1.5
}

readme_script() {
  case "$README_SCENE" in
    # Print Screen, a window pointed at and taken, Record, Stop, and the recording played in the window
    hero) SCRIPT="mark:start;mark:pointing;locate:record;mark:framed;wait:1400;locate:stop;mark:recording;wait:1800;mark:window";;
    # numbered steps, a label and a text box, with the real pointer and keys
    marks) SCRIPT="frame:$FRAME;wait:700;locate:numberTool;locate:textTool;locate:selectTool;mark:framed";;
    private) SCRIPT="frame:$FRAME;wait:700;locate:hidePrivateInfo;mark:framed";;
    # the frame label's sizes: Full HD picked, the window made that size
    size) SCRIPT="mark:start;mark:pointing;locate:frameLabel;mark:framed;locate:sizePreset1920x1080;mark:menu";;
    # Pick Color, Measure, the QR code's own button, Pin
    tools) SCRIPT="frame:$FRAME;wait:900;locate:pickColor;locate:measure;locate:pin;locate:openQrArrow;mark:framed";;
    # After Capture (Capture's arrow) and Record's choices (Record's arrow)
    menus) SCRIPT="frame:$FRAME;wait:700;locate:captureArrow;locate:recordArrow;locate:textTool;mark:framed;locate:optionColor;locate:optionFont;mark:options";;
    # a marked picture in the Ectolens window, dark and light
    window|light) SCRIPT="frame:$FRAME;wait:700;number:474,252;number:634,306;number:350,342;text:510,252,Run these in order;deselect;wait:400;mark:framed;wait:2600;mark:window";;
    # the overlay in Ecto's look and in your KDE colors and fonts, the same marks drawn
    ectolook|kdelook) SCRIPT="frame:$FRAME;wait:700;number:474,252;number:634,306;number:350,342;text:510,252,Run these in order;deselect;tool:NumberTool;wait:600;mark:framed";;
    settings|spectacle*) SCRIPT="mark:framed";;
    # an AI agent asks through ectolens-mcp (a made-up agent, agent-client.py): the overlay asking, then Esc
    agent) SCRIPT="mark:framed";;
  esac
}

readme_before_activate() {
  feed "move $R_PX $R_PY"
  case "$README_SCENE" in
    agent)
      # the made-up agent asks for the terminal; the overlay opens on it
      feed "move 1150 640"; R_PX=1150; R_PY=640; film start agent 25; hold 600
      AGENT_TITLE="Claude Code" python3 -I "$HERE/agent-client.py" "$BIN/ectolens-mcp" "$OUT" one > "$OUT/agent-client.log" 2>&1 &
      for i in $(seq 300); do grep -q "^ECTOLENS-AGENT framed" "$OUT/ectolens.log" && break; sleep 0.1; done;;
    spectacle)
      # KDE's Spectacle, as this computer has it, on the same desktop: where its controls are
      film start "$README_SCENE" 10
      spectacle -r > "$OUT/spectacle.log" 2>&1 &
      SP=$!
      sleep 3; film_mark unframed
      feed "move 70 150"; R_PX=70; R_PY=150; feed "down"; glide 730 550 900; feed "up"; hold 1500; film_mark framed
      hold 300
      # its Number tool, three numbers where Ectolens's go, then New Recording's menu, then Accept
      glide 663 112 600; press; hold 700; film_mark numbertool
      glide 474 252 500; press; hold 300; glide 634 306 400; press; hold 300; glide 350 342 400; press; hold 900; film_mark numbers; hold 200
      glide 723 690 600; press; hold 1200; film_mark recordmenu; feed "key 1"; hold 600; film_mark afteresc
      glide 160 588 600; press; hold 3500; film_mark window
      hold 300; film_end;;
    hero) film start hero 25; glide 1060 640 700; hold 500; film_mark printscreen;;
    settings)
      # Settings' Recording and Appearance pages, as the settings picture hook shows them, each held for the film
      film start settings 10
      for page in recording sound appearance; do
        ECTOLENS_SETTINGS_SHOT="$OUT/settings-$page.png" ECTOLENS_SETTINGS_SHOT_PAGE=$page ECTOLENS_SETTINGS_SHOT_HOLD=3000 \
          ECTOLENS_SETTINGS_SHOT_WIDTH=1000 ECTOLENS_SETTINGS_SHOT_HEIGHT=680 ECTOLENS_NO_GPU_PROBE=1 "$BIN/ectolens" > "$OUT/settings-$page.log" 2>&1 &
        sleep 2.2; film_mark "$page"; wait $!
      done;;
  esac
}

readme_step() {
  case "$README_SCENE:$1" in
    hero:start)     hold 700; glide 990 360 600; hold 800; glide 400 350 700; hold 600; film_mark pointing; hold 200;;
    hero:pointing)  press; hold 600; film_mark framed; hold 200;;
    hero:framed)    glide_to record 650; hold 200; press; touch "$W/term-go";;
    hero:recording) hold 1400; glide_to stop 600; hold 250; press;;
    hero:window)    window_client; hold 500; glide $((CX + CW / 2)) $((CY + CH / 2)) 700; hold 250; press; hold 3800; film_end;;

    marks:framed)
      feed "move 560 610"; R_PX=560; R_PY=610; film start marks 25; hold 300; film_mark framed; hold 300
      glide_to numberTool 650; hold 150; press; hold 500
      glide 474 252 600; hold 150; press; hold 450     # 1: the clone
      glide 634 306 500; hold 150; press; hold 450     # 2: KPipeWire
      glide 350 342 500; hold 150; press; hold 600; film_mark numbers     # 3: Ectolens
      glide_to textTool 650; hold 150; press; hold 450
      glide 510 252 450; hold 150; press; hold 400; type_text "Run these in order"; hold 700
      film_mark box
      glide 112 400 650; hold 200; drag_to 452 448 700; hold 400
      type_text "KPipeWire goes into build/prefix, not over your system's copy."; hold 800
      glide_to selectTool 700; hold 150; press; hold 2000; film_end;;

    private:framed)
      feed "move 520 500"; R_PX=520; R_PY=500; film start private 25; hold 700
      glide_to hidePrivateInfo 700; hold 1500; press; hold 250; glide 470 470 700; hold 4500; film_end;;

    size:start)     feed "move 520 420"; R_PX=520; R_PY=420; hold 400;;
    size:pointing)  press; hold 900;;
    size:framed)
      film start size 25; hold 700
      glide_to frameLabel 650; hold 200; press; hold 300;;
    size:menu)
      hold 500; glide_to sizePreset1920x1080 750; hold 450; press; hold 3200; film_end;;

    tools:framed)
      feed "move 560 610"; R_PX=560; R_PY=610; film start tools 25; hold 500
      glide_to pickColor 650; hold 150; press; hold 300; glide 100 252 700; hold 900; film_mark pickcolor; hold 400; press; hold 1200; film_mark picked; hold 400
      glide_to measure 700; hold 150; press; hold 300; glide 150 300 600; hold 200; drag_to 470 420 800; hold 900; film_mark measure; hold 400
      feed "key 1"; hold 500
      glide_to openQrArrow 700; hold 150; press; hold 900; film_mark qrmenu; hold 400; feed "key 1"; hold 400
      glide_to pin 700; hold 150; press; hold 1200; glide 330 330 600; hold 300; drag_to 650 400 900; hold 900; film_mark pinned; hold 300; film_end;;

    menus:framed)
      feed "move 560 610"; R_PX=560; R_PY=610; film start menus 25; hold 500
      glide_to captureArrow 650; hold 150; press; hold 1200; film_mark aftermenu; hold 400; feed "key 1"; hold 500
      glide_to recordArrow 600; hold 150; press; hold 1200; film_mark recordmenu; hold 400; feed "key 1"; hold 400
      glide_to textTool 600; hold 150; press; hold 500;;
    menus:options)
      glide_to optionColor 600; hold 150; press; hold 1100; film_mark colormenu; hold 400; feed "key 1"; hold 400
      glide_to optionFont 500; hold 150; press; hold 1200; film_mark fontmenu; hold 400; feed "key 1"; hold 300; film_end;;

    window:framed|light:framed) film start "$README_SCENE" 10; feed "key 28";;
    window:window|light:window) window_client; glide $((CX + CW - 60)) $((CY + CH - 90)) 400; hold 1200; film_mark window; hold 200; film_end;;

    ectolook:framed|kdelook:framed) feed "move 560 470"; R_PX=560; R_PY=470; film start "$README_SCENE" 10; hold 1200; film_mark look; hold 200; film_end;;
    settings:framed) film_end;;
    agent:framed)
      # the frame first, then the dialog pops up; then Esc, as a person refusing
      for i in $(seq 300); do grep -q "^ECTOLENS-HIDDEN" "$OUT/ectolens.log" && break; sleep 0.1; done
      hold 1200; film_mark asking; hold 1500
      feed "key 1"; hold 800; film_end;;
  esac
}

readme_end() {
  [ -n "${SP:-}" ] && kill "$SP" 2>/dev/null
  film quit; sleep 0.5
}
