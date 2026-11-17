/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Shapes
import org.kde.spectacle.private

/**
 * Measure's drawing: a line between two points, a dot at each end, and its length in the
 * picture's pixels above it. `start` and `end` are in this item's coordinates; `pixelRatio`
 * turns a distance here into the picture's pixels (the screen's scale in the overlay, the
 * picture's scale over the zoom in the Ectolens window).
 */
Item {
    id: root
    property point start: Qt.point(-1, -1)
    property point end: Qt.point(-1, -1)
    property real pixelRatio: 1
    readonly property bool hasLine: start.x >= 0
    readonly property var t: EctoTheme.of(contextWindow)

    Shape {
        visible: root.hasLine
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: EctoTheme.accent
            strokeWidth: 1.5
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: root.start.x
            startY: root.start.y
            PathLine { x: root.end.x; y: root.end.y }
        }
    }
    Repeater {
        model: root.hasLine ? [root.start, root.end] : []
        Rectangle {
            required property point modelData
            x: modelData.x - 4
            y: modelData.y - 4
            width: 8
            height: 8
            radius: 4
            color: EctoTheme.accent
            border.width: 1
            border.color: "#0a0a0a"
        }
    }
    EctoRow {
        id: label
        objectName: "measureLabel"
        readonly property real dx: Math.abs(root.end.x - root.start.x) * root.pixelRatio
        readonly property real dy: Math.abs(root.end.y - root.start.y) * root.pixelRatio
        visible: root.hasLine && (dx > 0 || dy > 0)
        z: 1
        x: (root.start.x + root.end.x) / 2 - width / 2
        y: Math.min(root.start.y, root.end.y) - height - 10
        EctoLabel {
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: i18nc("@info %1 the distance, %2×%3 across and down, in pixels", "%1 px  ·  %2 × %3",
                            String(Math.round(Math.hypot(label.dx, label.dy))), String(Math.round(label.dx)), String(Math.round(label.dy)))
                color: root.t.fg
                font.family: EctoTheme.mono
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
    }
}
