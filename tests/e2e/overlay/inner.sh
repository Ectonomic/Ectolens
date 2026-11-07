#!/bin/bash
# Inside run.sh's sandbox.
W=$1; BIN=$2; OUT=$3; HERE=$(cd "$(dirname "$0")" && pwd)
PREFIX_APPS=$(realpath "$BIN/../share/applications")
REAL_RT=/run/user/$(id -u)
REAL_CONFIG=${XDG_CONFIG_HOME:-$HOME/.config}   # read before HOME and XDG dirs point at the sandbox
export XDG_RUNTIME_DIR=$W/rt XDG_CONFIG_HOME=$W/xdg/config XDG_DATA_HOME=$W/xdg/data XDG_CACHE_HOME=$W/xdg/cache XDG_STATE_HOME=$W/xdg/state HOME=$W/home
unset WAYLAND_DISPLAY DISPLAY
if [ "${SCENARIO:-frame}" = soundtracks ]; then
  # Sound: a private PipeWire with a stand-in speaker and microphone, never the real ones
  # (run.sh hides /dev/snd and the real session's runtime folder for this scenario)
  . "$HERE/../standin-sound.sh"
  start_standin_sound "$OUT" "$W" 40 > "$OUT/standin-sound.log" 2>&1 || { cat "$OUT/standin-sound.log"; exit 1; }
else
  # KWin streams recordings through PipeWire: reach the real PipeWire daemon (a new node, nothing captured from the real screen).
  ln -s "$REAL_RT/pipewire-0" "$W/rt/pipewire-0"
fi
# KWin's real permission check stays on: Ectolens must be granted screenshots by its desktop file,
# as on the real desktop (a switched-off check hid a wrong app id on 2026-11-01).
[ "${TEST_GPU:-}" = nvidia ] || export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
# Ectolens's own icon, as the user install links it into ~/.local/share/icons; before KWin starts, so its icon loader sees it
mkdir -p "$XDG_DATA_HOME/icons" "$XDG_DATA_HOME/applications" "$XDG_DATA_HOME/knotifications6" && cp -r "$PREFIX_APPS/../icons/hicolor" "$XDG_DATA_HOME/icons/" && cp "$PREFIX_APPS/com.ectonomic.ectolens.desktop" "$XDG_DATA_HOME/applications/"
# its notifications' config, as the user install links it, so KDE's notifications reach the stand-in server
cp "$PREFIX_APPS/../knotifications6/ectolens.notifyrc" "$XDG_DATA_HOME/knotifications6/"
QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="kwin_scripting.debug=true;js.debug=true;qml.debug=true;default.debug=true" kwin_wayland --virtual --width ${TEST_WIDTH:-1600} --height ${TEST_HEIGHT:-900} --output-count ${TEST_OUTPUTS:-1} --socket ectolens --no-lockscreen --no-global-shortcuts --no-kactivities > "$OUT/kwin.log" 2>&1 &
KW=$!
for i in $(seq 100); do [ -S "$W/rt/ectolens" ] && break; sleep 0.1; done
# TEST_LAYOUT: kscreen-doctor arguments for the virtual screens (scales, places), like a two-screen desktop with a scaled screen
if [ -n "${TEST_LAYOUT:-}" ]; then
  sleep 1; WAYLAND_DISPLAY=ectolens kscreen-doctor $TEST_LAYOUT >> "$OUT/layout.log" 2>&1
  WAYLAND_DISPLAY=ectolens kscreen-doctor -o >> "$OUT/layout.log" 2>&1
fi
# KWin grants its screenshot and screen-recording interfaces by the app's desktop file,
# matched to the running binary: install the build's own desktop file and run the installed binary.
mkdir -p "$XDG_DATA_HOME/applications"
cp "$PREFIX_APPS/com.ectonomic.ectolens.desktop" "$XDG_DATA_HOME/applications/"
# Draw with the running user's own KDE theme (colors, icons, fonts) so the shots look like their desktop.
cp "$REAL_CONFIG/kdeglobals" "$XDG_CONFIG_HOME/" 2>/dev/null
export WAYLAND_DISPLAY=ectolens QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME=kde QT_QUICK_CONTROLS_STYLE=org.kde.desktop
# The test's own screenshots come from the installed Spectacle, which KWin permits by its own
# desktop file; Ectolens must earn its permission the same way.
shot() { spectacle --background --nonotify --fullscreen --output "$1" >> "$OUT/shots.log" 2>&1; }
case "${SCENARIO:-frame}" in tour|printkey|window|tools|share|video|toggle|annotate|toolclicks|textbox|textzoom|fullrecord|frameundo|agent|agentoff|readme-*) SCENE=1;; *) SCENE=;; esac
# readme-*: the README's pictures, filmed (readme-scenes.sh holds each scene)
README=; case "${SCENARIO:-frame}" in readme-*) README=1; . "$HERE/readme-scenes.sh";; esac
# a stand-in notification server, to read what Ectolens says after a capture
python3 -I "$HERE/notify-server.py" "$OUT/notifications.jsonl" > "$OUT/notify-server.log" 2>&1 &
NS=$!
if [ -n "$SCENE" ]; then
  /usr/lib64/qt6/bin/qml "$HERE/background.qml" -- "$TOUR_WALLPAPER" > "$OUT/background.log" 2>&1 &
  printf '%s\n' '$ tests/e2e/record-synthetic.sh build/spectacle/bin out high 60 60 3' \
    'PASS  recorder finished cleanly' 'PASS  a file was written' \
    '      libx264: h264, 189 frames in 3.16s, High, yuv420p, crf 16.0, keyint 120' \
    'PASS  the file plays' 'PASS  frame rate holds 60 per second' 'PASS  encoded as h264 by libx264' \
    'PASS  quality level is 16.0' 'PASS  a keyframe every 2 seconds' 'PASS  standard color (4:2:0)' '$ ' > "$W/term.txt"
  [ -n "$README" ] && readme_setup
  if [ "${SCENARIO:-frame}" = tools ] || [ "${SCENARIO:-frame}" = agent ] || [ "${SCENARIO:-frame}" = agentoff ]; then
    # private things for Hide Private Info to find: an email address, a key, a home folder
    printf '%s\n' '$ git config user.email' 'dana.reyes@example.com' '$ echo $OPENAI_API_KEY' \
      'sk-proj-9fK2xQ7mL4vR8tY1wZ3bN6cD5hJ0' '$ pwd' '/home/dana/projects/launch-notes' '$ ls' 'notes.md  slides.key  budget.ods' '$ ' > "$W/term.txt"
  fi
  konsole --hide-menubar --hide-tabbar -p TerminalColumns=96 -p TerminalRows=24 -e bash -c "${TERM_CMD:-cat '$W/term.txt'; exec cat}" > /dev/null 2>&1 &
  [ "${SCENARIO:-frame}" = tools ] && { /usr/lib64/qt6/bin/qml "$HERE/qr.qml" > "$OUT/qr.log" 2>&1 & }
  sleep 2
fi
if [ -n "$README" ]; then readme_windows; else
kdialog --title "Test window" --msgbox "A window behind the Ectolens overlay.\nNothing here is your real screen." > /dev/null 2>&1 &
DLG=$!
fi
sleep 2
if [ "${SCENARIO:-frame}" = startup ]; then
  # Print Screen as on the desktop: nothing runs until the key, then D-Bus starts Ectolens and
  # hands it the Activate call. Five cold starts, each closed by a second press (as Esc would),
  # while compositor-watch times every answer from KWin: a frozen desktop is a slow answer.
  cat > "$W/ectolens-activated.sh" <<SH
