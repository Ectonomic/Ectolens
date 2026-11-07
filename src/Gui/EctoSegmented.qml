/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/**
 * The design system's segmented control (bits.tsx): one choice of a few, in a well with 8 px
 * corners; the chosen one sits raised on bg.
 * model: a list of {value, text, enabled}; value: the chosen value.
 *
 * Its choices stand as far from the well's sides as from its top and bottom (the mock's 2 px
 * padding inside a 1 px border), with corners concentric with the well's. Given more room than
 * its words want (the font menu's weight, across the menu), the choices share it evenly; it
 * never takes less than its words want.
 */
Rectangle {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    property var model: []
    property var value
    /// the row it belongs to is unchecked, so the choice dims (it still works)
    property bool dimmed: false
    signal picked(var value)
    /// from the well's edge to a choice, all round
    readonly property real inset: 3
    // what the words want: each choice's words with a control's room at both ends
    readonly property real wanted: {
        let sum = 0
        for (let i = 0; i < choices.count; ++i) {
            const c = choices.itemAt(i)
            if (c) sum += c.wanted
        }
        return sum
    }
    readonly property real extra: choices.count > 0 ? Math.max(0, (width - 2 * inset - wanted) / choices.count) : 0
    implicitWidth: wanted + 2 * inset
    implicitHeight: 32
    radius: 8
    color: t.bgMuted
    border.width: 1
    border.color: t.border
    opacity: dimmed ? 0.4 : 1
    Row {
        id: row
        x: root.inset
        y: root.inset
        spacing: 0
        Repeater {
            id: choices
            model: root.model
            delegate: Rectangle {
                id: seg
                required property var modelData
                readonly property bool on: modelData.value === root.value
                readonly property bool usable: modelData.enabled === undefined || modelData.enabled
                readonly property real wanted: Math.ceil(label.implicitWidth) + EctoTheme.controlPadding * 2
                height: root.height - 2 * root.inset
                width: wanted + root.extra
                radius: root.radius - root.inset
                color: on ? root.t.bg : mouse.containsMouse && usable ? Qt.alpha(root.t.bg, 0.5) : "transparent"
                opacity: usable ? 1 : 0.35
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: seg.modelData.text
                    color: seg.on ? root.t.fg : root.t.fgMuted
                    font.family: EctoTheme.sans
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: seg.usable
                    onClicked: root.picked(seg.modelData.value)
                }
            }
        }
    }
}
