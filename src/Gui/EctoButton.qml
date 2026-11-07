/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/**
 * The family's button, small: round, 28 px tall, medium weight. In the rows it is never
 * filled (Ectonomic's design system's ghost button): hover lifts it to bg-muted, and the tool in use sits
 * on bg-lift with a short accent line under its icon. A dialog's buttons take `variant`
 * (primary, outline). Icons follow the text color.
 */
T.AbstractButton {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    property string iconName: ""
    property url iconSource
    /// Shown in the tooltip after the name, like "Enter" or "Ctrl+C".
    property string shortcutText: ""
    property string toolTipText: text
    property bool showText: text.length > 0 && !iconOnly
    property bool iconOnly: false
    /// the tool in use: bg-lift and the accent line
    property bool current: checked
    property bool quiet: false
    property bool mono: false
    property real iconOpacity: !enabled ? 0.28 : current || down ? 1 : 0.72
    property color textColor: quiet ? t.fgMuted : t.fg
    property bool menuOpen: false
    /// The design system's button variants (components/ui/button.tsx): "ghost" (the rows' buttons),
    /// "primary" (bg-fg text-bg hover:bg-fg-muted, active:brightness-95: a dialog's default
    /// action, the one Enter presses) and "outline" (border-border-strong over the surface's own
    /// color: its other choices; the design system's fill is bg-bg because its page is bg, and on a
    /// raised surface the same thing is no fill; hover and press tint it toward the words, as
    /// Material 3's state layers do, 8% and 10%: its StateTokens)
    property string variant: "ghost"
    readonly property bool primary: variant === "primary"
    readonly property bool outline: variant === "outline"

    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    implicitHeight: EctoTheme.buttonHeight
    // words get a control's room at both ends (EctoTheme.controlPadding), with an icon or without;
    // never narrower than tall (a one-letter button stays round), in whole pixels (words measure
    // in fractions, which would put every part after it between pixels)
    implicitWidth: !showText ? implicitHeight : Math.max(implicitHeight, Math.ceil(contentRow.implicitWidth) + EctoTheme.controlPadding * 2)
    readonly property bool hasIcon: iconName.length > 0 || iconSource.toString().length > 0
    opacity: enabled ? 1 : (hasIcon && !showText ? 1 : 0.45)

    background: Rectangle {
        radius: height / 2
        color: root.primary ? (root.down ? Qt.darker(root.t.fgMuted, 1.05) : root.hovered ? root.t.fgMuted : root.t.fg)
             : root.outline ? (root.down ? Qt.alpha(root.t.fg, 0.10) : root.hovered ? Qt.alpha(root.t.fg, 0.08) : "transparent")
             : root.current || root.down || root.menuOpen ? root.t.bgLift
             : root.hovered ? root.t.bgMuted : "transparent"
        border.width: root.outline ? 1 : 0
        border.color: root.t.borderStrong
        Rectangle {
            visible: root.current
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            width: parent.width - 16
            height: 2
            radius: 1
            color: EctoTheme.accent
        }
    }
    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: 6
            Kirigami.Icon {
                visible: root.hasIcon
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                source: root.iconName.length > 0 ? root.iconName : root.iconSource
                // an icon of our own (the gear) is a mask, filled with the text color
                isMask: root.iconName.length === 0
                color: root.primary ? root.t.bg : root.t.fg
                // a filled or outlined button dims whole when disabled (the button's own opacity)
                opacity: root.primary || root.outline ? 1 : root.iconOpacity
            }
            Text {
                visible: root.showText
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: root.primary ? root.t.bg : root.textColor
                font.family: root.mono ? EctoTheme.mono : EctoTheme.sans
                font.pixelSize: root.quiet || root.mono ? 12 : EctoTheme.fontSize
                font.weight: Font.Medium
            }
        }
    }
    EctoToolTip {
        text: root.shortcutText.length > 0 ? root.toolTipText + "  (" + root.shortcutText + ")" : root.toolTipText
        shown: root.hovered && (!root.showText || root.shortcutText.length > 0) && !root.menuOpen
    }
    Accessible.name: text
}
