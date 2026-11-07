/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/**
 * Record's arrow opens a panel in place of a menu with three submenus (design proposal 2,
 * step 5): every choice in view, one press each, and the choices stay for next time.
 */
EctoMenu {
    id: root
    minimumWidth: 400
    // its form's words and choices start where a menu's lines start (EctoTheme.menuContentInset);
    // the last line is a menu item, its surface concentric with the panel's corners
    padding: EctoTheme.menuContentInset
    bottomPadding: EctoTheme.menuInset
    function save() {
        Settings.save()
    }
    readonly property bool sound: CaptureTools.formatHasSound(Settings.preferredVideoFormat)

    component Line: Item {
        id: line
        readonly property bool fillsWidth: true
        property string label: ""
        /// how tall its first choice is: the label stands centered on it
        property real firstHeight: 32
        default property alias content: slot.data
        implicitHeight: slot.childrenRect.height + 12
        Text {
            width: 82
            y: Math.round((line.firstHeight - height) / 2)
            horizontalAlignment: Text.AlignRight
            text: line.label
            color: root.t.fgSubtle
            font.family: EctoTheme.sans
            font.pixelSize: 12
        }
        Column {
            id: slot
            x: 94
            width: parent.width - 94
            spacing: 6
        }
    }

    Line {
        label: i18nc("@label what goes into the recording", "Include")
        firstHeight: systemAudio.height
        Column {
            spacing: 8
            EctoCheck {
                id: systemAudio
                text: i18nc("@option:check", "System audio")
                enabled: root.sound
                checked: Settings.videoRecordSystemAudio
                onToggled: { Settings.videoRecordSystemAudio = checked; root.save() }
            }
            EctoCheck {
                text: i18nc("@option:check", "Microphone")
                enabled: root.sound
                checked: Settings.videoRecordMicrophone
                onToggled: { Settings.videoRecordMicrophone = checked; root.save() }
            }
            EctoCheck {
                text: i18nc("@option:check", "Mouse pointer")
                checked: Settings.videoIncludePointer
                onToggled: { Settings.videoIncludePointer = checked; root.save() }
            }
            Text {
                visible: !root.sound
                text: i18nc("@info", "The selected video format does not support audio.")
                color: root.t.fgSubtle
                font.family: EctoTheme.sans
                font.pixelSize: 12
            }
        }
    }
    Line {
        label: i18nc("@label", "Quality")
        EctoSegmented {
            model: {
                let m = [{value: 0, text: i18nc("@item quality", "Small File")}, {value: 1, text: i18nc("@item quality", "High")}, {value: 2, text: i18nc("@item quality", "Best")}]
                if (Settings.videoQuality === 3) m.push({value: 3, text: i18nc("@item quality", "Custom")})
                return m
            }
            value: Settings.videoQuality
            onPicked: v => { Settings.videoQuality = v; root.save() }
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: CaptureTools.qualityNote(Settings.videoQuality)
            color: root.t.fgSubtle
            font.family: EctoTheme.sans
            font.pixelSize: 12
            lineHeight: 1.1
        }
    }
    Line {
        label: i18nc("@label", "Frame Rate")
        EctoSegmented {
            model: root.opened ? CaptureTools.frameRates() : []
            value: Settings.videoFramerate
            onPicked: v => { Settings.videoFramerate = v; root.save() }
        }
    }
    Line {
        label: i18nc("@label", "Format")
        EctoSegmented {
            model: root.opened ? CaptureTools.videoFormats() : []
            value: Settings.preferredVideoFormat
            onPicked: v => { Settings.preferredVideoFormat = v; root.save() }
        }
    }
    // the last line, a menu's: its line and its surface reach to the menu's inset, as in any menu
    Item {
        readonly property bool fillsWidth: true
        implicitHeight: divide.implicitHeight + settingsItem.implicitHeight
        EctoMenuSeparator {
            id: divide
            x: -EctoTheme.controlPadding
            width: parent.width + 2 * EctoTheme.controlPadding
        }
        EctoMenuItem {
            id: settingsItem
            objectName: "recordingSettings"
            y: divide.implicitHeight
            x: -EctoTheme.controlPadding
            width: parent.width + 2 * EctoTheme.controlPadding
            iconName: "configure"
            text: i18nc("@action:inmenu", "Recording Settings…")
            onClicked: {
                root.close()
                CaptureTools.openSettings("recording")
            }
        }
    }
}
