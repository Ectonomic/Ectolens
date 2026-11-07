#!/bin/bash
# Install Ectolens for you, from this source folder, without replacing anything on the system.
#
#   ./install.sh [--yes] [--prefix DIR] [--no-shortcuts] [--no-deps]
#
# 1. Checks that this is KDE Plasma with a new enough Qt and KDE Frameworks.
# 2. Shows the command that installs what the build needs, and runs it only if you agree
#    (that one step uses sudo and your distribution's package manager).
# 3. Builds Ectolens and its own copy of KPipeWire into ~/.local/opt/ectolens.
# 4. Links its menu entry, D-Bus services, notifications and icons into ~/.local/share.
# 5. Moves Spectacle's capture shortcuts (Print Screen, Meta+Shift+S and the rest) to
#    Ectolens and saves them, so uninstall.sh can give them back. Spectacle stays installed.
#
# --yes answers yes to every question (the package install and closing a running Ectolens).
# Running it again updates the install and keeps your shortcuts and settings.
{
set -euo pipefail
SRC=$(cd "$(dirname "$0")" && pwd)
. "$SRC/tools/install/common.sh"
. "$SRC/tools/install/shortcuts.sh"

ASSUME_YES=0 PREFIX=$DEFAULT_PREFIX SHORTCUTS=1 DEPS=1
while [ $# -gt 0 ]; do
    case "$1" in
        --yes|-y) ASSUME_YES=1;;
        --prefix) [ $# -ge 2 ] && [ -n "$2" ] || fail "--prefix needs a folder"; PREFIX=$(realpath -m "$2"); shift;;
        --no-shortcuts) SHORTCUTS=0;;
        --no-deps) DEPS=0;;
        -h|--help) usage "$0"; exit 0;;
        *) fail "unknown option $1 (see --help)";;
    esac
    shift
done
[ "$(id -u)" != 0 ] || fail "run this as yourself, not as root: Ectolens installs for one person"
[ -f "$SRC/CMakeLists.txt" ] && [ -d "$SRC/src" ] || fail "run this from the Ectolens source folder"

# The install folder is Ectolens's alone: uninstall.sh deletes it whole, so never a folder
# that holds anything else
check_prefix "$PREFIX"
case "$PREFIX" in "$SRC"|"$SRC"/*) fail "$PREFIX is inside the source folder; choose a folder of its own, such as $DEFAULT_PREFIX";; esac
if [ -d "$PREFIX" ] && [ -n "$(ls -A "$PREFIX" 2>/dev/null)" ] && [ ! -f "$PREFIX/$MARKER" ]; then
    fail "$PREFIX already holds files that aren't an Ectolens install; choose an empty or new folder"
fi
if [ -f "$STATE/prefix" ] && [ "$(cat "$STATE/prefix")" != "$PREFIX" ]; then
    fail "Ectolens is already installed in $(cat "$STATE/prefix"). Uninstall that first, or install there again."
fi

# ---- 1. KDE Plasma, Qt and Frameworks, against the minimums the build itself asks for
step "Checking this system"
QT_MIN=$(sed -n 's/^set(QT_MIN_VERSION "\(.*\)")/\1/p' "$SRC/CMakeLists.txt")
KF_MIN=$(sed -n 's/^set(KF6_MIN_VERSION "\(.*\)")/\1/p' "$SRC/CMakeLists.txt")

# A library's version from its file name (libQt6Core.so.6.11.2), as the system has it
lib_version() {
    local f= d
    for d in /usr/lib64 /usr/lib/"$(uname -m)"-linux-gnu /usr/lib /lib64; do
        [ -e "$d/$1" ] && { f=$d/$1; break; }
    done
    [ -n "$f" ] || f=$(ldconfig -p 2>/dev/null | awk -v n="$1" '$1 == n {print $NF; exit}')
    [ -n "$f" ] || return 0
    f=$(readlink -f "$f"); f=${f##*/}
    echo "${f#"${1%%.so*}".so.}" | sed -n 's/^\(6\(\.[0-9]\+\)*\)$/\1/p'
}
at_least() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]; }

