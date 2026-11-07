/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/// Pin: the capture floating above your windows. Drag to move, Esc or ✕ to close, Ctrl+C to copy.
Item {
    id: root
    // the overlay's look (Settings › Appearance): a pin is a piece of the overlay left behind
    readonly property var t: EctoTheme.overlay
    width: pinWindow.imageSize.width + 32
    height: pinWindow.imageSize.height + 32
    Kirigami.ShadowedRectangle {
        x: 16
        y: 16
        width: pinWindow.imageSize.width
        height: pinWindow.imageSize.height
        radius: 4
        color: root.t.bgSubtle
        border.width: 1
        border.color: hover.hovered ? EctoTheme.accent : root.t.borderStrong
        shadow.size: 18
        shadow.yOffset: 6
        shadow.color: Qt.rgba(0, 0, 0, 0.55)
        Image {
            anchors.fill: parent
            anchors.margins: 1
            source: pinWindow.imageUrl
            smooth: true
            mipmap: true
        }
        HoverHandler { id: hover }
        DragHandler {
            id: drag
            target: null
            // The point held stays at the same place in the window as the window follows
            // the pointer, so each move is how far the pointer is from that point.
            property point held
            onActiveChanged: held = centroid.scenePosition
            onCentroidChanged: if (active) {
                const p = centroid.scenePosition
                pinWindow.moveBy(p.x - held.x, p.y - held.y)
            }
        }
        // ✕ in the corner while the pointer is over it
        Rectangle {
            visible: hover.hovered
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            width: 24
            height: 24
            radius: 12
            color: close.containsMouse ? root.t.bgLift : root.t.bg
            border.width: 1
            border.color: root.t.border
            Kirigami.Icon {
                anchors.centerIn: parent
                width: 14
                height: 14
                source: "window-close"
                color: root.t.fg
            }
            MouseArea {
                id: close
                anchors.fill: parent
                hoverEnabled: true
                onClicked: pinWindow.unpin()
            }
        }
    }
}
