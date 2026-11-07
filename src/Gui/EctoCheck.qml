/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T
import org.kde.spectacle.private

/// The family's checkbox: 15 px, 4 px corners; checked fills with the accent and shows a dark tick.
/// The box stands in a line's lead slot (EctoTheme.leadSize), so its words start in the column the
/// words after an icon or a tick start in.
T.AbstractButton {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    checkable: true
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    implicitHeight: 22
    implicitWidth: leftPadding + rightPadding + (text.length > 0 ? label.x + Math.ceil(label.implicitWidth) : box.width)
    opacity: enabled ? 1 : 0.4
    contentItem: Item {
        Rectangle {
            id: box
            anchors.verticalCenter: parent.verticalCenter
            width: 15
            height: 15
            radius: 4
            color: root.checked ? EctoTheme.accent : root.t.bg
            border.width: 1
            border.color: root.checked ? EctoTheme.accent : root.hovered ? root.t.fgSubtle : root.t.borderStrong
            Shape {
                visible: root.checked
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: EctoTheme.onAccent
                    strokeWidth: 2
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    startX: 3.8; startY: 7.6
                    PathLine { x: 6.4; y: 10.2 }
                    PathLine { x: 11.2; y: 4.8 }
                }
            }
        }
        Text {
            id: label
            x: EctoTheme.leadSize + EctoTheme.leadGap
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.t.fg
            font.family: EctoTheme.sans
            font.pixelSize: 13
        }
    }
}