. /etc/os-release 2>/dev/null || true
DISTRO=${PRETTY_NAME:-this system}
QT=$(lib_version libQt6Core.so.6)
KF=$(lib_version libKF6CoreAddons.so.6)
PLASMA=$(lib_version libkwin.so.6)
say "$DISTRO: Plasma ${PLASMA:-not found}, Qt ${QT:-not found}, KDE Frameworks ${KF:-not found}"
[ -n "$PLASMA" ] || fail "KDE Plasma's KWin isn't installed here. Ectolens runs on KDE Plasma."
too_old=()
{ [ -n "$QT" ] && at_least "$QT" "$QT_MIN"; } || too_old+=("Qt $QT_MIN or newer (this system has ${QT:-none})")
{ [ -n "$KF" ] && at_least "$KF" "$KF_MIN"; } || too_old+=("KDE Frameworks $KF_MIN or newer (this system has ${KF:-none})")
if [ ${#too_old[@]} -gt 0 ]; then
    say "Ectolens needs:"; printf '  - %s\n' "${too_old[@]}"
    fail "$DISTRO is too old for Ectolens. Nothing was changed."
fi

# ---- 2. what the build needs, by package manager
FAMILY=
for id in ${ID:-} ${ID_LIKE:-}; do
    case "$id" in
        fedora|rhel) FAMILY=rpm-dnf;;
        suse|opensuse|opensuse-tumbleweed|opensuse-leap|opensuse-slowroll) FAMILY=rpm-zypper;;
        arch) FAMILY=pacman;;
        debian|ubuntu) FAMILY=apt;;
        *) continue;;
    esac
    break
done

# Fedora and openSUSE name what CMake and pkg-config look for, so one list serves both
RPM_NEEDS=(git cmake gcc-c++
    'cmake(Qt6Core)' 'cmake(Qt6GuiPrivate)' 'cmake(Qt6Quick)' 'cmake(Qt6QuickControls2)' 'cmake(Qt6WaylandClient)'
    'cmake(Qt6Multimedia)' 'cmake(Qt6Concurrent)' 'cmake(Qt6PrintSupport)' 'cmake(Qt6Test)'
    'cmake(KF6CoreAddons)' 'cmake(KF6WidgetsAddons)' 'cmake(KF6DBusAddons)' 'cmake(KF6Notifications)' 'cmake(KF6Config)'
    'cmake(KF6I18n)' 'cmake(KF6KIO)' 'cmake(KF6WindowSystem)' 'cmake(KF6GlobalAccel)' 'cmake(KF6XmlGui)'
    'cmake(KF6GuiAddons)' 'cmake(KF6KirigamiPlatform)' 'cmake(KF6StatusNotifierItem)' 'cmake(KF6Prison)'
    'cmake(KF6Crash)' 'cmake(KF6Purpose)' 'cmake(LayerShellQt)' 'cmake(PlasmaWaylandProtocols)'
    'pkgconfig(wayland-client)' 'pkgconfig(libpipewire-0.3)' 'pkgconfig(libavcodec)' 'pkgconfig(libavutil)'
    'pkgconfig(libavformat)' 'pkgconfig(libavfilter)' 'pkgconfig(libswscale)' 'pkgconfig(gbm)' 'pkgconfig(epoxy)'
    'pkgconfig(libdrm)' 'pkgconfig(libva)' 'pkgconfig(libva-drm)' 'pkgconfig(egl)' 'pkgconfig(tesseract)'
    'pkgconfig(libcurl)'   # Tesseract links it, and openSUSE's tesseract-devel doesn't bring it
    'pkgconfig(xcb-xfixes)' 'pkgconfig(xcb-image)' 'pkgconfig(xcb-util)' 'pkgconfig(xcb-cursor)' 'pkgconfig(xcb-randr)')
