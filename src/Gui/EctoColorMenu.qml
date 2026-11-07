/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/**
 * The color menu of the marking options: twelve swatches (EctoTheme.markColors, Ecto green
 * first), None where a mark can go without the color (a shape's stroke or fill), and Custom,
 * which opens a picker in the same menu: saturation and brightness, hue, opacity and a hex
 * field. It replaces KDE's color dialog, so the choosing stays in Ectolens's own look.
 *
 * openFor(button, title, color, allowNone, side); picked(color) says what was chosen.
 */
EctoMenu {
    id: root
    property string title: ""
    property color current: "transparent"
    property bool allowNone: false
    property bool customOpen: false
    // the picker's color, as hue, saturation, value and alpha
    property real hue: 0
    property real sat: 1
    property real val: 1
    property real alpha: 1
    readonly property color custom: Qt.hsva(hue, sat, val, alpha)
    signal picked(color color)
    // the swatch grid's width and the menus' content inset at each side (EctoTheme.menuContentInset):
    // the title, the swatches and the picker start where every menu's lines start
    minimumWidth: 6 * 24 + 5 * 6 + 2 * EctoTheme.menuContentInset

    function openFor(button, title, color, allowNone, side) {
        root.title = title
        root.current = color
        root.allowNone = allowNone
        root.customOpen = false
        root.loadCustom(color)
        root.toggleAt(button, side)
    }
    function loadCustom(c) {
        if (c.a === 0) {
            c = Qt.rgba(c.r, c.g, c.b, 1)
        }
        root.hue = c.hsvHue >= 0 ? c.hsvHue : root.hue
        root.sat = c.hsvSaturation
        root.val = c.hsvValue
        root.alpha = c.a
    }
    function hexOf(c) {
        return Qt.rgba(c.r, c.g, c.b, 1).toString().toUpperCase()
    }
    function same(a, b) {
        return Math.abs(a.r - b.r) < 0.004 && Math.abs(a.g - b.g) < 0.004 && Math.abs(a.b - b.b) < 0.004 && (a.a > 0) === (b.a > 0)
    }
    function choose(c, closeAfter) {
        root.current = c
        root.picked(c)
        if (closeAfter) {
            root.close()
        }
    }

    EctoMenuHeader { text: root.title }
    Column {
        x: EctoTheme.controlPadding
        // room for the ring around the chosen swatch; at the bottom, as far as at the sides
        topPadding: 4
        bottomPadding: EctoTheme.controlPadding
        Grid {
            id: swatches
            columns: 6
            spacing: 6
            Repeater {
                model: EctoTheme.markColors
                delegate: Rectangle {
                    id: chip
                    required property string modelData
                    objectName: "markColor" + modelData.slice(1)
                    width: 24
                    height: 24
                    radius: 7
                    color: modelData
                    border.width: 1
                    // an edge that shows the black swatch on the black menu (and white on the light window)
                    border.color: root.t.borderStrong
                    Rectangle {
                        visible: root.same(Qt.color(chip.modelData), root.current)
                        anchors.centerIn: parent
                        width: parent.width + 6
                        height: parent.height + 6
                        radius: 9
                        color: "transparent"
                        border.width: 1.5
                        border.color: root.t.fg
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.choose(Qt.color(chip.modelData), true)
                    }
                }
            }
        }
        Item { width: 1; height: 10 }
        Row {
            spacing: 6
            // None: a shape without this color
            Rectangle {
                visible: root.allowNone
                objectName: "markColorNone"
                width: 24
                height: 24
                radius: 7
                color: "transparent"
                border.width: 1
                border.color: root.t.borderStrong
                Rectangle {
                    anchors.centerIn: parent
                    width: 1.5
                    height: 26
                    rotation: 45
                    color: root.t.fgSubtle
                }
                Rectangle {
                    visible: root.current.a === 0
                    anchors.centerIn: parent
                    width: parent.width + 6
                    height: parent.height + 6
                    radius: 9
                    color: "transparent"
                    border.width: 1.5
                    border.color: root.t.fg
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.choose(Qt.rgba(root.current.r, root.current.g, root.current.b, 0), true)
                }
            }
            // Custom: the picker below
            Rectangle {
                objectName: "markColorCustom"
                width: 24
                height: 24
                radius: 7
                border.width: root.customOpen ? 1.5 : 0
                border.color: root.t.fg
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#f43f5e" }
                    GradientStop { position: 0.25; color: "#f59e0b" }
                    GradientStop { position: 0.45; color: "#c8f22e" }
                    GradientStop { position: 0.65; color: "#22d3ee" }
                    GradientStop { position: 0.85; color: "#3b82f6" }
                    GradientStop { position: 1.0; color: "#a855f7" }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.customOpen = !root.customOpen
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                // the gap after a line's first mark
                leftPadding: EctoTheme.leadGap - parent.spacing
                text: i18nc("@action:button a color of your own", "Custom")
                color: root.t.fgMuted
                font.family: EctoTheme.sans
                font.pixelSize: 12
            }
        }
        Column {
            visible: root.customOpen
            spacing: 10
            topPadding: 12
            // saturation (across) and brightness (down)
            Item {
                id: square
                objectName: "pickerSquare"
                // its knob stays inside the square, as an EctoSlider's thumb stays inside its track (the
                // sliders check finds it by this, and by its knob's name)
                readonly property bool thumbInside: true
                readonly property real padding: 0
                readonly property real value: root.sat
                readonly property real knob: 14
                width: swatches.width
                height: 112
                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    color: Qt.hsva(root.hue, 1, 1, 1)
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#ffffffff" }
                        GradientStop { position: 1.0; color: "#00ffffff" }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#00000000" }
                        GradientStop { position: 1.0; color: "#ff000000" }
                    }
                }
                Rectangle {
                    objectName: "sliderThumb"
                    x: root.sat * (square.width - width)
                    y: (1 - root.val) * (square.height - height)
                    width: square.knob
                    height: square.knob
                    radius: 7
                    color: Qt.hsva(root.hue, root.sat, root.val, 1)
                    border.width: 2
                    border.color: "white"
                }
                MouseArea {
                    anchors.fill: parent
                    function at(m) {
                        // the knob's middle follows the pointer
                        root.sat = Math.max(0, Math.min(1, (m.x - square.knob / 2) / (width - square.knob)))
                        root.val = 1 - Math.max(0, Math.min(1, (m.y - square.knob / 2) / (height - square.knob)))
                    }
                    onPressed: m => at(m)
                    onPositionChanged: m => at(m)
                    onReleased: root.choose(root.custom, false)
                }
            }
            // hue
            EctoSlider {
                objectName: "pickerHue"
                width: swatches.width
                height: 16
                // in line with the swatches above (the menu's own padding is the room past its ends); its
                // thumb stays inside the bar instead of hanging half past its ends
                padding: 0
                trackThickness: 10
                thumbSize: 16
                thumbBorderWidth: 2
                thumbBorderColor: "white"
                thumbColor: Qt.hsva(root.hue, 1, 1, 1)
                value: root.hue / 0.999
                onMoved: v => root.hue = Math.min(0.999, v)
                onSettled: root.choose(root.custom, false)
                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#ff0000" }
                        GradientStop { position: 0.167; color: "#ffff00" }
                        GradientStop { position: 0.333; color: "#00ff00" }
                        GradientStop { position: 0.5; color: "#00ffff" }
                        GradientStop { position: 0.667; color: "#0000ff" }
                        GradientStop { position: 0.833; color: "#ff00ff" }
                        GradientStop { position: 1.0; color: "#ff0000" }
                    }
                }
            }
            // opacity, over a checkerboard
            EctoSlider {
                objectName: "pickerOpacity"
                width: swatches.width
                height: 16
                padding: 0
                trackThickness: 10
                thumbSize: 16
                thumbBorderWidth: 2
                thumbBorderColor: "white"
                thumbColor: Qt.hsva(root.hue, root.sat, root.val, 1)
                value: root.alpha
                onMoved: v => root.alpha = v
                onSettled: root.choose(root.custom, false)
                Item {
                    id: checks
                    anchors.fill: parent
                    clip: true
                    Grid {
                        columns: Math.ceil(checks.width / 5)
                        Repeater {
                            model: Math.ceil(checks.width / 5) * 2
                            Rectangle {
                                required property int index
                                readonly property int cols: Math.ceil(checks.width / 5)
                                width: 5
                                height: 5
                                color: (index % cols + Math.floor(index / cols)) % 2 ? root.t.borderStrong : root.t.bgMuted
                            }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Qt.hsva(root.hue, root.sat, root.val, 0) }
                            GradientStop { position: 1.0; color: Qt.hsva(root.hue, root.sat, root.val, 1) }
                        }
                    }
                }
            }
            // the hex code and the opacity in words
            Row {
                spacing: 8
                Rectangle {
                    width: 110
                    height: 28
                    radius: 8
                    color: root.t.bgMuted
                    border.width: 1
                    border.color: hex.activeFocus ? EctoTheme.accent : root.t.border
                    TextInput {
                        id: hex
                        objectName: "markColorHex"
                        anchors.fill: parent
                        leftPadding: 9
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.t.fg
                        selectionColor: EctoTheme.accent
                        selectedTextColor: EctoTheme.onAccent
                        font.family: EctoTheme.mono
                        font.pixelSize: 12
                        selectByMouse: true
                        validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
                        text: root.hexOf(root.custom)
                        // while you type, the field keeps your letters
                        Connections {
                            target: root
                            function onCustomChanged() {
                                if (!hex.activeFocus) {
                                    hex.text = root.hexOf(root.custom)
                                }
                            }
                        }
                        onAccepted: {
                            const t = text.startsWith("#") ? text : "#" + text
                            if (t.length === 7) {
                                const c = Qt.color(t)
                                root.loadCustom(Qt.rgba(c.r, c.g, c.b, root.alpha))
                                root.choose(root.custom, false)
                                focus = false
                            }
                        }
                    }
                }
                Rectangle {
                    width: swatches.width - 118
                    height: 28
                    radius: 8
                    color: root.t.bgMuted
                    border.width: 1
                    border.color: root.t.border
                    Text {
                        anchors.fill: parent
                        leftPadding: 9
                        verticalAlignment: Text.AlignVCenter
                        text: i18nc("@info opacity in percent", "%1%", String(Math.round(root.alpha * 100)))
                        color: root.t.fg
                        font.family: EctoTheme.mono
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
