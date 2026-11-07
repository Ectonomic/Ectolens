/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/**
 * The frame label: the window's icon (a screen icon for a whole screen, none for an area
 * you drew) and the size in pixels. Its arrow opens the size menu. In the Ectolens window
 * it is information only: the size, in the faded gray of the undo and redo beside it.
 */
T.AbstractButton {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    property string iconName: ""
    property size pixelSize: Qt.size(0, 0)
    property string sizeText: pixelSize.width > 0 ? i18nc("@info width×height in pixels", "%1×%2", String(Math.round(pixelSize.width)), String(Math.round(pixelSize.height))) : ""
    property bool hasMenu: true
    property bool infoOnly: false
    property bool menuOpen: false
    focusPolicy: Qt.NoFocus
    hoverEnabled: !infoOnly
    enabled: !infoOnly
    implicitHeight: EctoTheme.buttonHeight
    // the window's icon sits as far from the label's rounded end as from its top and bottom,
    // concentric with it (an avatar in a chip); words alone get a control's room
    // (EctoTheme.controlPadding), and so does the end after them
    readonly property real leadPadding: iconName.length > 0 && !infoOnly ? (EctoTheme.buttonHeight - 18) / 2 : EctoTheme.controlPadding
    implicitWidth: leadPadding + Math.ceil(row.implicitWidth) + EctoTheme.controlPadding
    background: Rectangle {
        radius: height / 2
        color: root.infoOnly ? "transparent" : root.menuOpen || root.down ? root.t.bgLift : root.hovered ? root.t.bgMuted : "transparent"
    }
    contentItem: Item {
        Row {
            id: row
            x: root.leadPadding
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7
            Kirigami.Icon {
                visible: root.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                source: root.iconName
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.sizeText
                color: root.t.fg
                opacity: root.infoOnly ? 0.28 : 1
                font.family: EctoTheme.mono
                font.pixelSize: 12
                font.weight: root.infoOnly ? Font.Normal : Font.Medium
            }
            EctoChevron {
                visible: root.hasMenu && !root.infoOnly
                anchors.verticalCenter: parent.verticalCenter
                color: root.t.fg
            }
        }
    }
    EctoToolTip {
        text: i18nc("@info:tooltip", "Set the size of the capture")
        shown: root.hovered && root.hasMenu && !root.menuOpen && !root.infoOnly
    }
    Accessible.name: sizeText
}
