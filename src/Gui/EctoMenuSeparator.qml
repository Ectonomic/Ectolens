/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/// A line between groups in a menu.
Item {
    readonly property var t: EctoTheme.of(contextWindow)
    readonly property bool fillsWidth: true
    implicitHeight: 9
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 1
        color: parent.t.border
    }
}
