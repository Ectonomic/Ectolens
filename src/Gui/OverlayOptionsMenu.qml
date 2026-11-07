/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/**
 * The gear at the end of the overlay's bottom row (and on the hint line while pointing):
 * today's Options menu, minus its Recording Settings part (now in Record's panel), plus
 * Close at the bottom (design proposal 2, section 07). Esc still cancels.
 */
EctoMenu {
    id: root
    minimumWidth: 320
    function toggle(name, on) {
        Settings[name] = on
        Settings.save()
    }
    component Check: EctoMenuRow {
        id: c
        property string setting
        checked: Settings[c.setting]
        onToggled: on => root.toggle(c.setting, on)
    }
    EctoMenuHeader { text: i18nc("@title:menu", "Screenshot Settings") }
    Check { setting: "includePointer"; text: i18nc("@option:check", "Include mouse pointer") }
    Check { setting: "includeDecorations"; text: i18nc("@option:check", "Include window titlebar and borders") }
    Check { setting: "includeShadow"; text: i18nc("@option:check", "Include window shadow") }
    Check { setting: "quitAfterSaveCopyExport"; text: i18nc("@option:check", "Quit after manual Save or Copy") }
    EctoDelay {}
    EctoMenuSeparator {}
    EctoMenuItem {
        iconName: "configure"
        text: i18nc("@action:inmenu", "Configure Ectolens…")
        shortcutText: CaptureTools.keyText("Ctrl+Shift+,")
        onClicked: CaptureTools.openSettings()
    }
    EctoMenuSeparator {}
    EctoMenuItem {
        objectName: "closeOverlay"
        iconName: "window-close"
        text: i18nc("@action:inmenu close the overlay", "Close")
        shortcutText: i18nc("@info key name", "Esc")
        onClicked: SpectacleCore.cancelScreenshot()
    }
}
