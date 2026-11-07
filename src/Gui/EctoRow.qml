/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/**
 * A row of the overlay: the family's menu surface (bg-subtle, a border), fully round,
 * 36 px tall, floating with a soft shadow (design proposal 2, ".ftb").
 * Put buttons and separators inside; they lay out left to right, EctoTheme.rowSpacing apart.
 *
 * Its padding is EctoTheme.rowInset on every side, its ends too, so a button at either end
 * is concentric with the row's rounded end. Words or an icon at an end
 * stand in an EctoLabel, which keeps a button's room around them; no spacer at an end.
 * The `anatomy` test command checks every row against this.
 */
Item {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    default property alias content: row.data
    property real sidePadding: EctoTheme.rowInset
    property bool floating: true
    /// a tool's own row (its options, Crop's row): it lines up with the tool's button (the `anatomy` check)
    property bool followsTool: false
    property alias spacing: row.spacing
    property alias background: bg
    implicitWidth: row.implicitWidth + sidePadding * 2
    implicitHeight: EctoTheme.rowHeight

    Kirigami.ShadowedRectangle {
        id: bg
        anchors.fill: parent
        radius: height / 2
        color: root.t.bgSubtle
        border.width: 1
        border.color: root.t.border
        shadow.size: root.floating ? 22 : 0
        shadow.yOffset: root.floating ? 8 : 0
        shadow.color: root.t.shadow
    }
    // Clicks on the row's own surface stop here instead of reaching the overlay under it.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        cursorShape: Qt.ArrowCursor
    }
    Row {
        id: row
        x: root.sidePadding
        anchors.verticalCenter: parent.verticalCenter
        spacing: EctoTheme.rowSpacing
    }
}