#!/bin/bash
exec env QT_FORCE_STDERR_LOGGING=1 ECTOLENS_NO_GPU_PROBE=1 "$BIN/ectolens" --dbus >> "$OUT/ectolens.log" 2>&1
SH
  chmod +x "$W/ectolens-activated.sh"
  printf '[D-BUS Service]\nName=com.ectonomic.ectolens\nExec=%s\n' "$W/ectolens-activated.sh" > "$W/services/com.ectonomic.ectolens.service"
  dbus-update-activation-environment --all >> "$OUT/activate.log" 2>&1
  qdbus org.kde.KWin /KWin supportInformation 2>/dev/null | grep -iE "OpenGL (vendor|renderer)|Compositing Type|Platform" > "$OUT/gpu.txt"
  "$COMPOSITOR_WATCH" 120 30 > "$OUT/compositor-watch.log" 2>&1 &
  CW=$!
  sleep 1
  for n in $(seq ${STARTUP_RUNS:-5}); do
    date +"PRESS $n %H:%M:%S.%3N" >> "$OUT/presses.log"
    python3 -I - >> "$OUT/activate.log" 2>&1 <<'PY'
import dbus
app = dbus.Interface(dbus.SessionBus().get_object('com.ectonomic.ectolens', '/com/ectonomic/ectolens'), 'org.freedesktop.Application')
app.Activate(dbus.Dictionary({}, signature='sv'))
PY
    for i in $(seq 150); do [ "$(grep -c "ECTOLENS-STARTUP" "$OUT/ectolens.log" 2>/dev/null)" -ge "$n" ] && break; sleep 0.1; done
    date +"DRAWN-LINE $n %H:%M:%S.%3N" >> "$OUT/presses.log"
    python3 -I - >> "$OUT/activate.log" 2>&1 <<'PY'
import dbus
app = dbus.Interface(dbus.SessionBus().get_object('com.ectonomic.ectolens', '/com/ectonomic/ectolens'), 'org.freedesktop.Application')
app.Activate(dbus.Dictionary({}, signature='sv'))
PY
    for i in $(seq 100); do
      [ "$(dbus-send --session --print-reply --dest=org.freedesktop.DBus / org.freedesktop.DBus.NameHasOwner string:com.ectonomic.ectolens 2>/dev/null | grep -c true)" = 0 ] && break; sleep 0.1
    done
    date +"GONE $n %H:%M:%S.%3N" >> "$OUT/presses.log"
    sleep 1
  done
  kill -INT $CW; wait $CW 2>/dev/null
  kill $DLG $NS 2>/dev/null; sleep 0.5; kill $KW; wait $KW 2>/dev/null
  exit 0
fi
if [ -n "$README" ]; then readme_place
elif [ -n "$SCENE" ]; then
  # Fixed places, so every capture and the mock line up. Konsole can take a few seconds to
  # open: place again until KWin says it was found.
  cat > "$W/place.js" <<'JS'
for (const w of workspace.windowList()) {
    // Wayland windows carry their app id as the class (org.kde.konsole); the mock's Konsole is 799×508 at 400, 196
    if (w.resourceClass.indexOf("konsole") >= 0) { w.frameGeometry = {x: 400, y: 196, width: 799, height: 508}; print("ECTOLENS-PLACED konsole"); }
    if (w.caption.indexOf("Test window") >= 0) { w.frameGeometry = {x: 1180, y: 520, width: 340, height: 150}; }
    if (w.caption.indexOf("QR test") >= 0) { w.frameGeometry = {x: 900, y: 380, width: 170, height: 170}; w.keepAbove = true; print("ECTOLENS-PLACED qr"); }
}
JS
  for i in $(seq 20); do
    qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$W/place.js" ectolens-place-$i > /dev/null 2>&1
    qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.start > /dev/null 2>&1
    sleep 0.5
    grep -q "ECTOLENS-PLACED konsole" "$OUT/kwin.log" && { [ "${SCENARIO:-frame}" != tools ] || grep -q "ECTOLENS-PLACED qr" "$OUT/kwin.log"; } && break
  done
  sleep 1
fi
if [ "${SCENARIO:-frame}" = desktops ]; then
  # A second virtual desktop, and the test window moved onto it: from the first
  # desktop, the place where it sits must be empty space to Ectolens.
  qdbus org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.createDesktop 1 "Second" >> "$OUT/desktops.log" 2>&1
  cat > "$W/move.js" <<'JS'
const second = workspace.desktops[1];
for (const w of workspace.windowList()) {
    if (w.caption.indexOf("Test window") >= 0) { w.desktops = [second]; print("ECTOLENS-MOVED " + w.caption); }
}
workspace.currentDesktop = workspace.desktops[0];
JS
  qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$W/move.js" ectolens-move >> "$OUT/desktops.log" 2>&1
  qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.start >> "$OUT/desktops.log" 2>&1
  sleep 1