case "$FAMILY" in
    # the shader tool qsb: Fedora has it in qt6-qtshadertools, openSUSE with ShaderTools' CMake files
    rpm-dnf)    NEEDS=("${RPM_NEEDS[@]}" qt6-qtshadertools gettext ninja-build extra-cmake-modules tesseract-langpack-eng glib2)
                missing() { local p; for p in "$@"; do rpm -q --whatprovides "$p" >/dev/null 2>&1 || echo "$p"; done; }
                INSTALL=(sudo dnf install -y);;
    rpm-zypper) NEEDS=("${RPM_NEEDS[@]}" 'cmake(Qt6ShaderTools)' gettext-tools ninja kf6-extra-cmake-modules tesseract-ocr-traineddata-english glib2-tools)
                missing() { local p; for p in "$@"; do rpm -q --whatprovides "$p" >/dev/null 2>&1 || echo "$p"; done; }
                INSTALL=(sudo zypper --non-interactive install);;
    pacman)     NEEDS=(base-devel git cmake ninja extra-cmake-modules
                    qt6-base qt6-declarative qt6-wayland qt6-multimedia qt6-shadertools
                    kcoreaddons kwidgetsaddons kdbusaddons knotifications kconfig ki18n kio kwindowsystem kglobalaccel
                    kxmlgui kguiaddons kirigami kstatusnotifieritem prison kcrash purpose layer-shell-qt
                    plasma-wayland-protocols wayland libpipewire ffmpeg libepoxy libdrm libva mesa
                    tesseract tesseract-data-eng libxcb xcb-util xcb-util-image xcb-util-cursor glib2)
                missing() { pacman -T "$@" || true; }
                INSTALL=(sudo pacman -S --needed --noconfirm);;
    apt)        NEEDS=(build-essential git cmake ninja-build extra-cmake-modules gettext pkg-config
                    qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-declarative-private-dev
                    qt6-wayland-dev qt6-wayland-private-dev qt6-multimedia-dev qt6-shader-baker
                    libkf6coreaddons-dev libkf6widgetsaddons-dev libkf6dbusaddons-dev libkf6notifications-dev
                    libkf6config-dev libkf6i18n-dev libkf6kio-dev libkf6windowsystem-dev libkf6globalaccel-dev
                    libkf6xmlgui-dev libkf6guiaddons-dev libkirigami-dev 'libkf6statusnotifieritem-dev|kf6-kstatusnotifieritem-dev'
                    libkf6prison-dev libkf6crash-dev libkf6purpose-dev liblayershellqtinterface-dev
                    plasma-wayland-protocols libwayland-dev libpipewire-0.3-dev
                    libavcodec-dev libavfilter-dev libavformat-dev libavutil-dev libswscale-dev
                    libepoxy-dev libdrm-dev libgbm-dev libva-dev libegl-dev libtesseract-dev tesseract-ocr-eng
                    libxcb-cursor-dev libxcb-image0-dev libxcb-util-dev libxcb-xfixes0-dev libxcb-randr0-dev
                    libglib2.0-bin)
                # a|b: names differ between Debian, Ubuntu and KDE neon; the first one the package
                # lists know is the one asked for
                missing() { local p a c alts; for p in "$@"; do
                    IFS='|' read -ra alts <<<"$p"
                    for a in "${alts[@]}"; do [ "$(dpkg-query -W -f='${db:Status-Status}' "$a" 2>/dev/null)" = installed ] && continue 2; done
                    for a in "${alts[@]}"; do
                        c=$(apt-cache policy "$a" 2>/dev/null | sed -n 's/ *Candidate: //p')
                        [ -n "$c" ] && [ "$c" != "(none)" ] && { echo "$a"; continue 2; }
                    done
                    echo "${alts[0]}"
                done; }
                INSTALL=(sudo apt-get install -y);;
    *)          NEEDS=();;
esac

