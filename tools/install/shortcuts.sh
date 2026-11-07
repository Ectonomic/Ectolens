# Moving the capture shortcuts between Spectacle and Ectolens, for install.sh, uninstall.sh
# and the packages' ectolens-shortcuts. Sourced after common.sh, never run. The record of
# Spectacle's keys lives in $STATE (spectacle-shortcuts), so whichever installed Ectolens,
# the keys go back as they were.

# take_shortcuts: Spectacle's keys for the actions Ectolens has become Ectolens's, saved first
# (GIVE_BACK names what gives them back, for the message)
take_shortcuts() {
    step "Moving the capture shortcuts from Spectacle to Ectolens"
    mkdir -p "$STATE"
    if ! shortcut_service_up; then
        say "KDE's shortcut service isn't running (this isn't a Plasma session)."
        say "Log in to Plasma and run this again to move the shortcuts."
    else
        # KWin's shortcut service learns about Ectolens from the files just linked
        for _ in $(seq 50); do has_component "$ECTOLENS" && break; sleep 0.2; done
        has_component "$ECTOLENS" || fail "KDE's shortcut service hasn't picked up Ectolens; log out and in, then run this again"
        if [ -f "$STATE/spectacle-shortcuts" ] && [ ! -f "$STATE/moving" ]; then
            say "Already moved by an earlier run; left as they are."
        else
            finishing=0
            if [ -f "$STATE/spectacle-shortcuts" ]; then
                finishing=1   # an earlier run was cut short between saving the keys and moving them
                say "Finishing a move an earlier run didn't finish."
            else
                # Spectacle's keys for the actions Ectolens has, saved before anything changes
                mine=$(component_actions "$ECTOLENS")
                : > "$STATE/spectacle-shortcuts.new"
                if has_component "$SPECTACLE"; then
                    for action in $(component_actions "$SPECTACLE"); do
                        grep -qxF "$action" <<<"$mine" || continue
                        k=$(keys "$SPECTACLE" "$action") || continue
                        no_keys "$k" && continue
                        printf '%s\t%s\n' "$action" "$k" >> "$STATE/spectacle-shortcuts.new"
                    done
                fi
                touch "$STATE/moving"
                mv "$STATE/spectacle-shortcuts.new" "$STATE/spectacle-shortcuts"
            fi
            # Spectacle's keys first come free, then Ectolens takes them. When finishing, an
            # action Ectolens already has keys for was moved, or chosen since, and stays.
            moved=$(mktemp)
            while IFS=$'\t' read -r action k; do
                [ "$finishing" = 1 ] && ! no_keys "$(keys "$ECTOLENS" "$action")" && continue
                set_keys "$SPECTACLE" "$action" '@a(ai) []'
                set_keys "$ECTOLENS" "$action" "$k"
                printf '%s\t%s\n' "$action" "$k" >> "$moved"
            done < "$STATE/spectacle-shortcuts"
            # read back: Ectolens holds each moved key, Spectacle none of them
            bad=0
            while IFS=$'\t' read -r action k; do
                [ "$(keys "$ECTOLENS" "$action")" = "$k" ] || bad=1
                no_keys "$(keys "$SPECTACLE" "$action")" || bad=1
            done < "$moved"
            if [ "$bad" = 1 ]; then
                # what this run moved goes back to Spectacle; the record stays only if an earlier
                # run's move is still unfinished, so uninstall.sh can still give its keys back
                while IFS=$'\t' read -r action k; do set_keys "$ECTOLENS" "$action" '@a(ai) []'; done < "$moved"
                while IFS=$'\t' read -r action k; do set_keys "$SPECTACLE" "$action" "$k"; done < "$moved"
                rm -f "$moved"
                [ "$finishing" = 1 ] || rm -f "$STATE/spectacle-shortcuts" "$STATE/moving"
                fail "KDE's shortcut service didn't give Ectolens Spectacle's keys, so Spectacle keeps them. Ectolens is installed; run this again to retry."
            fi
            rm -f "$moved" "$STATE/moving"
            n=$(wc -l < "$STATE/spectacle-shortcuts")
            if [ "$n" -gt 0 ]; then say "$n shortcuts moved; ${GIVE_BACK:-uninstall.sh} gives them back to Spectacle."
            else say "Spectacle had no shortcuts here; Ectolens keeps its own (Print Screen and the rest)."; fi
        fi
    fi
}

# give_back_shortcuts: Spectacle gets back what take_shortcuts saved; sets kept_shortcuts=1
# when they had to stay saved (Spectacle gone, or no shortcut service and FORCE=1)
give_back_shortcuts() {
    kept_shortcuts=0
    if shortcut_service_up; then
        if [ -s "$STATE/spectacle-shortcuts" ]; then
            step "Giving the shortcuts back to Spectacle"
            if has_component "$SPECTACLE"; then
                # Ectolens lets go first (one key, one owner), then Spectacle takes them back
                while IFS=$'\t' read -r action k; do set_keys "$ECTOLENS" "$action" '@a(ai) []'; done < "$STATE/spectacle-shortcuts"
                while IFS=$'\t' read -r action k; do set_keys "$SPECTACLE" "$action" "$k"; done < "$STATE/spectacle-shortcuts"
                bad=0
                while IFS=$'\t' read -r action k; do [ "$(keys "$SPECTACLE" "$action")" = "$k" ] || bad=1; done < "$STATE/spectacle-shortcuts"
                if [ "$bad" = 1 ]; then
                    # undone: Ectolens keeps them, so Print Screen still does something
                    while IFS=$'\t' read -r action k; do set_keys "$SPECTACLE" "$action" '@a(ai) []'; done < "$STATE/spectacle-shortcuts"
                    while IFS=$'\t' read -r action k; do set_keys "$ECTOLENS" "$action" "$k"; done < "$STATE/spectacle-shortcuts"
                    fail "Spectacle didn't take its shortcuts back, so Ectolens keeps them and nothing was removed"
                fi
                say "$(wc -l < "$STATE/spectacle-shortcuts") shortcuts are Spectacle's again."
            else
                # the keys go free; the record stays for when Spectacle is back
                while IFS=$'\t' read -r action k; do set_keys "$ECTOLENS" "$action" '@a(ai) []'; done < "$STATE/spectacle-shortcuts"
                say "Spectacle isn't installed now, so its shortcuts can't be given back. They stay saved;"
                say "install Spectacle and run this again to give them back."
                kept_shortcuts=1
            fi
        fi
        # the shortcut service forgets Ectolens's actions, and with them its own default keys
        for action in $(component_actions "$ECTOLENS"); do
            kga --object-path /kglobalaccel --method org.kde.KGlobalAccel.unregister "$ECTOLENS" "$action" >/dev/null || true
        done
    elif [ -s "$STATE/spectacle-shortcuts" ]; then
        if [ "$FORCE" = 1 ]; then
            say "KDE's shortcut service isn't running, so Spectacle's shortcuts stay saved."
            kept_shortcuts=1
        else
            fail "KDE's shortcut service isn't running (this isn't a Plasma session). Run this in Plasma, or add --force."
        fi
    fi
}