fi
case "${SCENARIO:-frame}" in
  # undo and redo through the frame's moves and resizes and the marks, in order, with the real pointer and keys
  frameundo) SCRIPT="mark:overlay;frame:400,196,799,508;wait:600;framerect;framehistory;mark:move;wait:900;framerect;framehistory;mark:resize;wait:900;framerect;framehistory;tool:NumberTool;wait:300;mark:number;wait:600;tool:NoTool;wait:300;framerect;framehistory;mark:nudge;wait:900;framerect;framehistory;mark:undo1;framerect;framehistory;mark:undo2;framerect;framehistory;mark:undo3;framerect;framehistory;mark:undo4;framerect;framehistory;mark:undo5;framerect;framehistory;mark:redo1;framerect;framehistory;mark:redo2;framerect;framehistory;mark:redo3;framerect;framehistory;mark:redo4;framerect;framehistory;mark:undo6;framerect;framehistory;mark:undo7;framerect;framehistory;mark:newmove;wait:900;framerect;framehistory;mark:redo5;framerect;framehistory;locate:undoButton;mark:click-undoButton;framerect;framehistory";;
  # an AI agent's requests through ectolens-mcp, the person answering each in the overlay
  agent) SCRIPT="mark:agent1;wait:3000;rect:agentCard;prop:frameHandles,visible;locate:agentCardAdjust;locate:agentCardSend;mark:send1;mark:agent2;wait:3000;rect:agentCard;locate:numberTool;mark:dimclick2;toolinfo;prop:agentCard,nudges;rect:agentCard;framerect;mark:dimkey2;framerect;locate:agentCardRefuse;mark:refuse2;mark:agent3;wait:3000;mark:enter3;mark:agent4;wait:2500;prop:agentCardNote,text;locate:agentCardAdjust;mark:adjust4;rect:agentCard;locate:agentSend;mark:send4;mark:agent5;wait:3000;locate:agentCardAdjust;mark:adjust5;wait:400;prop:frameHandles,visible;framerect;mark:drag5;framerect;locate:agentSend;mark:send5;mark:agent6;wait:3000;prop:agentCardBlur,checked;prop:agentCardBlurNote,text;locate:agentCardBlur;mark:unblur6;wait:600;prop:agentCardBlur,checked;prop:agentCardBlurNote,text;locate:agentCardSend;mark:send6;mark:agent7;wait:3000;prop:agentCardBlur,checked;locate:agentCardBlur;mark:unblur7;wait:600;mark:reblur7;wait:600;prop:agentCardBlur,checked;prop:agentCardBlurNote,text;locate:agentCardSend;mark:send7;mark:end";;
  agentoff) SCRIPT="mark:end";;
  # a frame filling the screen: the recording bar inside it, on screen, and not in the file
  fullrecord) SCRIPT="mark:overlay;frame:0,0,${TEST_WIDTH:-1600},${TEST_HEIGHT:-900};mark:framed;record;wait:2500;rect:recordingBar;mark:recording;stop;wait:4000";;
  frame) SCRIPT="mark:overlay;frame:300,150,800,500;mark:framed;record;wait:1500;mark:recording;pause;wait:600;mark:paused;resume;wait:1200;stop;wait:4000";;
  soundtracks) SCRIPT="mark:overlay;frame:300,150,800,500;mark:framed;record;wait:4000;mark:recording;stop;wait:4000";;
  settings) SCRIPT="mark:overlay;mark:framed;locate:recordArrow;mark:click-recordArrow;mark:menu;locate:recordingSettings;mark:click-recordingSettings;wait:2500;mark:settings;wait:500";;
  input) SCRIPT="mark:overlay;mark:framed;framerect;rows;locate:recordArrow;mark:click-recordArrow;menus;anatomy;mark:menu;rows;mark:recording;anatomy;locate:stop;mark:click-stop;mark:stopped;wait:4000";;
  launch) SCRIPT="mark:overlay;frame:300,150,800,500;mark:framed;record;wait:1500;mark:recording;pause;wait:600;mark:paused;resume;wait:1200;stop;wait:4000";;
  tour) SCRIPT="mark:start;mark:pointing;locate:recordArrow;mark:framed;mark:menu;mark:recording;anatomy;stop;wait:3000";;
  desktops) SCRIPT="hover:800,450;mark:hover-elsewhere;click;wait:300";;
  printkey) SCRIPT="mark:overlay;mark:pointing;framerect;mark:framed;wait:8000";;
  # Print Screen pressed again while the overlay is up closes it
  toggle) SCRIPT="mark:overlay;wait:8000";;
  share) SCRIPT="sharetargets;mark:overlay;frame:400,196,799,508;mark:framed;wait:1500;menus;anatomy;mark:chooser;mark:escape;wait:2500;mark:after;wait:500";;
  tools) SCRIPT="mark:start;mark:pointing;anatomy;framerect;locate:frameLabel;mark:click-frameLabel;menus;anatomy;mark:sizemenu;mark:escape;size:1280,720;watchvisible:3500;framerect;mark:resized;locate:hidePrivateInfo;mark:click-hidePrivateInfo;wait:6000;mark:hidden;anatomy;mark:rehover;wait:1800;anatomy;locate:pickColor;mark:click-pickColor;mark:hover-color;anatomy;mark:color;mark:click-here;wait:600;locate:measure;mark:click-measure;mark:drag-measure;anatomy;wait:300;locate:openQr;mark:qr;anatomy;locate:captureArrow;mark:click-captureArrow;menus;anatomy;mark:aftermenu;mark:escape;locate:gear;mark:click-gear;menus;anatomy;mark:optionsmenu;mark:escape;locate:pin;mark:click-pin;wait:2000;mark:pinned;mark:probe-size;wait:500";;
  # the marking options beside the frame, the Text and Number marks, and the picture they make
  annotate) SCRIPT="mark:overlay;screens;frame:640,420,300,120;tool:TextTool;wait:500;mark:small;anatomy;framerect;rows;rect:optionsRow;currenttool;rect:topRow;prop:optionsRow,sidePadding;tool:NoTool;frame:400,196,799,508;mark:framed;anatomy;number:470,330;number:470,390;number:470,450;deselect;text:700,300,Run this first;wait:400;mark:editing;deselect;tool:TextTool;wait:500;framerect;locate:optionsRow;rect:optionsRow;currenttool;rect:topRow;locate:topRow;locate:bottomRow;locate:optionColor;mark:rail-text;anatomy;locate:optionSize;mark:click-optionSize;menus;anatomy;closemenus;mark:click-optionColor;menus;anatomy;locate:markColorc8f22e;locate:markColoref4444;mark:colormenu;mark:click-markColoref4444;wait:300;toolinfo;closemenus;locate:optionFont;mark:click-optionFont;menus;anatomy;locate:font-GeistMono;mark:fontmenu;mark:click-font-GeistMono;wait:300;toolinfo;closemenus;text:700,560,Second label;deselect;tool:NumberTool;wait:400;locate:optionsRow;rect:optionsRow;currenttool;rect:topRow;mark:rail-number;anatomy;tool:RectangleTool;wait:400;locate:optionsRow;rect:optionsRow;currenttool;rect:topRow;mark:rail-rect;anatomy;tool:BlurTool;wait:400;locate:optionsRow;rect:optionsRow;currenttool;rect:topRow;locate:optionStrength;mark:rail-blur;anatomy;rect:optionStrength;mark:strength-left;sliders;mark:strength-right;sliders;tool:ArrowTool;wait:400;locate:optionsRow;rect:optionsRow;currenttool;rect:topRow;locate:optionWidth;mark:rail-arrow;anatomy;mark:click-optionWidth;menus;anatomy;closemenus;tool:FreehandTool;wait:400;rect:optionsRow;currenttool;rect:topRow;mark:rail-freehand;anatomy;locate:optionStroke;mark:click-optionStroke;menus;locate:markColorCustom;mark:click-markColorCustom;wait:300;anatomy;rect:pickerSquare;rect:pickerHue;rect:pickerOpacity;mark:pickerSquare-topleft;sliders;mark:pickerSquare-bottomright;sliders;mark:pickerHue-left;sliders;mark:pickerHue-right;sliders;mark:pickerOpacity-left;sliders;mark:pickerOpacity-right;sliders;closemenus;text:900,640,;deselect;accept;wait:2500;mark:after;wait:300";;
  # the options rail after each real click on a tool button (clicking the text box or number tool sometimes did not bring up the bar)
  # textbox: a real drag with the Text tool draws a box, real keys type into it, its right handle is
  # dragged in with the real pointer, then a click (that wobbles 3 px) makes a plain label
  # (a click in the box's padding then types into the same box; a plain label shows no handles, and a
  # click in its padding types into it too); a drag with Shift held draws a square box, with Ctrl
  # held a box around the press; Center then Middle picked from the rail's Align menu with real
  # clicks make the next label grow both ways from its click and a box's words sit in its middle
  textbox) SCRIPT="mark:overlay;frame:400,196,799,508;mark:framed;framerect;framegeometry;tool:TextTool;wait:500;mark:drag-box;textinfo;mark:type-box;textinfo;locate:textBoxHandle4;mark:drag-handle;textinfo;mark:click-padding;textinfo;mark:click-label;textinfo;locate:textBoxHandle4;mark:handles-after-label;mark:click-label-padding;textinfo;deselect;mark:shift-drag;textinfo;deselect;mark:ctrl-drag;textinfo;deselect;locate:optionAlign;mark:click-optionAlign;menus;anatomy;locate:align-4;locate:align-128;mark:alignmenu;mark:click-align-4;wait:300;toolinfo;locate:optionAlign;mark:click-optionAlign~2;locate:align-128;mark:click-align-128;wait:300;toolinfo;mark:center-label;textinfo;deselect;mark:center-box;textinfo;deselect;accept;wait:2500;mark:after;wait:300";;
  toolclicks) SCRIPT="mark:overlay;frame:400,196,799,508;mark:framed;screens;locate:textTool;locate:numberTool;locate:rectangleTool;locate:selectTool;locate:arrowTool;mark:click-textTool;toolinfo;rect:optionsRow;currenttool;rect:topRow;prop:optionsRow,sidePadding;mark:click-numberTool;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-textTool~2;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-textTool~3;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:canvas-text;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-numberTool~2;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:canvas-number;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-textTool~4;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-numberTool~3;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-rectangleTool;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-textTool~5;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-selectTool;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-numberTool~4;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-arrowTool;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-numberTool~5;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:click-textTool~6;toolinfo;rect:optionsRow;currenttool;rect:topRow;mark:end;wait:300";;
  # window and empty: the Ectolens window driven with real pointer clicks (wlocate finds a control, click-NAME clicks it)
  window) SCRIPT="mark:overlay;frame:400,196,799,508;mark:framed;wait:2500;mark:place;mark:window;anatomy;wlocate:windowSaveArrow;mark:click-windowSaveArrow;menus;anatomy;mark:savemenu;mark:escape;wlocate:windowCopyArrow;mark:click-windowCopyArrow;menus;anatomy;mark:copymenu;mark:escape;tool:RectangleTool;draw:560,300,760,400;mark:tool;anatomy;tool:TextTool;wait:500;mark:drag-wbox;textinfo;anatomy;deselect;tool:FreehandTool;wait:400;mark:wfreehand;anatomy;tool:CropTool;wait:800;mark:drag-crop;wprop:cropRow,hasArea;mark:drag-crop-again;mark:crop;anatomy;wlocate:cropApply;mark:click-cropApply;canvas;mark:cropped;wlocate:windowZoom;mark:click-windowZoom;menus;anatomy;mark:zoommenu;mark:escape;wlocate:windowGear;mark:click-windowGear;menus;anatomy;mark:gearmenu;mark:escape;save;message:scanned;mark:messages;anatomy;wlocate:newCaptureArrow;mark:click-newCaptureArrow;menus;anatomy;mark:newmenu;mark:escape;wresize:600,600;mark:narrow;wait:500;anatomy";;
  # textzoom: the whole screen taken, so the Ectolens window can zoom out (never below fit) as well as in:
  # Actual Size then Ctrl++ (150%), then Ctrl+0 (fit, under 100%). At each, a box drawn by a real drag and each
  # of its handles (every one the box reports) dragged out with the real pointer; zoomed in, then Ctrl+Z, Ctrl+Z
  # and Ctrl+Shift+Z while the box is still being written in; last, the window's own Align button and menu by real clicks
  textzoom) SCRIPT="mark:overlay;frame:0,0,1600,900;mark:framed;wait:2500;mark:place;mark:window;tool:TextTool;wait:500;watchreleases;mark:zoom-in;wait:600;wzoom;mark:drag-zbox;textinfo;wlocateall:textBoxHandle;mark:drag-handles;mark:undo;textinfo;mark:undo~2;textinfo;mark:redo;textinfo;mark:type-after;textinfo;deselect;tool:TextTool;wait:400;mark:zoom-out;wait:600;wzoom;mark:drag-zbox~2;textinfo;wlocateall:textBoxHandle;mark:drag-handles~2;deselect;tool:TextTool;wait:400;wlocate:optionAlign;mark:click-optionAlign;menus;anatomy;wlocateall:align-;mark:walignmenu;mark:click-align-2;wait:300;toolinfo;wait:300";;
  video) SCRIPT="mark:overlay;frame:400,196,799,508;mark:framed;record;wait:2500;stop;wait:4000;mark:place;wait:1500;mark:video;anatomy;wprop:scrubber,enabled;sliders;mark:scrub-left;sliders;mark:scrub-right;sliders;wprop:windowSize,sizeText;wprop:windowZoom,visible;wprop:windowExtract,visible;wait:300";;
  empty) SCRIPT="mark:place;mark:empty;anatomy;wlocate:newCaptureArrow;mark:click-newCaptureArrow;menus;anatomy;mark:emptymenu;mark:escape;wait:500";;
  pick)  SCRIPT="hover:800,450;mark:hover-window;hover:60,60;mark:hover-screen;hover:800,450;click;mark:picked;record;wait:1500;mark:recording;stop;wait:4000";;
