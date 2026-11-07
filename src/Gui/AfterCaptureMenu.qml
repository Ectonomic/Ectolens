/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/**
 * Capture's arrow: one list of what happens after a capture (design proposal 2, step 4).
 * Every row is a check, so they stack, and a choice at the end of the row says how.
 * Save to file sits above Copy to clipboard because a file path needs the saved file:
 * File Path and Show in folder stay locked until Save to file is checked.
 */
EctoMenu {
    id: root
    minimumWidth: 372
    function save() {
        Settings.save()
    }

    EctoMenuHeader { text: i18nc("@title:menu", "After Capture") }
    EctoMenuRow {
        id: openRow
        text: i18nc("@option:check after a capture", "Open with")
        checked: Settings.afterCaptureOpen
        onToggled: on => { Settings.afterCaptureOpen = on; root.save() }
        EctoPick {
            id: appPick
            text: CaptureTools.appName(Settings.afterCaptureOpenApp)
            dimmed: !Settings.afterCaptureOpen
            onClicked: appMenu.openAt(appPick, "below")
        }
    }
    EctoMenuRow {
        text: i18nc("@option:check after a capture", "Save to file")
        checked: Settings.autoSaveImage
        onToggled: on => {
            Settings.autoSaveImage = on
            if (!on && Settings.afterCaptureCopyWhat === Settings.CopyTheFilePath) {
                Settings.afterCaptureCopyWhat = Settings.CopyTheImage // a path needs the file
            }
            root.save()
        }
        EctoSegmented {
            dimmed: !Settings.autoSaveImage
            model: [{value: Settings.SaveToScreenshots, text: i18nc("@item save to", "Screenshots")},
                    {value: Settings.AskWhereEachTime, text: i18nc("@item save to", "Ask where each time")}]
            value: Settings.afterCaptureSaveWhere
            onPicked: v => { Settings.afterCaptureSaveWhere = v; root.save() }
        }
    }
    EctoMenuRow {
        text: i18nc("@option:check after a capture", "Show in folder")
        locked: !Settings.autoSaveImage
        lockedReason: i18nc("@info", "Needs Save to file")
        checked: Settings.afterCaptureShowInFolder && Settings.autoSaveImage
        onToggled: on => { Settings.afterCaptureShowInFolder = on; root.save() }
    }
    EctoMenuRow {
        text: i18nc("@option:check after a capture", "Copy to clipboard")
        checked: Settings.afterCaptureCopy
        onToggled: on => { Settings.afterCaptureCopy = on; root.save() }
        EctoSegmented {
            dimmed: !Settings.afterCaptureCopy
            model: [{value: Settings.CopyTheImage, text: i18nc("@item copy", "Image")},
                    {value: Settings.CopyTheFilePath, text: i18nc("@item copy", "File Path"), enabled: Settings.autoSaveImage}]
            value: Settings.autoSaveImage ? Settings.afterCaptureCopyWhat : Settings.CopyTheImage
            onPicked: v => { Settings.afterCaptureCopyWhat = v; root.save() }
        }
    }
    EctoMenuRow {
        text: i18nc("@option:check after a capture", "Share")
        checked: Settings.afterCaptureShare
        onToggled: on => { Settings.afterCaptureShare = on; root.save() }
        EctoPick {
            id: sharePick
            text: CaptureTools.shareTargetName(Settings.afterCaptureShareTarget)
            dimmed: !Settings.afterCaptureShare
            onClicked: shareMenu.openAt(sharePick, "below")
        }
    }
    Item { width: 1; height: 4 }

    EctoMenu {
        id: appMenu
        minimumWidth: 220
        Repeater {
            model: appMenu.opened ? CaptureTools.openWithApps() : []
            EctoMenuItem {
                required property var modelData
                iconName: modelData.icon
                text: modelData.name
                tickColumn: true
                tick: modelData.id === Settings.afterCaptureOpenApp
                onClicked: {
                    Settings.afterCaptureOpenApp = modelData.id
                    Settings.afterCaptureOpen = true
                    root.save()
                }
            }
        }
    }
    EctoMenu {
        id: shareMenu
        minimumWidth: 220
        EctoMenuItem {
            text: i18nc("@item share service", "Ask each time")
            tickColumn: true
            tick: Settings.afterCaptureShareTarget === ""
            onClicked: {
                Settings.afterCaptureShareTarget = ""
                Settings.afterCaptureShare = true
                root.save()
            }
        }
        EctoMenuSeparator {}
        Repeater {
            model: shareMenu.opened ? CaptureTools.shareTargets() : []
            EctoMenuItem {
                required property var modelData
                iconName: modelData.icon
                text: modelData.name
                tickColumn: true
                tick: modelData.id === Settings.afterCaptureShareTarget
                onClicked: {
                    Settings.afterCaptureShareTarget = modelData.id
                    Settings.afterCaptureShare = true
                    root.save()
                }
            }
        }
    }
}
