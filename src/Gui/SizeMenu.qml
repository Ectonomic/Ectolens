/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private

/**
 * The frame label's menu (design proposal 2, step 3): type a size or pick one. On a
 * window, the window itself resizes so the capture is exactly that size, and it goes back
 * when you're done. On an area you drew, the frame takes the size. Sizes that don't fit
 * the screen are listed but can't be picked, with the reason. On a whole screen it lists
 * the screens instead.
 */
EctoMenu {
    id: root
    minimumWidth: 300
    /// the frame now, in pixels
    property size current: Qt.size(0, 0)
    property bool isWindow: SelectionEditor.frameIsWindow
    property bool isScreen: false
    property string appName: SelectionEditor.frameAppName
    readonly property size screenPx: CaptureTools.screenPixelSize(SelectionEditor.selection.rect)
    function pick(w, h) {
        close()
        CaptureTools.setFrameSize(w, h)
    }
    readonly property var presets: [
        {w: 1920, h: 1080, note: i18nc("@item size name", "Full HD, 16:9")},
        {w: 1280, h: 720, note: i18nc("@item size name", "HD, 16:9")},
        {w: 2560, h: 1440, note: i18nc("@item size name", "QHD, 16:9")},
        {w: 2560, h: 1080, note: i18nc("@item size name", "Ultrawide, 21:9")},
        {w: 1080, h: 1080, note: i18nc("@item size name", "Square")},
        {w: 1080, h: 1920, note: i18nc("@item size name", "Phone, 9:16")},
    ]
    function reason(w, h) {
        if (w > screenPx.width && h > screenPx.height) return i18nc("@info", "Larger than the screen")
        if (h > screenPx.height) return i18nc("@info", "Taller than the screen")
        if (w > screenPx.width) return i18nc("@info", "Wider than the screen")
        return ""
    }

    // ── a whole screen: switch screens ──
    EctoMenuHeader { visible: root.isScreen; text: i18nc("@title:menu", "Screen") }
    Repeater {
        model: root.isScreen && root.opened ? CaptureTools.screens() : []
        EctoMenuItem {
            required property var modelData
            required property int index
            visible: root.isScreen
            tickColumn: true
            tick: modelData.rect.x === SelectionEditor.selection.x && modelData.rect.y === SelectionEditor.selection.y
            iconName: "monitor"
            text: modelData.model.length > 0 ? modelData.model : modelData.name
            note: i18nc("@info width×height", "%1×%2", String(modelData.width), String(modelData.height))
            onClicked: CaptureTools.frameScreen(index)
        }
    }

    // ── a window or an area: the size ──
    EctoMenuHeader { visible: !root.isScreen; text: i18nc("@title:menu", "Size of the capture") }
    Item {
        visible: !root.isScreen
        readonly property bool fillsWidth: true
        implicitHeight: visible ? 44 : 0
        Row {
            x: EctoTheme.controlPadding
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            component Field: Rectangle {
                id: field
                property alias input: input
                width: 74
                height: 30
                radius: 6
                color: root.t.bg
                border.width: 1
                border.color: input.activeFocus ? EctoTheme.accent : root.t.border
                TextInput {
                    id: input
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: root.t.fg
                    selectionColor: Qt.alpha(EctoTheme.accent, 0.4)
                    selectedTextColor: root.t.fg
                    font.family: EctoTheme.mono
                    font.pixelSize: 13
                    validator: IntValidator { bottom: 1; top: 16384 }
                    selectByMouse: true
                    onAccepted: root.pick(parseInt(widthField.input.text), parseInt(heightField.input.text))
                }
            }
            Field {
                id: widthField
                input.text: Math.round(root.current.width)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "×"
                color: root.t.fgSubtle
                font.family: EctoTheme.sans
                font.pixelSize: 13
            }
            Field {
                id: heightField
                input.text: Math.round(root.current.height)
            }
            EctoButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "dialog-ok-apply"
                iconOnly: true
                text: i18nc("@action", "Use this size")
                shortcutText: i18nc("@info key name", "Enter")
                onClicked: root.pick(parseInt(widthField.input.text), parseInt(heightField.input.text))
            }
        }
    }
    EctoMenuSeparator { visible: !root.isScreen }
    Repeater {
        model: root.isScreen ? [] : root.presets
        EctoMenuItem {
            required property var modelData
            // for the test script's locate (the README's film picks a size with the real pointer)
            objectName: "sizePreset" + modelData.w + "x" + modelData.h
            tickColumn: true
            tick: Math.round(root.current.width) === modelData.w && Math.round(root.current.height) === modelData.h
            text: i18nc("@item width×height", "%1×%2", String(modelData.w), String(modelData.h))
            // the reason it can't be picked, at the end of the line
            reason: root.reason(modelData.w, modelData.h)
            note: reason.length > 0 ? "" : modelData.note
            enabled: reason.length === 0
            onClicked: root.pick(modelData.w, modelData.h)
        }
    }
    EctoMenuItem {
        visible: !root.isScreen && CaptureTools.originalSize.width > 0
        tickColumn: true
        text: i18nc("@item width×height", "%1×%2", String(CaptureTools.originalSize.width), String(CaptureTools.originalSize.height))
        note: i18nc("@item the window's size before", "As it was")
        onClicked: root.pick(CaptureTools.originalSize.width, CaptureTools.originalSize.height)
    }
    Text {
        readonly property bool fillsWidth: true
        visible: CaptureTools.sizeProblem.length > 0
        leftPadding: EctoTheme.controlPadding
        rightPadding: EctoTheme.controlPadding
        topPadding: 4
        bottomPadding: 6
        wrapMode: Text.WordWrap
        text: CaptureTools.sizeProblem
        color: root.t.error
        font.family: EctoTheme.sans
        font.pixelSize: 12
    }
    EctoMenuSeparator { visible: root.isWindow && !root.isScreen }
    EctoMenuRow {
        visible: root.isWindow && !root.isScreen
        text: i18nc("@option:check %1 app name", "Resize %1 to fit", root.appName.length > 0 ? root.appName : i18nc("@item", "the window"))
        checked: Settings.resizeWindowToFit
        onToggled: on => { Settings.resizeWindowToFit = on; Settings.save() }
    }
    EctoMenuRow {
        visible: root.isWindow && !root.isScreen
        text: i18nc("@option:check", "Revert size afterward")
        checked: Settings.revertSizeAfterward
        check.enabled: Settings.resizeWindowToFit
        onToggled: on => { Settings.revertSizeAfterward = on; Settings.save() }
    }
}