esac
[ -n "$README" ] && readme_script
# TEST_FRAME=X,Y,W,H: the toolclicks frame somewhere else (another screen); its picture clicks follow it
FX=400; FY=196
if [ -n "${TEST_FRAME:-}" ]; then SCRIPT=${SCRIPT//frame:400,196,799,508/frame:$TEST_FRAME}; FX=${TEST_FRAME%%,*}; FY=$(echo "$TEST_FRAME" | cut -d, -f2); fi
case "${SCENARIO:-frame}" in window|empty|video|textzoom) WINDOW_TEST=1;; *) WINDOW_TEST=;; esac
if [ -n "${TEST_LAST_TOOL:-}" ]; then
  # the marking tool a person used last, as their ectolensrc remembers it (12 is Number)
  printf '[Annotations]\nannotationToolType=%s\n' "$TEST_LAST_TOOL" >> "$XDG_CONFIG_HOME/ectolensrc"
fi
if [ "${SCENARIO:-frame}" = agent ]; then
  # Settings › Model Context Protocol (MCP): agents on (Hide Private Info's blur stays on, as it starts)
  printf '[General]\nagentAccess=true\n' > "$XDG_CONFIG_HOME/ectolensrc"
fi
if [ "${SCENARIO:-frame}" = annotate ] || [ "${SCENARIO:-frame}" = textbox ]; then
  # Save to file checked, so the marked picture can be read back
  printf '[General]\nautoSaveImage=true\n' > "$XDG_CONFIG_HOME/ectolensrc"
fi
if [ "${SCENARIO:-frame}" = soundtracks ]; then
  # Recording Settings › Sound: system audio and the microphone, on separate tracks too
  printf '[GuiConfig]\nvideoRecordSystemAudio=true\nvideoRecordMicrophone=true\nvideoSeparateAudioTracks=true\n' > "$XDG_CONFIG_HOME/ectolensrc"
fi
if [ "${SCENARIO:-frame}" = share ]; then
  # Share checked and set to ask each time: Enter shows the services first
  printf '[General]\nafterCaptureShare=true\n' > "$XDG_CONFIG_HOME/ectolensrc"
fi
if [ "${SCENARIO:-frame}" = window ] || [ "${SCENARIO:-frame}" = video ] || [ "${SCENARIO:-frame}" = textzoom ]; then
  # Open with › Ectolens checked, so Enter opens the window (design proposal 2's After Capture)
  printf '[General]\nafterCaptureOpen=true\n' > "$XDG_CONFIG_HOME/ectolensrc"
fi
if [ -n "${TEST_LIGHT:-}" ]; then
  # a light Plasma color scheme, for the window's light set
  cp /usr/share/color-schemes/BreezeLight.colors "$XDG_CONFIG_HOME/kdeglobals"
  echo "light scheme: $(grep -m1 BackgroundNormal "$XDG_CONFIG_HOME/kdeglobals")" >> "$OUT/window-steps.log"
fi
if [ "${SCENARIO:-frame}" = input ] || [ "${SCENARIO:-frame}" = settings ] || [ -n "$SCENE" ] || [ -n "$WINDOW_TEST" ]; then
  # KWin grants fake input by desktop file, like everything else: give the test client one.
  cat > "$XDG_DATA_HOME/applications/ectolens-fake-input.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Ectolens test input
Exec=$FAKE_INPUT
NoDisplay=true
X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input
DESK
  coproc FI { exec "$FAKE_INPUT" 2>>"$OUT/fake-input.log"; }
  feed() { echo "$1" >&"${FI[1]}"; read -t 10 -r _ <&"${FI[0]}"; }
  # lowercase words and spaces as real key presses (Linux key codes)
  type_words() {
    local s=$1 i c code
    for ((i = 0; i < ${#s}; i++)); do
      c=${s:i:1}
      case "$c" in
        a) code=30;; b) code=48;; c) code=46;; d) code=32;; e) code=18;; f) code=33;; g) code=34;; h) code=35;; i) code=23;;
        j) code=36;; k) code=37;; l) code=38;; m) code=50;; n) code=49;; o) code=24;; p) code=25;; q) code=16;; r) code=19;;
        s) code=31;; t) code=20;; u) code=22;; v) code=47;; w) code=17;; x) code=45;; y) code=21;; z) code=44;; ' ') code=57;;
        *) code=;;
      esac
      [ -n "$code" ] && feed "key $code"
    done
  }
fi
if [ "${SCENARIO:-frame}" = tour ]; then shot "$OUT/scene.png"; fi
# the script as run, so a check can count what it asked for (anatomy.py)
printf '%s\n' "${SCRIPT:-}" > "$OUT/script.txt"
if [ "${SCENARIO:-frame}" = empty ]; then
  # opened from the launcher: no capture
  QT_FORCE_STDERR_LOGGING=1 ECTOLENS_TEST_SCRIPT="$SCRIPT" ECTOLENS_TEST_ACK=$W/ack ECTOLENS_NO_GPU_PROBE=1 "$BIN/ectolens" --launchonly > "$OUT/ectolens.log" 2>&1 &
  EL=$!
