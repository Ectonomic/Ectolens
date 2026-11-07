/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/**
 * The font menu of the marking options: a search field, Geist and Geist Mono first (Ectolens
 * ships them), then every font on the computer, each name drawn in its own face, and the
 * weight below. It replaces KDE's font dialog, so the choosing stays in Ectolens's own look.
 *
 * openFor(button, font, side); picked(font) says what was chosen, at the size it had.
 */
EctoMenu {
    id: root
    property font current: Qt.font({family: "Geist"})
    signal picked(font font)
    // the menu's own padding (EctoTheme.menuInset): the search field above and the weight below are
    // lines of it, their corners concentric with the menu's; the weight's words set how wide it is
    minimumWidth: 244
    readonly property var pinned: ["Geist", "Geist Mono"]
    readonly property var families: {
        const all = Qt.fontFamilies().filter(f => !f.startsWith(".") && pinned.indexOf(f) < 0)
        const q = search.text.trim().toLowerCase()
        const match = f => q.length === 0 || f.toLowerCase().indexOf(q) >= 0
        return pinned.filter(match).concat(q.length === 0 ? ["—"] : [], all.filter(match))
    }

    function openFor(button, font, side) {
        root.current = font
        search.text = ""
        root.toggleAt(button, side)
        list.positionViewAtBeginning()
    }
    function choose(family, weight) {
        const f = Qt.font({family: family, pointSize: root.current.pointSize, weight: weight, italic: root.current.italic})
        root.current = f
        root.picked(f)
    }

    // search: the first line, its words where every line's start
    Item {
        readonly property bool fillsWidth: true
        implicitWidth: 232
        implicitHeight: field.height + EctoTheme.menuInset
        Rectangle {
            id: field
            width: parent.width
            height: 32
            radius: EctoTheme.menuItemRadius
            color: root.t.bgMuted
            border.width: 1
            border.color: search.activeFocus ? EctoTheme.accent : root.t.border
            TextInput {
                id: search
                objectName: "fontSearch"
                anchors.fill: parent
                leftPadding: EctoTheme.controlPadding
                rightPadding: EctoTheme.controlPadding
                verticalAlignment: TextInput.AlignVCenter
                color: root.t.fg
                selectionColor: EctoTheme.accent
                selectedTextColor: EctoTheme.onAccent
                font.family: EctoTheme.sans
                font.pixelSize: 13
                clip: true
                Text {
                    visible: search.text.length === 0
                    anchors.fill: parent
                    leftPadding: EctoTheme.controlPadding
                    verticalAlignment: Text.AlignVCenter
                    text: i18nc("@info:placeholder", "Search fonts")
                    color: root.t.fgSubtle
                    font: search.font
                }
            }
        }
    }
    ListView {
        id: list
        objectName: "fontList"
        readonly property bool fillsWidth: true
        implicitWidth: 232
        implicitHeight: Math.min(contentHeight, 280)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.families
        delegate: Item {
            id: line
            required property string modelData
            readonly property bool divider: modelData === "—"
            // for the off-screen test: font-GeistMono
            objectName: "font-" + modelData.replace(/\W/g, "")
            width: ListView.view.width
            height: divider ? divide.implicitHeight : item.implicitHeight
            EctoMenuSeparator {
                id: divide
                visible: line.divider
                width: parent.width
            }
            // each font's name in its own face, a menu line like any other
            EctoMenuItem {
                id: item
                visible: !line.divider
                width: parent.width
                tickColumn: true
                tick: root.current.family === line.modelData
                text: line.modelData
                wordsFamily: line.modelData
                wordsSize: 14
                closesMenu: false
                onClicked: root.choose(line.modelData, root.current.weight)
            }
        }
    }
    EctoMenuSeparator {}
    // the weight: the last line, across the menu
    Item {
        readonly property bool fillsWidth: true
        implicitWidth: weight.implicitWidth
        implicitHeight: weight.implicitHeight
        EctoSegmented {
            id: weight
            width: parent.width
            value: root.current.weight >= Font.Bold ? Font.Bold : root.current.weight >= Font.DemiBold ? Font.DemiBold : root.current.weight >= Font.Medium ? Font.Medium : Font.Normal
            model: [
                {value: Font.Normal, text: i18nc("@item font weight", "Regular")},
                {value: Font.Medium, text: i18nc("@item font weight", "Medium")},
                {value: Font.DemiBold, text: i18nc("@item font weight", "Semibold")},
                {value: Font.Bold, text: i18nc("@item font weight", "Bold")},
            ]
            onPicked: value => root.choose(root.current.family, value)
        }
    }
}
