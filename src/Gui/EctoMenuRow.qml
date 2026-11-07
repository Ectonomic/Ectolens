/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/**
 * A menu line that holds a check at the start and, if given, a choice at the end (After Capture's
 * rows; the options' checks). The check stands where a menu item's first mark does and the choice
 * ends where an item's key does (EctoTheme.controlPadding inside the line), so a menu of both
 * keeps one column at each side.
 */
Item {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    readonly property bool fillsWidth: true
    default property alias end: endSlot.data
    property alias check: check
    property alias text: check.text
    property alias checked: check.checked
    /// locked: can't be checked until something else is (Show in folder needs Save to file)
    property bool locked: false
    property string lockedReason: ""
    signal toggled(bool on)
    readonly property bool hasEnd: endSlot.children.length > 0
    // a line with a choice is a little taller, for the choice's own surface
    implicitHeight: hasEnd ? 40 : 32
    implicitWidth: EctoTheme.controlPadding * 2 + check.implicitWidth + (hasEnd ? 24 + endSlot.width : 0)
    opacity: locked ? 0.4 : 1
    EctoCheck {
        id: check
        x: EctoTheme.controlPadding
        anchors.verticalCenter: parent.verticalCenter
        enabled: !root.locked
        onToggled: root.toggled(checked)
    }
    Item {
        id: endSlot
        anchors.right: parent.right
        anchors.rightMargin: EctoTheme.controlPadding
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width
        height: childrenRect.height
    }
    HoverHandler { id: hover }
    // why a locked line can't be checked
    EctoToolTip {
        text: root.lockedReason
        shown: hover.hovered && root.locked
    }
}