elif [ "${SCENARIO:-frame}" = launch ] || [ "${SCENARIO:-frame}" = input ] || [ "${SCENARIO:-frame}" = settings ] || [ "${SCENARIO:-frame}" = desktops ] || [ -n "$SCENE" ]; then
  # The way Print Screen starts Ectolens: in D-Bus mode, then the shortcut's action is
  # sent to the app's D-Bus name (com.ectonomic.ectolens), as kglobalaccel does.
  QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="${ECTOLENS_LOG_RULES:-}" ECTOLENS_TEST_SCRIPT="$SCRIPT" ECTOLENS_TEST_ACK=$W/ack ECTOLENS_NO_GPU_PROBE=1 "$BIN/ectolens" --dbus > "$OUT/ectolens.log" 2>&1 &
  EL=$!
  sleep 2
  # printkey: Print Screen itself is the desktop entry's own shortcut, which KIO sends as Activate
  # (start mode Gui). The capture shortcuts, such as Meta+Shift+Print, send ActivateAction.
  ACT=action; case "${SCENARIO:-frame}" in printkey|window|tools|share|video|toggle|annotate|toolclicks|textbox|textzoom|readme-*) ACT=activate;; esac
  [ -n "$README" ] && readme_before_activate
  case "${SCENARIO:-frame}" in agent|agentoff|readme-agent) ACT=none;; esac
  ACT=$ACT python3 -I - > "$OUT/activate.log" 2>&1 <<'PY'
import dbus, os
app = dbus.Interface(dbus.SessionBus().get_object('com.ectonomic.ectolens', '/com/ectonomic/ectolens'), 'org.freedesktop.Application')
if os.environ['ACT'] == 'none':
    pass  # an agent opens the overlay (agent runs)
elif os.environ['ACT'] == 'activate':
    app.Activate(dbus.Dictionary({}, signature='sv'))
else:
    app.ActivateAction('RectangularRegionScreenShot', dbus.Array([], signature='v'), dbus.Dictionary({}, signature='sv'))
print('activated', os.environ['ACT'])
PY
  if [ "${SCENARIO:-frame}" = agent ] || [ "${SCENARIO:-frame}" = agentoff ]; then
    # a made-up agent: ectolens-mcp started as an agent starts it, speaking MCP (agent-client.py)
    python3 -I "$HERE/agent-client.py" "$BIN/ectolens-mcp" "$OUT" "$([ "${SCENARIO:-frame}" = agent ] && echo on || echo off)" > "$OUT/agent-client.log" 2>&1 &
    AC=$!
  fi
else
  ECTOLENS_TEST_SCRIPT="$SCRIPT" ECTOLENS_TEST_ACK=$W/ack ECTOLENS_NO_GPU_PROBE=1 "$BIN/ectolens" -r > "$OUT/ectolens.log" 2>&1 &
  EL=$!
fi
# The test's own screenshots come from the installed Spectacle, which KWin permits by its own
# desktop file; Ectolens must earn its permission the same way.
# Which windows KWin treats as normal windows (the only kind its open animations act on).
probe_windows() {
  cat > "$W/probe.js" <<'JS'
for (const w of workspace.windowList()) {
    print("ECTOLENS-WINDOW " + w.resourceClass + " normal=" + w.normalWindow + " dialog=" + w.dialog);
}
JS
  local id; id=$(qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$W/probe.js" ectolens-probe 2>>"$OUT/probe.err"); echo "loaded $id" >> "$OUT/probe.err"
  qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.start >>"$OUT/probe.err" 2>&1; sleep 0.5
  qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript ectolens-probe >/dev/null 2>&1
  grep -h "ECTOLENS-WINDOW" "$OUT/kwin.log" | sort -u
}
SEEN=""
for i in $(seq 600); do
  while read -r tag name; do
    case "$tag" in
      ECTOLENS-STEP) [[ " $SEEN " == *" $name "* ]] && continue; SEEN="$SEEN $name"
        if [ "$name" != done ]; then
          [ -z "$README" ] && shot "$OUT/$name.png"
          [ -n "$README" ] && readme_step "$name"
          if [ "${SCENARIO:-frame}" = tour ]; then
            case "$name" in
              start)    feed "move 700 380"; feed "sleep 400";;
              pointing) feed "click"; feed "sleep 500";;
              framed)   read -r _ _ AX AY < <(grep "^ECTOLENS-AT recordArrow " "$OUT/ectolens.log" | tail -1)
                        feed "move $AX $AY"; feed "click"; feed "sleep 600";;
              menu)     feed "key 1"; feed "sleep 300"; feed "move 700 380"; feed "key 19"; feed "sleep 2500";;
            esac
          fi
          if [ -z "$WINDOW_TEST" ]; then
            case "$name" in
              # an overlay control by its objectName: locate:NAME printed where it is, click-NAME clicks it
              # click-NAME~2: the same control again (a step name runs once)
              click-*) item=${name#click-}; item=${item%%~*}; read -r _ _ IX IY < <(grep "^ECTOLENS-AT $item " "$OUT/ectolens.log" | tail -1)
                       echo "click $item $IX $IY" >> "$OUT/steps.log"
                       if [ -n "$IX" ]; then feed "move $IX $IY"; feed "click"
                         # Stop: shoot the screen while the file is finished, when the overlay must
                         # show nothing (it used to show the frame with Record and the gear)
                         if [ "$item" = stop ]; then
                           for n in 1 2 3 4 5 6 7 8; do echo "$n $(date +%s%3N)" >> "$OUT/afterstop-times.txt"; shot "$OUT/afterstop-$n.png"; done
                         else feed "sleep 700"; fi
                       fi;;
              escape)  feed "key 1"; feed "sleep 500";;
            esac
          fi
          if [ "${SCENARIO:-frame}" = settings ] && [ "$name" = overlay ]; then
            for c in "move 300 150" "down" "move 1100 650" "up" "sleep 300"; do feed "$c"; done
          fi
          if [ "${SCENARIO:-frame}" = share ] && [ "$name" = framed ]; then feed "key 28"; feed "sleep 1200"; fi
          if [ "${SCENARIO:-frame}" = tools ]; then
            case "$name" in
              start)      feed "move 740 400"; feed "sleep 400";;
              pointing)   feed "click"; feed "sleep 700";;
              hover-color) feed "move 700 820"; feed "sleep 400";;
              # off Hide Private Info and back: its tooltip, hidden by the click, shows again
              rehover)    read -r _ _ HX HY < <(grep "^ECTOLENS-AT hidePrivateInfo " "$OUT/ectolens.log" | tail -1)
                          feed "move 700 400"; feed "sleep 300"; feed "move $HX $HY"; feed "sleep 200";;
              click-here) feed "click"; feed "sleep 500";;
              # the line, its picture, then Esc at once (after this test's own screenshot tool has run,
              # a closed menu leaves KWin sending the overlay no keys: docs/deferred.md, Watch)
              drag-measure) for c in "move 480 300" "down" "move 600 340" "move 760 380" "up" "sleep 400"; do feed "$c"; done
                            shot "$OUT/measured.png"; feed "key 1"; feed "sleep 400";;
              probe-size) cat > "$W/ksize.js" <<'JS'
