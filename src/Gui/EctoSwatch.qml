/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/**
 * A marking color, as a 28 px round button: a 16 px chip in the color, a ring in it for a
 * stroke (beside a fill's chip, the two read apart: Inkscape's and Figma's convention), or a
 * struck-through chip when there is none. A press opens the color menu.
 */
T.AbstractButton {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    property color color: "transparent"
    /// draw it as a ring: the stroke of a shape that also has a fill
    property bool ring: false
    property bool menuOpen: false
    readonly property bool none: color.a === 0
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    implicitHeight: EctoTheme.buttonHeight
    implicitWidth: EctoTheme.buttonHeight
    background: Rectangle {
        radius: height / 2
        color: root.down || root.menuOpen ? root.t.bgLift : root.hovered ? root.t.bgMuted : "transparent"
    }
    contentItem: Item {
        Rectangle {
            anchors.centerIn: parent
            width: 16
            height: 16
            radius: 5
            color: root.none || root.ring ? "transparent" : root.color
            border.width: root.ring && !root.none ? 3.5 : 1
            border.color: root.ring && !root.none ? root.color
                : root.none ? root.t.borderStrong : Qt.rgba(1, 1, 1, root.t.light ? 0 : 0.18)
            // a dark ring on the dark rail needs an edge, as a dark chip has
            Rectangle {
                visible: root.ring && !root.none && !root.t.light
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.18)
            }
            // a light chip on the light window needs an edge
            Rectangle {
                visible: root.t.light && !root.none
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(0, 0, 0, 0.14)
            }
            // none: struck through
            Rectangle {
                visible: root.none
                anchors.centerIn: parent
                width: 1.5
                height: 18
                rotation: 45
                color: root.t.fgSubtle
            }
        }
    }
    EctoToolTip {
        text: root.text
        shown: root.hovered && !root.menuOpen
    }
    Accessible.name: text
}
