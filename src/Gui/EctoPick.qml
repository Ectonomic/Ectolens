/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/// A dropdown at the end of a menu row: the chosen app or service, with its arrow.
T.AbstractButton {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    property string iconName: ""
    property bool dimmed: false
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    implicitHeight: 28
    implicitWidth: row.implicitWidth + 18
    opacity: dimmed ? 0.55 : 1
    background: Rectangle {
        radius: 8
        color: root.hovered ? root.t.bgLift : root.t.bgMuted
        border.width: 1
        border.color: root.t.border
    }
    contentItem: Item {
        Row {
            id: row
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Kirigami.Icon {
                visible: root.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                source: root.iconName
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: root.t.fg
                font.family: EctoTheme.sans
                font.pixelSize: 12
                font.weight: Font.Medium
            }
            EctoChevron {
                anchors.verticalCenter: parent.verticalCenter
                color: root.t.fg
            }
        }
    }
}
