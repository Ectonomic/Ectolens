#!/bin/bash
# Remove Ectolens installed by install.sh, and give the capture shortcuts back to Spectacle.
#
#   uninstall.sh [--yes] [--remove-settings | --keep-settings] [--force]
#
# Gives Spectacle back the shortcuts install.sh moved (as they were then), removes the
# links in ~/.local/share, the install folder and Ectolens's cache. It asks before removing
# your Ectolens settings; --yes alone keeps them. --force goes on outside a Plasma session,
# where the shortcuts can't be given back: they stay saved, with a copy of this script, and
# running it again in Plasma gives them back.
# The whole script sits in one block, read before it runs: it may delete its own file.
{
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/install/common.sh"
. "$HERE/tools/install/shortcuts.sh"

ASSUME_YES=0 SETTINGS=ask FORCE=0
while [ $# -gt 0 ]; do
    case "$1" in
        --yes|-y) ASSUME_YES=1;;
        --remove-settings) SETTINGS=remove;;
        --keep-settings) SETTINGS=keep;;
        --force) FORCE=1;;
        -h|--help) usage "$0"; exit 0;;
        *) fail "unknown option $1 (see --help)";;
    esac
    shift
done
[ "$(id -u)" != 0 ] || fail "run this as yourself, not as root"
if [ -f "$STATE/prefix" ]; then
    PREFIX=$(cat "$STATE/prefix")
    check_prefix "$PREFIX"
    # never delete a folder install.sh didn't make
    [ ! -e "$PREFIX" ] || [ -f "$PREFIX/$MARKER" ] || fail "$PREFIX doesn't carry install.sh's mark ($MARKER), so nothing was removed"
    stop_running "$PREFIX"
elif [ -s "$STATE/spectacle-shortcuts" ]; then
    PREFIX=   # removed by an earlier run; only the shortcuts are left to give back
else
    fail "no Ectolens install by install.sh was found for you ($STATE)"
fi

# ---- shortcuts back to Spectacle, as they were before install.sh moved them
give_back_shortcuts

# the record and a copy of this script stay for later, before the install's copy goes
if [ "$kept_shortcuts" = 1 ] && [ "$HERE" != "$(realpath -m "$STATE")" ]; then
    mkdir -p "$STATE/tools/install"
    install -m 755 "$HERE/uninstall.sh" "$STATE/uninstall.sh"
    install -m 644 "$HERE/tools/install/common.sh" "$HERE/tools/install/shortcuts.sh" "$STATE/tools/install/"
fi

# ---- the links install.sh made, and the install folder
if [ -n "$PREFIX" ]; then
    step "Removing Ectolens"
    if [ -f "$STATE/links" ]; then
        while IFS= read -r link; do
            [ -L "$link" ] && case "$(readlink "$link")" in "$PREFIX"/*) rm -f "$link";; esac
        done < "$STATE/links"
    fi
    mcp=$(mcp_running_from "$PREFIX")
    rm -rf "$PREFIX"
    # Ectolens's cache (compiled QML) is rebuilt on the next start, so it goes with the program
    rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/ectolens"
    reload_session
    say "Removed $PREFIX and its links."
    [ -z "$mcp" ] || say "$(echo $mcp | wc -w) AI agent connections (ectolens-mcp) still run until their sessions end."
fi

# ---- settings
SETTINGS_FILES=()
for f in "${XDG_CONFIG_HOME:-$HOME/.config}"/ectolensrc "${XDG_CONFIG_HOME:-$HOME/.config}"/ectolens.notifyrc; do
    [ -e "$f" ] && SETTINGS_FILES+=("$f")
done
if [ ${#SETTINGS_FILES[@]} -gt 0 ]; then
    if [ "$SETTINGS" = ask ]; then
        if [ "$ASSUME_YES" = 0 ] && [ -t 0 ] && ask "Remove your Ectolens settings too (${SETTINGS_FILES[*]})?"; then
            SETTINGS=remove
        else
            SETTINGS=keep
        fi
    fi
    if [ "$SETTINGS" = remove ]; then rm -f "${SETTINGS_FILES[@]}"; say "Settings removed."
    else say "Your settings are kept in ${SETTINGS_FILES[*]}."; fi
fi

if [ "$kept_shortcuts" = 1 ]; then
    rm -f "$STATE/links" "$STATE/prefix"
    say "To give Spectacle its shortcuts back later: $STATE/uninstall.sh"
else
    rm -rf "$STATE"
fi
say "Ectolens is uninstalled. Spectacle is untouched."
exit 0
}
