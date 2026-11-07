/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/// A faded hairline between groups in a row, fading at both ends.
/// `horizontal` lays it down, for a rail standing on end (the marking options beside the frame).
Item {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    property bool horizontal: false
    implicitWidth: horizontal ? EctoTheme.buttonHeight : 15
    implicitHeight: horizontal ? 13 : EctoTheme.buttonHeight
    anchors.verticalCenter: parent && !horizontal ? parent.verticalCenter : undefined
    Rectangle {
        anchors.centerIn: parent
        width: root.horizontal ? 14 : 1
        height: root.horizontal ? 1 : 14
        gradient: Gradient {
            orientation: root.horizontal ? Gradient.Horizontal : Gradient.Vertical
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.3; color: root.t.borderStrong }
            GradientStop { position: 0.7; color: root.t.borderStrong }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
}
