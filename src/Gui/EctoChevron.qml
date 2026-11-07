/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Shapes

/// The small down arrow that says a button has choices (10 px, 70% of the text color).
Shape {
    id: root
    property color color: "white"
    /// 0 points down; 270 points right (a submenu)
    property real angle: 0
    /// the room between its box's right end and the right end of its stroke, so a line can end it
    /// on its ink: pointing right, its point (6.8); left, its arms' ends (6.2); down or up, an arm's end (8)
    readonly property real endRoom: width - (angle === 270 ? 6.8 : angle === 90 ? 6.2 : 8) - stroke.strokeWidth / 2
    width: 10
    height: 10
    opacity: 0.7
    rotation: angle
    preferredRendererType: Shape.CurveRenderer
    ShapePath {
        id: stroke
        strokeColor: root.color
        strokeWidth: 1.3
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        startX: 2; startY: 3.8
        PathLine { x: 5; y: 6.8 }
        PathLine { x: 8; y: 3.8 }
    }
}
