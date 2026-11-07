/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private
import org.kde.kquickimageeditor.ectolens

/**
 * The marking tool's options, each shown only for the tools that have it, in a row: in the
 * overlay above the tool row, lined up with the tool's button ; in
 * the Ectolens window under the toolbar, lined up the same way. Everything is Ectolens's own
 * drawing, the color and font menus too.
 *
 * - Color: the mark's color. Text and Number take one color: a label or badge in it, with
 *   words that read on it; a text mark without its label is words in that color.
 *   Rectangle and Ellipse take two, a stroke (a ring) and a fill (a chip), each of which can be None.
 * - Width (a line as thick as the stroke), Label (text on a box, or plain words), Font
 *   (Aa, for Text and Number) and Size, Align (Text: where its words sit, across, and up and
 *   down a box), the next Number, Strength (blur and pixelate) and Shadow.
 *
 * With no options to show it is empty.
 *
 * A Row, EctoTheme.rowSpacing apart: a Grid with more columns than items adds one more gap after
 * the last (Qt 6.11), which put the last option 2 px short of the tool it lines up with.
 */
Row {
    id: root
    readonly property var t: EctoTheme.of(contextWindow)
    readonly property AnnotationDocument document: SpectacleCore.annotationDocument
    readonly property AnnotationTool tool: document.tool
    readonly property SelectedItemWrapper selectedItem: document.selectedItem
    readonly property bool useSelectionOptions: tool.type === AnnotationTool.SelectTool
        || (tool.type === AnnotationTool.TextTool && selectedItem.options & AnnotationTool.TextOption)
    readonly property int options: useSelectionOptions ? selectedItem.options : tool.options
    readonly property bool hasOptions: options !== AnnotationTool.NoOptions
    // which kind of mark: words and numbers take one color (a label or badge in it)
    readonly property bool isText: options & AnnotationTool.TextOption
    readonly property bool isNumber: options & AnnotationTool.NumberOption
    readonly property bool oneColor: isText || isNumber
    readonly property bool hasStroke: options & AnnotationTool.StrokeOption
    readonly property bool hasFill: (options & AnnotationTool.FillOption) && !oneColor
    readonly property bool hasFont: options & AnnotationTool.FontOption
    readonly property bool hasStrength: options & AnnotationTool.StrengthOption
    readonly property bool hasShadow: options & AnnotationTool.ShadowOption
    // where its menus open: below the row, unless the row says otherwise (the overlay's row above
    // the tools opens them above it, away from the tools)
    property string menuSide: "below"

    spacing: EctoTheme.rowSpacing

    // the text being written, or a mark picked with Select: its own settings change, and
    // a text's also become the Text tool's, so the next text is the same. A choice counts at
    // once (as Spectacle's dialogs did), so leaving the mark right after never loses it; the
    // strength slider counts when it is let go (`settling`).
    function set(name, value, settling) {
        if (useSelectionOptions) {
            if (isText && selectedItem[name] !== value) {
                // words typed and not yet saved are their own step, before this choice's
                selectedItem.commitTyping()
            }
            if (selectedItem[name] !== value) {
                selectedItem[name] = value
                if (!settling) {
                    selectedItem.commitChanges()
                }
            }
            if (tool.type === AnnotationTool.TextTool) {
                tool[name] = value
            }
        } else {
            tool[name] = value
        }
    }
    function get(name) {
        return useSelectionOptions ? selectedItem[name] : tool[name]
    }
    function withAlpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }
    // Text and Number: one color, the label's (or badge's) when it shows, the words' otherwise
    // (settings brought from Spectacle can hold different ones; the swatch shows what is drawn)
    readonly property bool labelOn: oneColor && (get("fillColor") ?? Qt.rgba(0, 0, 0, 0)).a > 0
    readonly property color markColor: !oneColor ? "transparent"
        : labelOn ? get("fillColor") : (get("fontColor") ?? EctoTheme.ectoGreen)
    function setMarkColor(c) {
        const opaque = withAlpha(c, c.a > 0 ? c.a : 1)
        set("fontColor", opaque)
        set("fillColor", labelOn || isNumber ? opaque : withAlpha(opaque, 0))
    }
    function setLabel(on) {
        // the words and the box keep the one color either way
        const c = withAlpha(markColor, 1)
        set("fontColor", c)
        set("fillColor", withAlpha(c, on ? 1 : 0))
    }
    function setFont(family, pointSize, weight) {
        const f = get("font") ?? Qt.application.font
        set("font", Qt.font({family: family ?? f.family, pointSize: pointSize ?? f.pointSize, weight: weight ?? f.weight, italic: f.italic}))
    }

    component Sep: EctoSeparator {}
    // Align's drawing: three lines of words set left, centered or right (across), or, for up and
    // down, a line of words at the top, middle or bottom of a box's outline
    component AlignGlyph: Item {
        id: glyph
        property int across: Qt.AlignLeft
        property int upDown: 0
        property color color: root.t.fg
        width: 16
        height: upDown !== 0 ? 16 : 14
        Rectangle {
            visible: glyph.upDown !== 0
            anchors.fill: parent
            radius: 3
            color: "transparent"
            border.width: 1
            border.color: glyph.color
            opacity: 0.55
        }
        Repeater {
            model: glyph.upDown !== 0 ? [10] : [14, 8, 12]
            Rectangle {
                required property int index
                required property int modelData
                width: modelData
                height: 2
                radius: 1
                color: glyph.color
                x: glyph.upDown !== 0 ? 3
                    : glyph.across === Qt.AlignHCenter ? (glyph.width - width) / 2
                    : glyph.across === Qt.AlignRight ? glyph.width - width : 0
                y: glyph.upDown === Qt.AlignTop ? 3
                    : glyph.upDown === Qt.AlignVCenter ? 7
                    : glyph.upDown === Qt.AlignBottom ? 11
                    : 1 + index * 5
            }
        }
    }

    // ── Color ──
    EctoSwatch {
        id: markSwatch
        objectName: "optionColor"
        visible: root.oneColor
        text: i18nc("@info:tooltip the color of a text or number mark", "Color")
        color: root.markColor
        menuOpen: colorMenu.opened && colorMenu.anchorItem === this
        onClicked: {
            colorMenu.kind = "mark"
            colorMenu.openFor(markSwatch, i18nc("@title the color of a mark", "Color"), root.markColor, false, root.menuSide)
        }
    }
    EctoSwatch {
        id: strokeSwatch
        objectName: "optionStroke"
        visible: root.hasStroke
        text: root.hasFill ? i18nc("@info:tooltip", "Stroke") : i18nc("@info:tooltip the color of a line", "Color")
        ring: root.hasFill
        color: root.get("strokeColor") ?? "transparent"
        menuOpen: colorMenu.opened && colorMenu.anchorItem === this
        onClicked: {
            colorMenu.kind = "stroke"
            colorMenu.openFor(strokeSwatch, root.hasFill ? i18nc("@title", "Stroke") : i18nc("@title the color of a line", "Color"),
                              root.get("strokeColor"), root.hasFill, root.menuSide)
        }
    }
    // the stroke's width, drawn as a line that thick
    EctoButton {
        id: widthButton
        objectName: "optionWidth"
        visible: root.hasStroke
        readonly property int strokeWidth: root.get("strokeWidth") ?? 1
        text: i18nc("@info:tooltip stroke width in pixels", "Width: %1 px", String(strokeWidth))
        iconOnly: true
        menuOpen: widthMenu.opened
        onClicked: widthMenu.toggleAt(widthButton, root.menuSide)
        Rectangle {
            anchors.centerIn: parent
            width: 16
            height: Math.max(1, Math.min(8, widthButton.strokeWidth))
            radius: height / 2
            color: root.t.fg
        }
        WheelHandler {
            onWheel: event => root.set("strokeWidth", Math.max(root.hasFill ? 0 : 1, Math.min(99, widthButton.strokeWidth + (event.angleDelta.y > 0 ? 1 : -1))))
        }
    }
    EctoSwatch {
        id: fillSwatch
        objectName: "optionFill"
        visible: root.hasFill
        text: i18nc("@info:tooltip", "Fill")
        color: root.get("fillColor") ?? "transparent"
        menuOpen: colorMenu.opened && colorMenu.anchorItem === this
        onClicked: {
            colorMenu.kind = "fill"
            colorMenu.openFor(fillSwatch, i18nc("@title", "Fill"), root.get("fillColor"), true, root.menuSide)
        }
    }
    // Label: the words on a box of the color, or plain words
    EctoButton {
        id: labelButton
        objectName: "optionLabel"
        visible: root.isText
        text: i18nc("@info:tooltip", "Label: the words on a box of their color")
        iconOnly: true
        current: root.labelOn
        onClicked: root.setLabel(!root.labelOn)
        Rectangle {
            anchors.centerIn: parent
            width: labelGlyph.implicitWidth + 8
            height: 16
            radius: 4
            color: root.t.fg
            opacity: root.labelOn ? 1 : 0.45
            Text {
                id: labelGlyph
                anchors.centerIn: parent
                text: "A"
                color: root.t.bg
                font.family: EctoTheme.sans
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }
    }

    // ── the next Number ──
    Sep { visible: root.isNumber }
    EctoButton {
        objectName: "optionNumberUp"
        visible: root.isNumber
        text: "−"
        mono: true
        toolTipText: i18nc("@action", "Lower number")
        enabled: (root.get("number") ?? 1) > 0
        onClicked: root.set("number", (root.get("number") ?? 1) - 1)
    }
    Text {
        objectName: "optionNumber"
        visible: root.isNumber
        width: EctoTheme.buttonHeight
        height: EctoTheme.buttonHeight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: root.get("number") ?? 1
        color: root.t.fg
        font.family: EctoTheme.mono
        font.pixelSize: 12
        font.weight: Font.DemiBold
        EctoToolTip {
            text: i18nc("@info:tooltip", "The next number")
            shown: numberHover.hovered
        }
        HoverHandler { id: numberHover }
        WheelHandler {
            onWheel: event => root.set("number", Math.max(0, (root.get("number") ?? 1) + (event.angleDelta.y > 0 ? 1 : -1)))
        }
    }
    EctoButton {
        objectName: "optionNumberDown"
        visible: root.isNumber
        text: "+"
        mono: true
        toolTipText: i18nc("@action", "Higher number")
        onClicked: root.set("number", (root.get("number") ?? 1) + 1)
    }

    // ── Font ──
    Sep { visible: root.hasFont }
    EctoButton {
        id: fontButton
        objectName: "optionFont"
        visible: root.hasFont
        readonly property font currentFont: root.get("font") ?? Qt.application.font
        text: i18nc("@info:tooltip %1 a font's name", "Font: %1", currentFont.family)
        iconOnly: true
        menuOpen: fontMenu.opened
        onClicked: fontMenu.openFor(fontButton, currentFont, root.menuSide)
        Text {
            anchors.centerIn: parent
            text: "Aa"
            color: root.t.fg
            font.family: EctoTheme.sans
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }
    EctoButton {
        id: sizeButton
        objectName: "optionSize"
        visible: root.hasFont
        readonly property int pointSize: (root.get("font") ?? Qt.application.font).pointSize
        text: String(pointSize)
        mono: true
        toolTipText: i18nc("@info:tooltip %1 font size in points", "Size: %1 pt", String(pointSize))
        menuOpen: sizeMenu.opened
        implicitWidth: EctoTheme.buttonHeight
        onClicked: sizeMenu.toggleAt(sizeButton, root.menuSide)
        WheelHandler {
            onWheel: event => root.setFont(undefined, Math.max(6, Math.min(144, sizeButton.pointSize + (event.angleDelta.y > 0 ? 1 : -1))), undefined)
        }
    }
    // Align: where a text's words sit across it, and up and down a box drawn taller than its words
    EctoButton {
        id: alignButton
        objectName: "optionAlign"
        visible: root.isText
        readonly property int alignment: root.get("textAlignment") ?? (Qt.AlignLeft | Qt.AlignTop)
        readonly property int across: alignment & (Qt.AlignLeft | Qt.AlignHCenter | Qt.AlignRight) || Qt.AlignLeft
        readonly property int upDown: alignment & (Qt.AlignTop | Qt.AlignVCenter | Qt.AlignBottom) || Qt.AlignTop
        // up and down matters in a box: offered for one, and for the next text (it may be drawn as one),
        // not for a label sized to its words
        readonly property bool offersUpDown: !(root.useSelectionOptions && root.isText && !root.selectedItem.textBox)
        readonly property string acrossName: across === Qt.AlignHCenter ? i18nc("@item words centered across", "Center")
            : across === Qt.AlignRight ? i18nc("@item words set right", "Right") : i18nc("@item words set left", "Left")
        readonly property string upDownName: upDown === Qt.AlignVCenter ? i18nc("@item words in the middle of a box", "Middle")
            : upDown === Qt.AlignBottom ? i18nc("@item words at the bottom of a box", "Bottom") : i18nc("@item words at the top of a box", "Top")
        text: offersUpDown && upDown !== Qt.AlignTop
            ? i18nc("@info:tooltip %1 where the words sit across (Left, Center, Right), %2 up and down a box (Middle, Bottom)", "Align: %1, %2", acrossName, upDownName)
            : i18nc("@info:tooltip %1 where the words sit across (Left, Center, Right)", "Align: %1", acrossName)
        iconOnly: true
        menuOpen: alignMenu.opened
        onClicked: alignMenu.toggleAt(alignButton, root.menuSide)
        AlignGlyph {
            anchors.centerIn: parent
            across: alignButton.across
        }
    }

    // ── Strength (blur and pixelate) ──
    EctoSlider {
        id: strength
        objectName: "optionStrength"
        visible: root.hasStrength
        value: root.get("strength") ?? 0.5
        // its padding past each end keeps the thumb clear of the row's ends (EctoTheme.sliderSidePadding)
        width: 96 + 2 * padding
        height: EctoTheme.buttonHeight
        EctoToolTip {
            text: i18nc("@info:tooltip %1 percent", "Strength: %1%", String(Math.round(strength.value * 100)))
            shown: strength.hovered || strength.pressed
            // it reads out the strength while the slider is held
            hideOnPress: false
            delay: strength.pressed ? 0 : Kirigami.Units.toolTipDelay
        }
        onMoved: v => root.set("strength", v, true)
        onSettled: if (root.useSelectionOptions) root.selectedItem.commitChanges()
    }

    // ── Shadow ──
    Sep { visible: root.hasShadow && (root.oneColor || root.hasStroke || root.hasFont) }
    EctoButton {
        id: shadowButton
        objectName: "optionShadow"
        visible: root.hasShadow
        readonly property bool on: root.tool.type === AnnotationTool.TextTool && root.selectedItem.options & AnnotationTool.TextOption
            ? root.tool.shadow : root.get("shadow") ?? false
        text: i18nc("@info:tooltip", "Shadow")
        iconOnly: true
        current: on
        onClicked: {
            if (root.tool.type === AnnotationTool.TextTool && root.selectedItem.options & AnnotationTool.TextOption) {
                // words typed and not yet saved are their own step, before the shadow's
                root.selectedItem.commitTyping()
                root.selectedItem.shadow = !on
                root.tool.shadow = !on
                root.selectedItem.commitChanges()
            } else {
                root.set("shadow", !on)
            }
        }
        // a square and its shadow
        Item {
            anchors.centerIn: parent
            width: 14
            height: 14
            opacity: shadowButton.on ? 1 : 0.45
            Rectangle {
                x: 3
                y: 3
                width: 11
                height: 11
                radius: 3
                color: root.t.fg
                opacity: 0.32
            }
            Rectangle {
                width: 11
                height: 11
                radius: 3
                color: root.t.bgSubtle
                border.width: 1.5
                border.color: root.t.fg
            }
        }
    }

    // ── the menus ──
    EctoColorMenu {
        id: colorMenu
        property string kind: "mark"
        onPicked: c => {
            if (kind === "mark") {
                root.setMarkColor(c)
            } else if (kind === "stroke") {
                root.set("strokeColor", c)
            } else {
                root.set("fillColor", c)
            }
        }
    }
    EctoFontMenu {
        id: fontMenu
        onPicked: f => root.setFont(f.family, undefined, f.weight)
    }
    EctoMenu {
        id: widthMenu
        minimumWidth: 132
        Repeater {
            model: [1, 2, 3, 4, 6, 8, 12, 16, 24]
            EctoMenuItem {
                id: widthItem
                required property int modelData
                tickColumn: true
                tick: widthButton.strokeWidth === modelData
                text: i18nc("@item stroke width in pixels", "%1 px", String(modelData))
                onClicked: root.set("strokeWidth", modelData)
                Rectangle {
                    width: 28
                    height: Math.min(widthItem.modelData, 12)
                    radius: height / 2
                    color: widthItem.t.fg
                }
            }
        }
    }
    EctoMenu {
        id: sizeMenu
        minimumWidth: 112
        Repeater {
            model: [8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 64]
            EctoMenuItem {
                required property int modelData
                tickColumn: true
                tick: sizeButton.pointSize === modelData
                text: i18nc("@item font size in points", "%1 pt", String(modelData))
                onClicked: root.setFont(undefined, modelData, undefined)
            }
        }
    }
    EctoMenu {
        id: alignMenu
        minimumWidth: 168
        Repeater {
            model: [{value: Qt.AlignLeft, text: i18nc("@item words set left", "Left")},
                    {value: Qt.AlignHCenter, text: i18nc("@item words centered across", "Center")},
                    {value: Qt.AlignRight, text: i18nc("@item words set right", "Right")}]
            EctoMenuItem {
                id: acrossItem
                required property var modelData
                objectName: "align-" + modelData.value
                tickColumn: true
                tick: alignButton.across === modelData.value
                text: modelData.text
                onClicked: root.set("textAlignment", modelData.value | alignButton.upDown)
                AlignGlyph {
                    across: acrossItem.modelData.value
                    color: acrossItem.t.fg
                }
            }
        }
        EctoMenuSeparator { visible: alignButton.offersUpDown }
        EctoMenuHeader {
            visible: alignButton.offersUpDown
            text: i18nc("@title:menu where the words sit up and down a text box drawn taller than they are", "In a box")
        }
        Repeater {
            model: [{value: Qt.AlignTop, text: i18nc("@item words at the top of a box", "Top")},
                    {value: Qt.AlignVCenter, text: i18nc("@item words in the middle of a box", "Middle")},
                    {value: Qt.AlignBottom, text: i18nc("@item words at the bottom of a box", "Bottom")}]
            EctoMenuItem {
                id: upDownItem
                required property var modelData
                objectName: "align-" + modelData.value
                visible: alignButton.offersUpDown
                tickColumn: true
                tick: alignButton.upDown === modelData.value
                text: modelData.text
                onClicked: root.set("textAlignment", alignButton.across | modelData.value)
                AlignGlyph {
                    upDown: upDownItem.modelData.value
                    color: upDownItem.t.fg
                }
            }
        }
    }
}
