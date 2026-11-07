# Shared by install.sh and uninstall.sh: where an install lives, what it put where,
# and moving the capture shortcuts between Spectacle and Ectolens.
# Sourced, never run. Everything here works for the user running it; nothing needs root.

PATH=$PATH:/usr/sbin:/sbin

DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
STATE=${XDG_STATE_HOME:-$HOME/.local/state}/ectolens-install
DEFAULT_PREFIX=$HOME/.local/opt/ectolens

SPECTACLE=org.kde.spectacle.desktop
ECTOLENS=com.ectonomic.ectolens.desktop
# written into the install folder before anything is installed there; uninstall.sh removes
# only a folder that carries it
MARKER=share/ectolens-install/installed-by

say()  { printf '%s\n' "$*"; }
# the comment block at the top of a script, without its marks
usage() { awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$1"; }
step() { printf '\n== %s\n' "$*"; }
fail() { printf 'ectolens: %s\n' "$*" >&2; exit 1; }

# ask QUESTION: yes with --yes; otherwise a terminal answer, and no when nobody can answer
ask() {
    [ "${ASSUME_YES:-0}" = 1 ] && return 0
    [ -t 0 ] || return 1
    local reply; read -r -p "$1 [y/N] " reply
    case "$reply" in [yY]|[yY][eE][sS]) return 0;; esac
    return 1
}

# A folder uninstall.sh may delete whole: never the home folder, a folder above it, or one
# of the folders the session keeps its files in
check_prefix() {
    local p=${1%/} d
    [ -n "$p" ] && [ "$p" != / ] || fail "the install folder can't be /"
    case "$HOME/" in "$p"/*) fail "$p holds your home folder; choose a folder of its own, such as $DEFAULT_PREFIX";; esac
    for d in "$HOME/.local" "$HOME/.local/opt" "$HOME/.local/share" "$HOME/.local/state" "$HOME/.config" "$HOME/.cache" \
             "$DATA_HOME" "${XDG_CONFIG_HOME:-$HOME/.config}" "${XDG_CACHE_HOME:-$HOME/.cache}" "${XDG_STATE_HOME:-$HOME/.local/state}" "$STATE"; do
        [ "$p" = "${d%/}" ] && fail "$p is one of your session's own folders; choose a folder of its own, such as $DEFAULT_PREFIX"
    done
    return 0
}

# The programs named NAME running from PREFIX: their program file is PREFIX's, or was
# before it was replaced (the kernel marks a replaced one "(deleted)").
program_running_from() {
    local prefix=$1 name=$2 pid exe
    for pid in $(pgrep -u "$(id -u)" -x "$name" 2>/dev/null); do
        exe=$(readlink "/proc/$pid/exe" 2>/dev/null) || continue
        case "$exe" in "$prefix/bin/$name"|"$prefix/bin/$name (deleted)") echo "$pid";; esac
    done
}
running_from() { program_running_from "$1" ectolens; }
# the MCP servers AI agents' sessions start; they end with those sessions
mcp_running_from() { program_running_from "$1" ectolens-mcp; }

# Stop them, if the person agrees: a running Ectolens keeps the old program, which
# KWin then refuses screenshots, and the uninstall would leave it running.
stop_running() {
    local prefix=$1 pids
    pids=$(running_from "$prefix")
    [ -z "$pids" ] && return 0
    say "Ectolens is running (process $(echo $pids)). Anything open in it that isn't saved will be lost."
    ask "Close it now?" || fail "close Ectolens and run this again"
    local pid
    for pid in $pids; do
        # checked again just before: only a process still running our program is stopped
        case "$(readlink "/proc/$pid/exe" 2>/dev/null)" in "$prefix/bin/ectolens"|"$prefix/bin/ectolens (deleted)") kill -TERM "$pid";; esac
    done
    for _ in $(seq 50); do [ -z "$(running_from "$prefix")" ] && return 0; sleep 0.1; done
    fail "Ectolens did not close"
}

# Tell the session about added or removed files: menus and KWin's list of programs
# (KSycoca), the D-Bus services, and systemd's user units.
reload_session() {
    command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true
    if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}${XDG_RUNTIME_DIR:+x}" ] && command -v gdbus >/dev/null; then
        gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
            --method org.freedesktop.DBus.ReloadConfig >/dev/null 2>&1 || true
    fi
    systemctl --user daemon-reload >/dev/null 2>&1 || true
}

# ---- shortcuts, through KDE's shortcut service (org.kde.kglobalaccel, inside KWin on Wayland)

kga() { gdbus call --session --dest org.kde.kglobalaccel "$@" 2>/dev/null; }

shortcut_service_up() {
    command -v gdbus >/dev/null || return 1
    kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.allMainComponents >/dev/null
}

has_component() {
    kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.allMainComponents | grep -q "'$1'"
}

# The actions a component has keys for, by its own list
component_actions() {
    local path
    path=$(kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.getComponent "$1" | sed -n "s/^(objectpath '\(.*\)',)$/\1/p")
    [ -n "$path" ] || return 0
    kga --object-path "$path" --method org.kde.kglobalaccel.Component.shortcutNames \
        | sed "s/^(@as \[\],)$//; s/^(\[//; s/\],)$//" | tr ',' '\n' | tr -d "' " | sed '/^$/d'
}

# keys COMPONENT ACTION: the action's keys in GVariant text, as setForeignShortcutKeys takes them
keys() {
    local out
    out=$(kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.shortcutKeys "['$1', '$2', '', '']") || return 1
    out=${out#(}; out=${out%,)}
    printf '%s\n' "$out"
}

set_keys() {
    kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.setForeignShortcutKeys \
        "['$1', '$2', '', '']" "$3" >/dev/null
}

no_keys() { [ "$1" = '@a(ai) []' ] || [ "$1" = '[]' ]; }