for (const w of workspace.windowList()) {
    if (w.resourceClass.indexOf("konsole") >= 0) { print("ECTOLENS-KONSOLE " + Math.round(w.frameGeometry.width) + " " + Math.round(w.frameGeometry.height)); }
    if (w.resourceClass.indexOf("ectolens") >= 0) { print("ECTOLENS-WINDOW " + w.resourceClass + " normal=" + w.normalWindow + " " + Math.round(w.frameGeometry.width) + "x" + Math.round(w.frameGeometry.height)); }
}
JS
                          qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$W/ksize.js" ectolens-ksize >/dev/null 2>&1
                          qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1; sleep 0.5;;
            esac
          fi
          if [ "${SCENARIO:-frame}" = agent ] || [ "${SCENARIO:-frame}" = agentoff ]; then
            # the person: wait for the overlay the agent's Nth request opened, then answer it
            agent_framed() { for i in $(seq 300); do [ "$(grep -c "^ECTOLENS-AGENT framed" "$OUT/ectolens.log")" -ge "$1" ] && return 0; sleep 0.1; done; echo "request $1 never framed" >> "$OUT/agent-steps.log"; }
            agent_click() { read -r _ _ IX IY < <(grep "^ECTOLENS-AT $1 " "$OUT/ectolens.log" | tail -1); echo "click $1 $IX $IY" >> "$OUT/agent-steps.log"
                            [ -n "$IX" ] && { feed "move $IX $IY"; feed "sleep 300"; feed "click"; feed "sleep 800"; }; }
            case "$name" in
              agent1) agent_framed 1;;
              agent2) agent_framed 2;;
              agent3) agent_framed 3;;
              agent4) agent_framed 4;;
              agent5) agent_framed 5;;
              agent6) agent_framed 6;;
              agent7) agent_framed 7;;
              # the dialog's Blur private information, clicked with the real pointer
              unblur6|unblur7|reblur7) agent_click agentCardBlur;;
              send6|send7) agent_click agentCardSend;;
              # nothing reaches the agent before Send: its folder is still empty while the overlay asks
              send1)  ls -A "$XDG_RUNTIME_DIR/ectolens-agent" 2>/dev/null | wc -l > "$OUT/before-send.txt"; shot "$OUT/consent.png"; agent_click agentCardSend;;
              # a click on a dimmed tool while the dialog asks: the dim takes it, the dialog nudges;
              # then an arrow key, which must leave the frame as the dialog's preview shows it
              dimclick2) agent_click numberTool;;
              dimkey2) feed "key 106"; feed "sleep 400";;
              refuse2) shot "$OUT/consent-area.png"; agent_click agentCardRefuse;;
              # Adjust First closes the dialog; the bottom row's Record then starts it
              adjust4) agent_click agentCardAdjust;;
              adjust5) agent_click agentCardAdjust;;
              # the frame dragged up by the real pointer, over the terminal's private lines
              drag5)  for c in "move 610 415" "sleep 150" "down" "sleep 80" "move 610 395" "sleep 40" "move 610 365" "sleep 40" "move 610 335" "sleep 40" "move 610 315" "sleep 150" "up" "sleep 300"; do feed "$c"; done;;
              send5)  agent_click agentSend;;
              enter3) feed "key 28"; feed "sleep 800";;
              send4)  shot "$OUT/consent-record.png"; agent_click agentSend; feed "sleep 1500"; shot "$OUT/agent-recording.png";;
              end)    for i in $(seq 600); do [ -e "$OUT/agent-client.done" ] && break; sleep 0.1; done;;
            esac
          fi
          if [ "${SCENARIO:-frame}" = frameundo ]; then
            # where the frame is now (the latest framerect), for the handle to pull
            read -r _ RX RY RW RH _ < <(grep "^ECTOLENS-FRAME " "$OUT/ectolens.log" | tail -1)
            case "$name" in
              move)    for c in "move 800 450" "sleep 150" "down" "sleep 80" "move 830 465" "sleep 40" "move 870 485" "sleep 40" "move 900 500" "sleep 150" "up" "sleep 300"; do feed "$c"; done;;
              resize)  BX=$((RX + RW)); BY=$((RY + RH))
                       for c in "move $BX $BY" "sleep 150" "down" "sleep 80" "move $((BX + 30)) $((BY + 15))" "sleep 40" "move $((BX + 80)) $((BY + 40))" "sleep 150" "up" "sleep 300"; do feed "$c"; done;;
              number)  for c in "move 700 400" "click" "sleep 400"; do feed "$c"; done;;
              nudge)   for c in "key 106" "sleep 80" "key 106" "sleep 80" "key 106" "sleep 300"; do feed "$c"; done;;
              undo*)   for c in "press 29" "key 44" "release 29" "sleep 400"; do feed "$c"; done;;
              redo*)   for c in "press 29" "press 42" "key 44" "release 42" "release 29" "sleep 400"; do feed "$c"; done;;
              newmove) MX=$((RX + RW / 2)); MY=$((RY + RH / 2))
                       for c in "move $MX $MY" "sleep 150" "down" "sleep 80" "move $((MX - 20)) $((MY + 10))" "sleep 40" "move $((MX - 50)) $((MY + 20))" "sleep 150" "up" "sleep 300"; do feed "$c"; done;;
            esac
          fi
          if [ "${SCENARIO:-frame}" = toolclicks ]; then
            case "$name" in
              # a real click on the picture with the tool in use: a text (then type) or a number
              canvas-text) feed "move $((FX + 300)) $((FY + 104))"; feed "click"; feed "sleep 500"; for k in 35 23; do feed "key $k"; done; feed "sleep 400";;
              canvas-number) feed "move $((FX + 70)) $((FY + 204))"; feed "click"; feed "sleep 500";;
            esac
          fi
          if [ "${SCENARIO:-frame}" = textbox ]; then
            case "$name" in
              # press, travel and let go, as a hand drags: a box from (40,40) to (240,110) in the frame
              drag-box) for c in "move $((FX + 40)) $((FY + 40))" "down" "move $((FX + 60)) $((FY + 50))" "move $((FX + 140)) $((FY + 80))" "move $((FX + 240)) $((FY + 110))" "up" "sleep 500"; do feed "$c"; done;;
              type-box) type_words "click record to start then pick the window you want to keep"; feed "sleep 600";;
              # the box's right handle, 100 px to the left
              drag-handle) read -r _ _ HX HY < <(grep "^ECTOLENS-AT textBoxHandle4 " "$OUT/ectolens.log" | tail -1)
                           echo "handle $HX $HY" >> "$OUT/steps.log"
                           if [ -n "$HX" ]; then for c in "move $HX $HY" "down" "move $((HX - 40)) $HY" "move $((HX - 100)) $HY" "up" "sleep 500"; do feed "$c"; done; fi;;
              # in the box's left padding (the words start 8 px in), then " ok"
              click-padding) for c in "move $((FX + 43)) $((FY + 60))" "click" "sleep 400"; do feed "$c"; done
                             type_words " ok"; feed "sleep 500";;
              click-label) for c in "move $((FX + 420)) $((FY + 300))" "down" "move $((FX + 423)) $((FY + 302))" "up" "sleep 500"; do feed "$c"; done
                           type_words "hi there"; feed "sleep 500";;
              # in the label's left padding (its words start where it was clicked), then " ok"
              click-label-padding) for c in "move $((FX + 416)) $((FY + 300))" "click" "sleep 400"; do feed "$c"; done
                                   type_words " ok"; feed "sleep 500";;
              # Shift held (key 42): from (500,60) toward (600,90), a square box 100 on a side
              shift-drag) for c in "move $((FX + 500)) $((FY + 60))" "press 42" "sleep 100" "down" "move $((FX + 520)) $((FY + 66))" "move $((FX + 560)) $((FY + 78))" "move $((FX + 600)) $((FY + 90))" "sleep 100" "up" "release 42" "sleep 500"; do feed "$c"; done;;
              # Ctrl held (key 29): from (380,120) toward (440,150), a box 120 by 60 around the press
              ctrl-drag) for c in "move $((FX + 380)) $((FY + 120))" "press 29" "sleep 100" "down" "move $((FX + 395)) $((FY + 128))" "move $((FX + 420)) $((FY + 140))" "move $((FX + 440)) $((FY + 150))" "sleep 100" "up" "release 29" "sleep 500"; do feed "$c"; done;;
              # with Center and Middle picked: a plain click at (600,230) and words; a box dragged (60,210) to (300,330) and a word
              # (inside a 700x400 frame too, as on a scaled two-screen layout, and above the row the overlay puts inside a frame low on its screen)
              center-label) for c in "move $((FX + 600)) $((FY + 230))" "click" "sleep 500"; do feed "$c"; done
                            type_words "centered words"; feed "sleep 500";;
              center-box) for c in "move $((FX + 60)) $((FY + 210))" "down" "move $((FX + 100)) $((FY + 230))" "move $((FX + 200)) $((FY + 290))" "move $((FX + 300)) $((FY + 330))" "up" "sleep 500"; do feed "$c"; done
                          type_words "mid"; feed "sleep 500";;
              after) cp "$(ls -t "$HOME"/Pictures/Screenshots/* 2>/dev/null | head -1)" "$OUT/saved.png" 2>/dev/null;;
            esac
          fi
          if [ "${SCENARIO:-frame}" = annotate ]; then
            case "$name" in
              # a real click at a slider's far left or right end (its rect from rect:NAME): its value goes to 0 or 1
              # (-topleft and -bottomright: a two-way picker's corners)
              *-left|*-right|*-topleft|*-bottomright) item=${name%-*}; [ "$item" = strength ] && item=optionStrength
                      read -r _ _ SX SY SW SH < <(grep "^ECTOLENS-RECT $item " "$OUT/ectolens.log" | tail -1)
                      if [ -n "$SX" ]; then
                        case "${name##*-}" in left) PX=$((SX + 1)); PY=$((SY + SH / 2));; right) PX=$((SX + SW - 2)); PY=$((SY + SH / 2));;
                          topleft) PX=$((SX + 1)); PY=$((SY + 1));; bottomright) PX=$((SX + SW - 2)); PY=$((SY + SH - 2));; esac
                        echo "slider $name at $PX $PY" >> "$OUT/steps.log"
                        for c in "move $PX $PY" "click" "sleep 400"; do feed "$c"; done
                      fi;;
              colormenu|fontmenu) probe_windows > "$OUT/windows-$name.txt";;
              # which KQuickImageEditor the running Ectolens has loaded: Plasma's must never be among them
              rail-text) grep -c "libKQuickImageEditor" /proc/$EL/maps > "$OUT/maps.txt" 2>/dev/null; [ -s "$OUT/maps.txt" ] || echo missing > "$OUT/maps.txt";;
              after) cp "$(ls -t "$HOME"/Pictures/Screenshots/* 2>/dev/null | head -1)" "$OUT/saved.png" 2>/dev/null;;
            esac
          fi
          if [ "${SCENARIO:-frame}" = toggle ] && [ "$name" = overlay ]; then
            # Print Screen again: the same Activate call the desktop entry's shortcut makes
            ACT=activate python3 -I - >> "$OUT/activate.log" 2>&1 <<'PY'
