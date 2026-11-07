/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/**
 * Ectolens's tooltip: a few words about a control, in the look's font and the family's menu
 * drawing (bg, a border, a soft shadow). KDE's own tooltip drew them in KDE's font and colors
 * beside menus drawn in Ectolens's (the bars and menus are standardized).
 *
 * It behaves as KDE's desktop style's tooltip does (qqc2-desktop-style ToolTip.qml): centered
 * above its control, shown after KDE's tooltip delay, kept while the pointer stays, long words
 * wrapped at KDE's width, kept inside the window and below the control when there is no room
 * above. It stands as far from its control as a menu does (EctoTheme.rowGap).
 *
 * Put one inside the control: EctoToolTip { text: …; shown: control.hovered }.
 */
T.ToolTip {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    /// whether it should show (after its delay)
    property bool shown: false
    /// Pressing its control hides it until the pointer leaves and comes back (Hide Private Info's tooltip covered the "Blurred 6 private items" note it had just
    /// caused, which stands where a tooltip does). A control whose tooltip reads out a value while it is
    /// held (a slider's strength) turns this off.
    property bool hideOnPress: true
    property bool pressedAway: false
    Connections {
        target: root.parent
        ignoreUnknownSignals: true
        function onPressedChanged() {
            if (root.hideOnPress && root.parent.pressed) {
                root.pressedAway = true
            }
        }
        function onHoveredChanged() {
            if (!root.parent.hovered) {
                root.pressedAway = false
            }
        }
    }

    visible: shown && text.length > 0 && !pressedAway
    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: -implicitHeight - EctoTheme.rowGap
    // whole pixels: words measure in fractions, which would blur its edges or wrap them early
    implicitWidth: Math.ceil(contentItem.implicitWidth) + leftPadding + rightPadding
    implicitHeight: Math.ceil(contentItem.implicitHeight) + topPadding + bottomPadding
    margins: EctoTheme.barInset
    topPadding: 6
    bottomPadding: 6
    leftPadding: EctoTheme.controlPadding
    rightPadding: EctoTheme.controlPadding
    delay: Kirigami.Units.toolTipDelay
    // never gone while the pointer is still on its control
    timeout: -1
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Kirigami.Units.longDuration; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Kirigami.Units.longDuration; easing.type: Easing.OutCubic }
    }

    contentItem: Item {
        implicitWidth: Math.min(words.widest, words.contentWidth)
        implicitHeight: words.implicitHeight
        Text {
            id: words
            // KDE's width for a tooltip's words, wrapping anywhere so a long path can't run past it
            readonly property real widest: Kirigami.Units.gridUnit * 14
            text: root.text
            wrapMode: Text.Wrap
            color: root.t.fg
            font.family: EctoTheme.sans
            font.pixelSize: 12
            onLineLaidOut: line => {
                if (line.implicitWidth > widest) {
                    line.width = widest
                }
            }
        }
    }
    background: Kirigami.ShadowedRectangle {
        color: root.t.bg
        radius: 8
        border.width: 1
        border.color: root.t.border
        shadow.size: 16
        shadow.yOffset: 4
        shadow.color: root.t.shadow
    }
}
