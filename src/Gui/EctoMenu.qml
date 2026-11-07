/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/**
 * A menu or panel drawn inside the window, in the family's menu drawing: bg, a border,
 * 12 px corners, a soft shadow. Items stack top to bottom, each line's surface
 * EctoTheme.menuInset inside the menu, concentric with its corners (EctoMenuItem).
 *
 * openAt(item, side) places it against a button: above or below it (side "above",
 * "below"), lined up with the button's left edge, or its right edge when the button is
 * in the right half of the window, and kept inside the window.
 */
T.Popup {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    default property alias items: column.data
    property real minimumWidth: 220
    property Item anchorItem: null
    property string side: "above"
    property real gap: 8
    property real margin: 8
    property real radius: EctoTheme.menuRadius

    parent: T.Overlay.overlay
    padding: EctoTheme.menuInset
    modal: false
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    // as wide as its widest line wants (children are stretched to the menu's width, so the
    // column's own width can't say that), and never narrower than minimumWidth
    property real widestChild: 0
    implicitWidth: Math.max(minimumWidth, widestChild + leftPadding + rightPadding)
    implicitHeight: column.implicitHeight + topPadding + bottomPadding

    background: Kirigami.ShadowedRectangle {
        color: root.t.bg
        radius: root.radius
        border.width: 1
        border.color: root.t.border
        shadow.size: 28
        shadow.yOffset: 12
        shadow.color: root.t.shadow
    }
    contentItem: Column {
        id: column
        spacing: 0
        // children fill the menu's width
        function adopt() {
            let widest = 0
            for (const c of children) {
                if (c.fillsWidth) c.width = width
                if (c.hasOwnProperty("menu")) c.menu = root
                if (c.visible) widest = Math.max(widest, c.implicitWidth)
            }
            if (root.widestChild !== widest) {
                root.widestChild = widest
            }
        }
        onWidthChanged: adopt()
        onChildrenChanged: adopt()
        Component.onCompleted: adopt()
    }
    // measured again as it shows: while closed its lines read as hidden, and so as no width
    onAboutToShow: column.adopt()

    function place() {
        if (!anchorItem || !parent) {
            return
        }
        const p = anchorItem.mapToItem(parent, 0, 0)
        const pw = parent.width, ph = parent.height
        const rightHalf = p.x + anchorItem.width / 2 > pw / 2
        let nx = rightHalf ? p.x + anchorItem.width - width : p.x
        nx = Math.max(margin, Math.min(nx, pw - width - margin))
        let ny
        if (side === "beside") {
            // a submenu: beside its line, on the side with room
            nx = p.x + anchorItem.width + 4
            if (nx + width > pw - margin) nx = p.x - width - 4
            x = Math.round(Math.max(margin, nx))
            y = Math.round(Math.max(margin, Math.min(p.y - padding, ph - height - margin)))
            return
        }
        if (side === "below") {
            ny = p.y + anchorItem.height + gap
            if (ny + height > ph - margin) ny = p.y - gap - height
        } else {
            ny = p.y - gap - height
            if (ny < margin) ny = p.y + anchorItem.height + gap
        }
        x = Math.round(nx)
        y = Math.round(Math.max(margin, Math.min(ny, ph - height - margin)))
    }
    // A press on the button that opened it closes it first (a press outside); the same
    // press must not open it again.
    property double closedAt: 0
    property Item closedFrom: null
    onClosed: {
        closedAt = Date.now()
        closedFrom = anchorItem
    }
    function openAt(item, where) {
        if (closedFrom === item && Date.now() - closedAt < 250) {
            return
        }
        anchorItem = item
        side = where || "above"
        open()
        place()
    }
    function toggleAt(item, where) {
        if (opened) {
            close()
        } else {
            openAt(item, where)
        }
    }
    onWidthChanged: place()
    onHeightChanged: place()
}
