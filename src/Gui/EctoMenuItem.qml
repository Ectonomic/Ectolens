/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/**
 * One line of an EctoMenu: a tick or an icon, the words, then a key, a reason, a submenu arrow or
 * anything placed inside it (a line's width, an alignment) at the end.
 *
 * The menu's standard (EctoTheme): its hover surface is concentric with the
 * menu's corners (menuItemRadius); the tick or icon stands in a lead slot a control's room inside
 * it, so the words after any mark start in one column; the end stands as far from the line's end
 * as the first mark from its start.
 */
T.AbstractButton {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    readonly property bool fillsWidth: true
    property string iconName: ""
    property string shortcutText: ""
    /// a note after the words, in gray ("Full HD, 16:9")
    property string note: ""
    /// why it can't be picked, at the end of the line
    property string reason: ""
    property bool tick: false
    property bool tickColumn: false
    property bool submenu: false
    property bool closesMenu: true
    /// the words' face and size: the font menu draws each font's name in its own face
    property string wordsFamily: EctoTheme.sans
    property real wordsSize: 13
    /// what stands at the end of the line, in place of a key
    default property alias end: endSlot.data
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    implicitHeight: 32
    // the gap between the words and the end
    readonly property real endGap: 24
    readonly property real leadWidth: (tickColumn ? EctoTheme.leadSize + EctoTheme.leadGap : 0)
        + (iconName.length > 0 ? EctoTheme.leadSize + EctoTheme.leadGap : 0)
    readonly property real endWidth: endText.visible ? endText.implicitWidth
        : submenu ? chevron.width - chevron.endRoom
        : endSlot.childrenRect.width
    readonly property real noteWidth: noteText.visible ? EctoTheme.leadGap + noteText.implicitWidth : 0
    // its widest (a menu is as wide as its widest line wants), from the words' own width, not the
    // width they get, which comes from the menu
    implicitWidth: EctoTheme.controlPadding * 2 + leadWidth + Math.ceil(words.implicitWidth) + noteWidth
        + (endWidth > 0 ? endGap + endWidth : 0)
    opacity: enabled ? 1 : 0.55

    background: Rectangle {
        radius: EctoTheme.menuItemRadius
        color: root.enabled && (root.hovered || root.down || root.visualFocus) ? root.t.bgMuted : "transparent"
    }
    contentItem: Item {
        Row {
            id: row
            x: EctoTheme.controlPadding
            anchors.verticalCenter: parent.verticalCenter
            spacing: EctoTheme.leadGap
            Text {
                visible: root.tickColumn
                width: EctoTheme.leadSize
                anchors.verticalCenter: parent.verticalCenter
                text: root.tick ? "✓" : ""
                color: EctoTheme.accent
                font.family: EctoTheme.sans
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Kirigami.Icon {
                visible: root.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: EctoTheme.leadSize
                height: EctoTheme.leadSize
                source: root.iconName
                color: root.t.fg
                opacity: 0.72
            }
            Text {
                id: words
                // the font menu's names in their own face (the check of the look's fonts leaves them be)
                readonly property bool ownFace: root.wordsFamily !== EctoTheme.sans
                anchors.verticalCenter: parent.verticalCenter
                // long words give way to the end, never run under it
                width: Math.max(0, Math.min(implicitWidth, root.width - EctoTheme.controlPadding * 2 - root.leadWidth - root.noteWidth
                    - (root.endWidth > 0 ? root.endGap + root.endWidth : 0)))
                elide: Text.ElideRight
                text: root.text
                color: root.enabled ? root.t.fg : root.t.fgSubtle
                font.family: root.wordsFamily
                font.pixelSize: root.wordsSize
            }
            Text {
                id: noteText
                visible: root.note.length > 0
                anchors.verticalCenter: parent.verticalCenter
                // a note follows its words closer than the next mark would
                leftPadding: 8 - EctoTheme.leadGap
                text: root.note
                color: root.t.fgSubtle
                font.family: EctoTheme.sans
                font.pixelSize: 12
            }
        }
        Text {
            id: endText
            anchors.right: parent.right
            anchors.rightMargin: EctoTheme.controlPadding
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.submenu && (root.shortcutText.length > 0 || root.reason.length > 0)
            text: root.reason.length > 0 ? root.reason : root.shortcutText
            color: root.t.fgSubtle
            font.family: EctoTheme.sans
            font.pixelSize: root.reason.length > 0 ? 11 : 12
        }
        EctoChevron {
            id: chevron
            visible: root.submenu
            // its point, not its box, ends where a key's words end
            anchors.right: parent.right
            anchors.rightMargin: EctoTheme.controlPadding - endRoom
            anchors.verticalCenter: parent.verticalCenter
            angle: 270
            color: root.t.fg
        }
        Item {
            id: endSlot
            visible: !root.submenu && !endText.visible
            anchors.right: parent.right
            anchors.rightMargin: EctoTheme.controlPadding
            anchors.verticalCenter: parent.verticalCenter
            width: childrenRect.width
            height: childrenRect.height
        }
    }
    /// set by the EctoMenu that holds it
    property var menu: null
    onClicked: if (menu && closesMenu && !submenu) menu.close()
}
