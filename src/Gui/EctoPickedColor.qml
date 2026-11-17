/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/**
 * Pick Color's readout: the color under the pointer, its hex, and that a click copies it.
 * Shown under the magnifier, in the overlay and in the Ectolens window.
 */
EctoRow {
    id: root
    property color color: "transparent"
    readonly property var t: EctoTheme.of(contextWindow)
    EctoLabel {
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: 4
            color: root.color
            border.width: 1
            border.color: root.t.borderStrong
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: EctoTheme.hex(root.color)
            color: root.t.fg
            font.family: EctoTheme.mono
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 2
            text: i18nc("@info", "Click to copy")
            color: root.t.fgSubtle
            font.family: EctoTheme.sans
            font.pixelSize: 12
        }
    }
}
