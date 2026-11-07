/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/**
 * A slider: a track, and a round thumb that travels inside it (its edge meets the track's end at
 * 0 and at 1, never hanging past it, as a browser's range slider keeps its thumb), with
 * EctoTheme.sliderSidePadding of room past each end of the track. Put what the track shows
 * (a gradient, a checkerboard) inside it; with nothing inside, it is a plain track filled up to
 * the value. `vertical` stands it on end, 1 at the top.
 *
 * value: 0 to 1. moved(value) while it is dragged or clicked; settled() when it is let go.
 */
Item {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    property real value: 0
    property bool vertical: false
    property real padding: EctoTheme.sliderSidePadding
    property real trackThickness: 4
    property real thumbSize: 14
    property color trackColor: t.borderStrong
    property color fillColor: t.fg
    property color thumbColor: t.fg
    property color thumbBorderColor: t.bg
    property real thumbBorderWidth: 1
    default property alias trackContent: trackItem.data
    readonly property bool pressed: mouse.pressed
    readonly property bool hovered: mouse.containsMouse
    // the thumb's travel: from the track's start to its end, less the thumb
    readonly property real travel: Math.max(0, (vertical ? height : width) - 2 * padding - thumbSize)
    signal moved(real value)
    signal settled()

    implicitWidth: vertical ? thumbSize : 96
    implicitHeight: vertical ? 104 : thumbSize

    Rectangle {
        id: trackItem
        x: root.vertical ? (root.width - width) / 2 : root.padding
        y: root.vertical ? root.padding : (root.height - height) / 2
        width: root.vertical ? root.trackThickness : root.width - 2 * root.padding
        height: root.vertical ? root.height - 2 * root.padding : root.trackThickness
        radius: Math.min(width, height) / 2
        color: root.trackColor
        // the plain track's fill, up to the thumb's middle (hidden when the track shows its own content)
        Rectangle {
            visible: trackItem.children.length === 1
            x: 0
            y: root.vertical ? parent.height - height : 0
            width: root.vertical ? parent.width : root.thumbSize / 2 + root.value * root.travel
            height: root.vertical ? root.thumbSize / 2 + root.value * root.travel : parent.height
            radius: parent.radius
            color: root.fillColor
        }
    }
    Rectangle {
        id: thumb
        objectName: "sliderThumb"
        x: root.vertical ? (root.width - width) / 2 : root.padding + root.value * root.travel
        y: root.vertical ? root.padding + (1 - root.value) * root.travel : (root.height - height) / 2
        width: root.thumbSize
        height: root.thumbSize
        radius: width / 2
        color: root.thumbColor
        border.width: root.thumbBorderWidth
        border.color: root.thumbBorderColor
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        function at(m) {
            // the thumb's middle follows the pointer
            const along = root.vertical ? root.height - m.y : m.x
            const v = root.travel > 0 ? (along - root.padding - root.thumbSize / 2) / root.travel : 0
            root.moved(Math.max(0, Math.min(1, v)))
        }
        onPressed: m => at(m)
        onPositionChanged: m => { if (pressed) at(m) }
        onReleased: root.settled()
        onCanceled: root.settled()
    }
}