import dbus
app = dbus.Interface(dbus.SessionBus().get_object('com.ectonomic.ectolens', '/com/ectonomic/ectolens'), 'org.freedesktop.Application')
app.Activate(dbus.Dictionary({}, signature='sv'))
print('activated again')
PY
            sleep 2
            probe_windows > "$OUT/windows.txt"
            { kill -0 $EL 2>/dev/null && echo running || echo exited; } > "$OUT/process.txt"
            shot "$OUT/after.png"; touch "$W/toggle-done"
          fi
          if [ "${SCENARIO:-frame}" = printkey ] && [ "$name" = overlay ]; then feed "move 740 400"; feed "sleep 400"; fi
          if [ "${SCENARIO:-frame}" = printkey ] && [ "$name" = pointing ]; then feed "click"; feed "sleep 700"; fi
          if [ "${SCENARIO:-frame}" = printkey ] && [ "$name" = framed ]; then
            # Enter, as a person presses it; then what is left on screen, what was saved, and is Ectolens still running
            feed "key 28"; feed "sleep 3000"
            probe_windows > "$OUT/windows.txt"
            ls -1 "$HOME"/Pictures/Screenshots/ 2>/dev/null > "$OUT/saved.txt"
            { kill -0 $EL 2>/dev/null && echo running || echo exited; } > "$OUT/process.txt"
            shot "$OUT/after.png"; touch "$W/printkey-done"
          fi
          if [ -n "$WINDOW_TEST" ]; then
            case "$name" in
              framed) [ "${SCENARIO:-frame}" = video ] || { feed "key 28"; feed "sleep 2500"; };;   # Enter
              place)  # the Ectolens window at a fixed place; where its inside starts, for clicks
                      cat > "$W/wplace.js" <<'JS'
for (const w of workspace.windowList()) {
    if (w.resourceClass.indexOf("ectolens") >= 0 && w.normalWindow) {
        w.frameGeometry = {x: 260, y: 90, width: 1060, height: 720};
        workspace.activeWindow = w;
    }
}
JS
                      qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$W/wplace.js" ectolens-wplace >/dev/null 2>&1
                      qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1; sleep 1
                      qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript ectolens-wplace >/dev/null 2>&1
                      cat > "$W/wwhere.js" <<'JS'
