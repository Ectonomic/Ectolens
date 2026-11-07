/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/// A button with an arrow beside it for its choices (Capture, Record, Extract Text, Copy, Save, New Capture).
Row {
    id: root
    property alias text: main.text
    property alias iconName: main.iconName
    property alias shortcutText: main.shortcutText
    property alias toolTipText: main.toolTipText
    property alias iconOnly: main.iconOnly
    property alias button: main
    property alias arrowButton: arrow
    property bool menuOpen: false
    property string arrowToolTip: ""
    signal clicked()
    signal arrowClicked()
    spacing: -6
    EctoButton {
        id: main
        onClicked: root.clicked()
    }
    EctoButton {
        id: arrow
        objectName: root.objectName.length > 0 ? root.objectName + "Arrow" : ""
        implicitWidth: 22
        iconName: "arrow-down"
        menuOpen: root.menuOpen
        toolTipText: root.arrowToolTip
        Accessible.role: Accessible.ButtonMenu
        onClicked: root.arrowClicked()
        contentItem: Item {
            EctoChevron {
                anchors.centerIn: parent
                color: arrow.t.fg
            }
        }
    }
}
