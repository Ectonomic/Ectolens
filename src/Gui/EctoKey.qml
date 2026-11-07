/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/// A key, drawn as a small chip ("Enter", "Esc", "Print").
Rectangle {
    readonly property var t: EctoTheme.of(contextWindow)
    property alias text: label.text
    implicitWidth: label.implicitWidth + 12
    implicitHeight: 19
    radius: 4
    color: t.bgLift
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
    Text {
        id: label
        anchors.centerIn: parent
        color: parent.t.fgMuted
        font.family: EctoTheme.sans
        font.pixelSize: 11
        font.weight: Font.Medium
    }
}