if [ "$DEPS" = 1 ]; then
    step "Checking what the build needs"
    if [ -z "$FAMILY" ]; then
        say "This script doesn't know $DISTRO's package names. Install what Spectacle and KPipeWire"
        say "need to build (Qt 6, KDE Frameworks 6, LayerShellQt, PipeWire, FFmpeg, Tesseract, CMake,"
        say "Ninja), then run ./install.sh --no-deps."
        exit 1
    fi
    mapfile -t MISSING < <(missing "${NEEDS[@]}")
    if [ ${#MISSING[@]} -eq 0 ]; then
        say "Everything is installed."
    else
        say "The build needs ${#MISSING[@]} packages from $DISTRO. The command is:"
        say ""; say "  ${INSTALL[*]} ${MISSING[*]}"; say ""
        if [ "$FAMILY" = apt ]; then
            ask "Refresh the package lists and run it now?" || fail "run that command, then ./install.sh again"
            sudo apt-get update
            mapfile -t MISSING < <(missing "${NEEDS[@]}")   # names known only after the refresh
        else
            ask "Run it now?" || fail "run that command, then ./install.sh again"
        fi
        "${INSTALL[@]}" "${MISSING[@]}"
        mapfile -t STILL < <(missing "${NEEDS[@]}")
        [ ${#STILL[@]} -eq 0 ] || fail "still missing after the install: ${STILL[*]}"
    fi
fi
command -v gdbus >/dev/null || [ "$SHORTCUTS" = 0 ] || fail "gdbus is missing (GLib's tools); it moves the shortcuts"

# ---- 3. build both, then put them in place: a build that fails leaves the install as it was
step "Building"
if [ ! -f "$SRC/3rdparty/kpipewire/CMakeLists.txt" ]; then
    [ -e "$SRC/.git" ] || fail "3rdparty/kpipewire is empty: clone with --recurse-submodules"
    git -C "$SRC" submodule update --init --recursive
fi
GEN=(); WANT="Unix Makefiles"; command -v ninja >/dev/null && GEN=(-G Ninja) WANT=Ninja
JOBS=${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc 2>/dev/null || echo 4)}
COMMON=(-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$PREFIX"
        -DKDE_INSTALL_SYSTEMDUSERUNITDIR="$PREFIX/lib/systemd/user")
# a build folder made with another generator can't be reused; start it again
for b in release-kpipewire release; do
    c="$SRC/build/$b/CMakeCache.txt"
    if [ -f "$c" ] && ! grep -qxF "CMAKE_GENERATOR:INTERNAL=$WANT" "$c"; then rm -rf "${SRC:?}/build/$b"; fi
done
cmake -S "$SRC/3rdparty/kpipewire" -B "$SRC/build/release-kpipewire" "${GEN[@]}" "${COMMON[@]}"
cmake --build "$SRC/build/release-kpipewire" -j "$JOBS"
# Ectolens builds against this KPipeWire, never the system's: it goes into a staging folder
# first, so nothing in the install changes before both builds have worked. Where its
# libraries land is this distribution's library folder (lib64 on Fedora and openSUSE, lib
# on Arch and Debian).
STAGE=$SRC/build/release-stage
rm -rf "$STAGE"
DESTDIR=$STAGE cmake --install "$SRC/build/release-kpipewire" >/dev/null
kpw_config=$(find "$STAGE$PREFIX" -name KPipeWireConfig.cmake -path '*/cmake/KPipeWire/*' | head -1)
kpw_lib=$(find "$STAGE$PREFIX" -name 'libKPipeWireRecord.so.*' | head -1)
[ -n "$kpw_config" ] && [ -n "$kpw_lib" ] || fail "KPipeWire's build made no library"
libdir=$(dirname "$kpw_lib"); libdir=${libdir#"$STAGE"}
cmake -S "$SRC" -B "$SRC/build/release" "${GEN[@]}" "${COMMON[@]}" \
      -DCMAKE_PREFIX_PATH="$STAGE$PREFIX" -DKPipeWire_DIR="$(dirname "$kpw_config")" -DCMAKE_INSTALL_RPATH="$libdir"
cmake --build "$SRC/build/release" -j "$JOBS"

step "Installing into $PREFIX"
stop_running "$PREFIX"
mkdir -p "$PREFIX/share/ectolens-install/tools/install" "$STATE"
echo "Ectolens, installed by install.sh from $SRC" > "$PREFIX/$MARKER"
echo "$PREFIX" > "$STATE/prefix"
cmake --install "$SRC/build/release-kpipewire" >/dev/null
cmake --install "$SRC/build/release" >/dev/null
rm -rf "$STAGE"
QT_QPA_PLATFORM=offscreen "$PREFIX/bin/ectolens" --version >/dev/null || fail "the built Ectolens doesn't start"
# the uninstaller travels with the install, so it works after this folder is gone
install -m 755 "$SRC/uninstall.sh" "$PREFIX/share/ectolens-install/uninstall.sh"
install -m 644 "$SRC/tools/install/common.sh" "$SRC/tools/install/shortcuts.sh" "$PREFIX/share/ectolens-install/tools/install/"
mcp=$(mcp_running_from "$PREFIX")
[ -z "$mcp" ] || say "$(echo $mcp | wc -w) AI agent connections (ectolens-mcp) keep the old version until their sessions restart."

# ---- 4. link what the session reads from ~/.local/share
step "Linking Ectolens into your session"
NEW_LINKS=$(mktemp); trap 'rm -f "$NEW_LINKS"' EXIT
# (found in the install folder, not in CMake's list: the shortcut file is a link made at install time)
while IFS= read -r f; do
    case "$f" in
        # icons only: the cache CMake's install builds beside them (icon-theme.cache) lists
        # Ectolens's icons alone, and linked into ~/.local/share it would hide the person's others
        "$PREFIX"/share/icons/*) case "$f" in *.svg|*.svgz|*.png) ;; *) continue;; esac; rel=${f#"$PREFIX"/share/};;
        "$PREFIX"/share/*) rel=${f#"$PREFIX"/share/};;
        "$PREFIX"/lib/systemd/user/*) rel=systemd/user/${f#"$PREFIX"/lib/systemd/user/};;
    esac
    dest=$DATA_HOME/$rel
    if [ -e "$dest" ] || [ -L "$dest" ]; then
        case "$(readlink "$dest" 2>/dev/null)" in
            "$PREFIX"/*) ;;
            *) fail "$dest already exists and isn't Ectolens's link; move it away and run this again";;
        esac
    fi
    mkdir -p "$(dirname "$dest")"
    ln -sfn "$f" "$dest"
    echo "$dest" >> "$NEW_LINKS"
done < <(cd "$PREFIX" && find "$PREFIX"/share/applications "$PREFIX"/share/dbus-1/services "$PREFIX"/share/knotifications6 \
             "$PREFIX"/share/kglobalaccel "$PREFIX"/share/icons "$PREFIX"/lib/systemd/user \( -type f -o -type l \) 2>/dev/null | sort)
# links an earlier install made that this one no longer has
if [ -f "$STATE/links" ]; then
    grep -vxF -f "$NEW_LINKS" "$STATE/links" | while IFS= read -r old; do
        [ -L "$old" ] && case "$(readlink "$old")" in "$PREFIX"/*) rm -f "$old";; esac
    done || true
fi
sort -u "$NEW_LINKS" > "$STATE/links"
say "$(wc -l < "$STATE/links") files linked into $DATA_HOME"
reload_session

# ---- 5. shortcuts
if [ "$SHORTCUTS" = 1 ]; then
    take_shortcuts
fi

step "Done"
say "Ectolens is installed in $PREFIX. Press Print Screen, or find it in the menu."
[ "${XDG_SESSION_TYPE:-wayland}" = wayland ] || say "It records on Plasma's Wayland session; this session is ${XDG_SESSION_TYPE}."
say "To remove it: $PREFIX/share/ectolens-install/uninstall.sh"
exit 0
}
