/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>
 * SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private
import org.kde.kquickimageeditor.ectolens

/**
 * The magnifier: the pixels under the pointer three times their size, for landing a frame's
 * edge (Ctrl while framing) or Pick Color on one exact pixel.
 *
 * Ectolens: drawn as the look's other floating surfaces (a rounded panel with a border and a
 * soft shadow) instead of a bare square with a crosshair in the system's focus color. The
 * pixel it aims at is outlined dark and light, so it shows on any color, with thin guides
 * leading to it, and the pixel's place on its screen is read out under it. Ctrl and the wheel
 * zoom it in and out (`factor`); the panel keeps its size and shows fewer or more pixels.
 */
Item {
    id: root
    required property AnnotationViewport viewport
    required property point targetPoint
    /// the screen the target is on, in the overlay's coordinates: the readout counts from its corner
    property rect screenRect: Qt.rect(0, 0, 0, 0)
    /// how many times their size the pixels show
    property int factor: 3
    readonly property var t: EctoTheme.of(contextWindow)
    /// the panel's size, whatever the zoom
    readonly property int size: 201
    // the pixels across that fill it: an odd number, so one pixel sits in the middle
    readonly property int span: {
        const n = Math.ceil(size / factor)
        return n % 2 === 1 ? n : n + 1
    }
    readonly property real dpr: contextWindow.devicePixelRatio
    /// the aimed pixel, in the screen's own pixels
    readonly property point pixel: Qt.point(Math.floor((targetPoint.x - screenRect.x) * dpr),
                                            Math.floor((targetPoint.y - screenRect.y) * dpr))

    objectName: "magnifierView"
    implicitWidth: size
    implicitHeight: size

    // the magnified pixels and the aim, drawn square, then given the panel's round corners below
    Item {
        id: zoomed
        width: root.size
        height: root.size
        visible: false

        ShaderEffectSource {
            id: pixels
            // the magnified pixels, centered; what runs past the panel is cut off by it
            x: Math.round((root.size - width) / 2)
            y: x
            width: root.span * root.factor
            height: width
            sourceItem: root.viewport
            // the position needs an offset of half the size minus half a logical pixel
            sourceRect: Qt.rect((root.targetPoint.x - root.viewport.viewportRect.x) - (root.span - 1) / 2,
                                (root.targetPoint.y - root.viewport.viewportRect.y) - (root.span - 1) / 2,
                                root.span, root.span)
            smooth: false
        }

        // the aimed pixel's middle
        readonly property real cx: pixels.x + (root.span - 1) / 2 * root.factor + root.factor / 2
        readonly property real cy: cx
        // guides: thin lines toward the aimed pixel, stopping short of it
        readonly property real gap: root.factor + 3
        Repeater {
            model: [
                {x: zoomed.cx, y: 0, w: 1, h: zoomed.cy - zoomed.gap},
                {x: zoomed.cx, y: zoomed.cy + zoomed.gap, w: 1, h: zoomed.height - zoomed.cy - zoomed.gap},
                {x: 0, y: zoomed.cy, w: zoomed.cx - zoomed.gap, h: 1},
                {x: zoomed.cx + zoomed.gap, y: zoomed.cy, w: zoomed.width - zoomed.cx - zoomed.gap, h: 1},
            ]
            delegate: Rectangle {
                required property var modelData
                x: modelData.x - (modelData.w === 1 ? 0.5 : 0)
                y: modelData.y - (modelData.h === 1 ? 0.5 : 0)
                width: modelData.w
                height: modelData.h
                color: Qt.rgba(1, 1, 1, 0.55)
                // a dark edge beside each guide keeps it visible on white
                Rectangle {
                    x: parent.width === 1 ? 1 : 0
                    y: parent.height === 1 ? 1 : 0
                    width: parent.width
                    height: parent.height
                    color: Qt.rgba(0, 0, 0, 0.35)
                }
            }
        }
        // the aimed pixel: a light ring inside a dark one, one magnified pixel inside them
        Rectangle {
            x: zoomed.cx - root.factor / 2 - 2
            y: zoomed.cy - root.factor / 2 - 2
            width: root.factor + 4
            height: root.factor + 4
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.8)
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                color: "transparent"
                border.width: 1
                border.color: "white"
            }
        }
    }
    ShaderEffectSource {
        id: zoomedTexture
        sourceItem: zoomed
        width: zoomed.width
        height: zoomed.height
        visible: false
    }

    Kirigami.ShadowedTexture {
        objectName: "magnifierPanel"
        anchors.fill: parent
        source: zoomedTexture
        radius: EctoTheme.menuRadius
        color: root.t.bgSubtle
        border.width: 1
        border.color: root.t.borderStrong
        shadow.size: 22
        shadow.yOffset: 8
        shadow.color: root.t.shadow
    }

    // where the aimed pixel is on its screen
    Rectangle {
        objectName: "magnifierReadout"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: EctoTheme.rowInset + 2
        width: readout.implicitWidth + EctoTheme.controlPadding
        height: readout.implicitHeight + 4
        radius: height / 2
        color: Qt.rgba(root.t.bg.r, root.t.bg.g, root.t.bg.b, 0.85)
        border.width: 1
        border.color: root.t.border
        visible: root.screenRect.width > 0
        Text {
            id: readout
            anchors.centerIn: parent
            text: root.pixel.x + ", " + root.pixel.y + "  ·  " + root.factor + "×"
            color: root.t.fg
            font.family: EctoTheme.mono
            font.pixelSize: 11
            font.weight: Font.Medium
        }
    }
}
