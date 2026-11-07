/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Templates as T
import org.kde.spectacle.private

/**
 * The capture delay in a menu: "Delay:" and a stepper, No Delay up to a minute.
 *
 * The stepper is one control on a pick's surface (EctoPick: bg-muted, a border, 8 px corners,
 * 28 px tall), − and + inside it, so it ends where a menu item's key ends, as After Capture's
 * choices do (the menus' right column). Its − and + light concentric with it.
 */
Item {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    readonly property bool fillsWidth: true
    implicitHeight: 36
    implicitWidth: EctoTheme.controlPadding * 2 + Math.ceil(label.implicitWidth) + 24 + stepper.width
    readonly property var steps: [0, 1, 2, 3, 5, 10, 15, 30, 60]
    function step(by) {
        const now = Settings.captureDelay
        let i = steps.findIndex(s => s >= now - 0.01)
        if (i < 0) i = steps.length - 1
        i = Math.max(0, Math.min(steps.length - 1, i + by))
        Settings.captureDelay = steps[i]
        Settings.save()
    }
    function words(seconds) {
        return seconds <= 0 ? i18nc("@item no capture delay", "No Delay")
            : i18ncp("@item capture delay in seconds", "%1 second", "%1 seconds", Math.round(seconds))
    }
    Text {
        id: label
        x: EctoTheme.controlPadding
        anchors.verticalCenter: parent.verticalCenter
        text: i18nc("@label", "Delay:")
        color: root.t.fgMuted
        font.family: EctoTheme.sans
        font.pixelSize: 13
    }

    component Step: T.AbstractButton {
        id: step
        property string tip
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        // inside the stepper by the same room above, below and at its end: concentric with its corners
        width: EctoTheme.buttonHeight - 2 * stepper.inset
        height: width
        anchors.verticalCenter: parent.verticalCenter
        background: Rectangle {
            radius: stepper.radius - stepper.inset
            color: !step.enabled ? "transparent" : step.down ? root.t.border : step.hovered ? root.t.bgLift : "transparent"
        }
        contentItem: Text {
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: step.text
            color: root.t.fg
            opacity: step.enabled ? 1 : 0.35
            font.family: EctoTheme.mono
            font.pixelSize: 13
        }
        EctoToolTip {
            text: step.tip
            shown: step.hovered
        }
    }
    // the widest the words can be, so the stepper keeps one size as the delay changes
    TextMetrics {
        id: widest
        font: value.font
        text: root.steps.map(s => root.words(s)).reduce((a, b) => b.length > a.length ? b : a, "")
    }
    Rectangle {
        id: stepper
        objectName: "delayStepper"
        // from its edge to the − and +
        readonly property real inset: 2
        anchors.right: parent.right
        anchors.rightMargin: EctoTheme.controlPadding
        anchors.verticalCenter: parent.verticalCenter
        height: EctoTheme.buttonHeight
        width: row.width + 2 * inset
        radius: 8
        color: root.t.bgMuted
        border.width: 1
        border.color: root.t.border
        Row {
            id: row
            x: stepper.inset
            height: parent.height
            Step {
                objectName: "delayShorter"
                text: "−"
                tip: i18nc("@action", "Shorter delay")
                enabled: Settings.captureDelay > 0
                onClicked: root.step(-1)
            }
            Text {
                id: value
                width: Math.ceil(widest.advanceWidth) + 2 * 6
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignHCenter
                text: root.words(Settings.captureDelay)
                color: root.t.fg
                font.family: EctoTheme.sans
                font.pixelSize: 12
            }
            Step {
                objectName: "delayLonger"
                text: "+"
                tip: i18nc("@action", "Longer delay")
                enabled: Settings.captureDelay < 60
                onClicked: root.step(1)
            }
        }
    }
}