for (const w of workspace.windowList()) {
    if (w.resourceClass.indexOf("ectolens") >= 0 && w.normalWindow) {
        print("ECTOLENS-CLIENT " + Math.round(w.clientGeometry.x) + " " + Math.round(w.clientGeometry.y) + " " + Math.round(w.clientGeometry.width) + " " + Math.round(w.clientGeometry.height));
    }
}
JS
                      qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$W/wwhere.js" ectolens-wwhere >/dev/null 2>&1
                      qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1; sleep 0.5
                      qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript ectolens-wwhere >/dev/null 2>&1
                      read -r CX CY CW CH < <(grep -o "ECTOLENS-CLIENT .*" "$OUT/kwin.log" | tail -1 | cut -d' ' -f2-); echo "client $CX $CY $CW $CH" >> "$OUT/window-steps.log"
                      feed "move $((CX + 500)) $((CY + 300))"; feed "sleep 300";;
              click-*) item=${name#click-}; read -r _ _ IX IY < <(grep "^ECTOLENS-WAT $item " "$OUT/ectolens.log" | tail -1)
                      echo "click $item $IX $IY" >> "$OUT/window-steps.log"
                      feed "move $((CX + IX)) $((CY + IY))"; feed "click"; feed "sleep 700";;
              escape) feed "key 1"; feed "sleep 400";;   # Escape closes the open menu
              messages) ls -1 "$HOME"/Pictures/Screenshots/ 2>/dev/null > "$OUT/saved.txt";;
              drag-crop-again) # about one run in four the first drag isn't taken as a crop (docs/deferred.md): once more
                      if grep -q "^ECTOLENS-WPROP cropRow hasArea false" "$OUT/ectolens.log"; then
                        echo "crop drag repeated" >> "$OUT/window-steps.log"
                        for c in "move $((CX + 330)) $((CY + 190))" "sleep 200" "down" "sleep 80" "move $((CX + 340)) $((CY + 196))" "sleep 50" "move $((CX + 360)) $((CY + 210))" "sleep 50" "move $((CX + 450)) $((CY + 270))" "sleep 50" "move $((CX + 600)) $((CY + 360))" "sleep 50" "move $((CX + 700)) $((CY + 420))" "sleep 200" "up" "sleep 600"; do feed "$c"; done
                      fi;;
              # a real click at the scrubber's far left or right end (where the sliders report puts it in the window)
              scrub-left|scrub-right) read -r _ _ SX SY SW SH _ < <(grep "^ECTOLENS-SLIDER scrubber " "$OUT/ectolens.log" | tail -1)
                      if [ -n "$SX" ]; then
                        SX=${SX%.*}; SY=${SY%.*}; SW=${SW%.*}; SH=${SH%.*}
                        if [ "$name" = scrub-left ]; then PX=$((CX + SX + 1)); else PX=$((CX + SX + SW - 2)); fi
                        echo "scrub $name at $PX $((CY + SY + SH / 2))" >> "$OUT/window-steps.log"
                        for c in "move $PX $((CY + SY + SH / 2))" "click" "sleep 500"; do feed "$c"; done
                      fi;;
              # textzoom: Ctrl+1 (Actual Size, key 2) then Ctrl++ (Ctrl 29, Shift 42, = 13); Ctrl+0 (fit, key 11)
              zoom-in) for c in "press 29" "key 2" "sleep 300" "press 42" "key 13" "release 42" "release 29" "sleep 600"; do feed "$c"; done;;
              zoom-out) for c in "press 29" "key 11" "release 29" "sleep 600"; do feed "$c"; done;;
              # a box dragged 200 by 70 window pixels from a quarter of the way into the picture as it shows, and a word
              drag-zbox*) read -r _ ZZ ZX ZY ZW ZH < <(grep "^ECTOLENS-ZOOM " "$OUT/ectolens.log" | tail -1)
                      SX=$((CX + ZX + ZW / 4)); SY=$((CY + ZY + ZH * 3 / 10))
                      echo "zoom $ZZ box from $SX $SY" >> "$OUT/window-steps.log"
                      for c in "move $SX $SY" "sleep 200" "down" "sleep 80" "move $((SX + 40)) $((SY + 14))" "sleep 50" "move $((SX + 120)) $((SY + 42))" "sleep 50" "move $((SX + 200)) $((SY + 70))" "sleep 200" "up" "sleep 500"; do feed "$c"; done
                      type_words "note"; feed "sleep 500";;
              # every handle the box reported, dragged out 30 px across and 20 px up or down (by the edges its name
              # carries: Qt::TopEdge 1, LeftEdge 2, RightEdge 4, BottomEdge 8), each from where the last release left it
              drag-handles*) mapfile -t HANDLES < <(python3 -I "$HERE/handles.py" "$OUT/ectolens.log" last)
                      echo "handles ${#HANDLES[@]}" >> "$OUT/window-steps.log"
                      for h in "${HANDLES[@]}"; do
                        read -r HN _ _ <<< "$h"; E=${HN#textBoxHandle}
                        LAST=$(grep -c "^ECTOLENS-RELEASE " "$OUT/ectolens.log")
                        if [ "$LAST" -gt 0 ]; then mapfile -t NOW < <(python3 -I "$HERE/handles.py" "$OUT/ectolens.log" "$(grep "^ECTOLENS-RELEASE " "$OUT/ectolens.log" | tail -1 | cut -d' ' -f2)"); else NOW=("${HANDLES[@]}"); fi
                        HX=; HY=; for n in "${NOW[@]}"; do read -r a b c <<< "$n"; [ "$a" = "$HN" ] && { HX=$b; HY=$c; }; done
                        DX=0; DY=0; (( E & 2 )) && DX=-30; (( E & 4 )) && DX=30; (( E & 1 )) && DY=-20; (( E & 8 )) && DY=20
                        [ -z "$HX" ] && { echo "hdrag $HN missing" >> "$OUT/window-steps.log"; continue; }
                        for c in "move $((CX + HX)) $((CY + HY))" "sleep 150" "down" "sleep 80" "move $((CX + HX + DX / 3)) $((CY + HY + DY / 3))" "sleep 50" "move $((CX + HX + DX)) $((CY + HY + DY))" "sleep 150" "up"; do feed "$c"; done
                        R=$((LAST + 1)); for i in $(seq 40); do [ -n "$(python3 -I "$HERE/handles.py" "$OUT/ectolens.log" $R)" ] && break; sleep 0.1; done
                        echo "hdrag $HN $E $DX $DY release $R" >> "$OUT/window-steps.log"
                      done;;
              # Ctrl+Z (Ctrl 29, z 44) and Ctrl+Shift+Z, while the box is still being written in
              undo*)  for c in "press 29" "key 44" "release 29" "sleep 500"; do feed "$c"; done;;
              redo)   for c in "press 29" "press 42" "key 44" "release 42" "release 29" "sleep 500"; do feed "$c"; done;;
              # keys after all that, with no click: they go into the box, which kept the keyboard
              type-after) type_words " ok"; feed "sleep 500";;
              # a text box in the window, by a real drag on the picture (shown 1:1), and a word typed into it
              drag-wbox) for c in "move $((CX + 180)) $((CY + 420))" "sleep 200" "down" "sleep 80" "move $((CX + 220)) $((CY + 440))" "sleep 50" "move $((CX + 340)) $((CY + 480))" "sleep 50" "move $((CX + 420)) $((CY + 500))" "sleep 200" "up" "sleep 500"; do feed "$c"; done
                      type_words "note"; feed "sleep 500";;
              drag-crop) for c in "move $((CX + 330)) $((CY + 190))" "sleep 200" "down" "sleep 80" "move $((CX + 340)) $((CY + 196))" "sleep 50" "move $((CX + 360)) $((CY + 210))" "sleep 50" "move $((CX + 450)) $((CY + 270))" "sleep 50" "move $((CX + 600)) $((CY + 360))" "sleep 50" "move $((CX + 700)) $((CY + 420))" "sleep 200" "up" "sleep 600"; do feed "$c"; done;;
              make-narrow) cat > "$W/wnarrow.js" <<'JS'
for (const w of workspace.windowList()) {
    if (w.resourceClass.indexOf("ectolens") >= 0 && w.normalWindow) { w.frameGeometry = {x: 260, y: 90, width: 640, height: 640}; }
}
JS
                      qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$W/wnarrow.js" ectolens-wnarrow >/dev/null 2>&1
                      qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1; sleep 1;;
            esac
          fi
          if [ "${SCENARIO:-frame}" = input ]; then
            case "$name" in
              overlay) probe_windows > "$OUT/windows.txt"
                       for c in "move 300 150" "down" "move 600 300" "move 900 500" "move 1100 650" "up" "sleep 300"; do feed "$c"; done;;
              menu)    feed "key 1"; feed "sleep 300"; feed "key 19"; feed "sleep 2500";;   # Escape closes Record's panel; R records
            esac
          fi
          touch "$W/ack/$name"
        fi;;
    esac
  done < <(grep -E "^ECTOLENS-STEP " "$OUT/ectolens.log")
  [ -n "$README" ] && { grep -q "^ECTOLENS-STEP done" "$OUT/ectolens.log" || [ -e "$W/readme-done" ]; } && break
  # (the agent run records in the middle and goes on after it: it ends on its script's done)
  [ -z "$WINDOW_TEST" ] && [ -z "$README" ] && [ "${SCENARIO:-frame}" != agent ] && grep -q "^ECTOLENS-SAVED \|^ECTOLENS-FAILED " "$OUT/ectolens.log" && break
  [ "${SCENARIO:-frame}" = desktops ] && grep -q "^ECTOLENS-TOOK " "$OUT/ectolens.log" && break
  [ "${SCENARIO:-frame}" = printkey ] && [ -e "$W/printkey-done" ] && break
  [ "${SCENARIO:-frame}" = toggle ] && [ -e "$W/toggle-done" ] && break
  [ -n "$WINDOW_TEST" ] && grep -q "^ECTOLENS-STEP done" "$OUT/ectolens.log" && break
  { [ "${SCENARIO:-frame}" = tools ] || [ "${SCENARIO:-frame}" = frameundo ] || [ "${SCENARIO:-frame}" = agent ] || [ "${SCENARIO:-frame}" = agentoff ] || [ "${SCENARIO:-frame}" = share ] || [ "${SCENARIO:-frame}" = annotate ] || [ "${SCENARIO:-frame}" = toolclicks ] || [ "${SCENARIO:-frame}" = textbox ]; } && grep -q "^ECTOLENS-STEP done" "$OUT/ectolens.log" && break
  sleep 0.1
done
sleep 1
sleep 2
[ -n "$README" ] && readme_end
SAVED=$(ls -t "$HOME"/Videos/Screencasts/*.mp4 2>/dev/null | head -1)
[ -n "$SAVED" ] && cp "$SAVED" "$OUT/recording.mp4"
kill $EL $DLG $NS 2>/dev/null; sleep 0.5; kill $KW; wait $KW 2>/dev/null
true
