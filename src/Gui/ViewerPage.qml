/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Window
import QtQuick.Controls as QQC
import QtMultimedia
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private
import org.kde.kquickimageeditor.ectolens

/**
 * The Ectolens window, simplified the way the overlay was (design proposal 2, step 4).
 *
 * The overlay's two rows, docked to the window's edges with their groups anchored to the
 * sides, and the picture between. Above: the picture's size at the left (information, in
 * gray), the marking tools in the middle (always out: no Edit mode) and the gear at the
 * right. Below: Copy, Save and Extract Text at the left; zoom and New Capture at the right.
 * Messages are slim strips under the top row. After a recording the bottom row plays it.
 * With nothing captured, New Capture and the gear are all there is.
 *
 * The window uses the design system's dark set, or the light set in a light Plasma color scheme.
 */
Rectangle {
    id: root
    readonly property var t: EctoTheme.window
    /// opened without a capture (from the launcher)
    property bool empty: false
    readonly property bool video: SpectacleCore.videoMode && !empty
    readonly property bool picture: !empty && !SpectacleCore.videoMode
    readonly property var view: contentLoader.item
    readonly property AnnotationDocument document: SpectacleCore.annotationDocument
    readonly property int toolType: document.tool.type

    // Used in ViewerWindow::updateMinimumSize(): the tools stay in one row.
    readonly property real minimumWidth: Math.max(480, toolsMeasure.implicitWidth + 2 * 48 + 24)
    readonly property real minimumHeight: 360

    // When the rows don't fit, the button words give way to their icons and the size label
    // goes first (gap 6).
    readonly property bool narrow: width < Math.max(toolsMeasure.implicitWidth + 2 * (sizeLabel.implicitWidth + 24),
                                                    wideBottom.implicitWidth + 32)

    color: t.bgSubtle
    focus: true

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    Component.onCompleted: {
        // the marking tools are always out in the window
        if (root.picture) {
            contextWindow.annotating = true
        }
    }

    // Needed for scrolling via keyboard input
    Keys.priority: Keys.AfterItem
    Keys.enabled: root.picture && contentLoader.item !== null
    Keys.forwardTo: contentLoader.item

    // ── The picture, the recording, or nothing yet ──
    Loader {
        id: contentLoader
        anchors {
            left: parent.left
            right: parent.right
            top: messages.bottom
            bottom: bottomDock.top
        }
        sourceComponent: root.empty ? null : root.video ? recordingViewComponent : screenshotViewComponent
        Component {
            id: recordingViewComponent
            RecordingView {
                ownControls: false
            }
        }
        Component {
            id: screenshotViewComponent
            ScreenshotView {}
        }
    }

    // a big play button over a paused recording
    Rectangle {
        visible: root.video && root.view && root.view.hasContent && !root.view.playing && !root.view.hasAnimatedImage
        anchors.centerIn: contentLoader
        width: 64
        height: 64
        radius: 32
        color: Qt.rgba(0.04, 0.04, 0.04, 0.75)
        Kirigami.Icon {
            anchors.centerIn: parent
            width: 26
            height: 26
            source: "media-playback-start"
            color: "#fafafa"
        }
        MouseArea {
            anchors.fill: parent
            onClicked: root.view.togglePlay()
        }
    }

    // Nothing captured yet: one thing to do
    Row {
        visible: root.empty
        anchors.centerIn: contentLoader
        spacing: 10
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: i18nc("@info the Ectolens window with no capture", "Nothing captured yet")
            color: root.t.fgMuted
            font.family: EctoTheme.sans
            font.pixelSize: 14
        }
        EctoKey {
            visible: text.length > 0
            text: CaptureTools.globalShortcut("open")
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: i18nc("@info after the Print key", "or New Capture")
            color: root.t.fgSubtle
            font.family: EctoTheme.sans
            font.pixelSize: 14
        }
    }

    // ── Top row: size, tools, gear ──
    Rectangle {
        id: topDock
        // a full-width bar, held to the bars' rules by the `anatomy` check
        readonly property bool ectoBar: true
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 44
        color: root.t.bg
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: root.t.border
        }
        FrameLabel {
            id: sizeLabel
            objectName: "windowSize"
            anchors.left: parent.left
            anchors.leftMargin: EctoTheme.barInset
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.empty && !root.narrow
            infoOnly: true
            sizeText: {
                if (root.video) {
                    const res = root.view?.player?.metaData?.value(MediaMetaData.Resolution)
                    const length = SpectacleCore.timeFromMilliseconds(root.view?.player?.duration ?? 0)
                    return res && res.width > 0 ? i18nc("@info size and length of a recording", "%1×%2 · %3", String(res.width), String(res.height), length) : length
                }
                const r = root.document.canvasRect
                const dpr = root.document.imageDpr
                return i18nc("@info width×height in pixels", "%1×%2", String(Math.round(r.width * dpr)), String(Math.round(r.height * dpr)))
            }
        }
        EctoTools {
            id: tools
            anchors.centerIn: parent
            visible: root.picture
            inOverlay: false
        }
        EctoButton {
            id: gear
            objectName: "windowGear"
            anchors.right: parent.right
            anchors.rightMargin: EctoTheme.barInset
            anchors.verticalCenter: parent.verticalCenter
            iconSource: "qrc:/icons/ectolens-gear.svg"
            iconOnly: true
            text: i18nc("@action:button", "Settings and Help")
            menuOpen: gearMenu.opened
            onClicked: gearMenu.toggleAt(gear, "below")
        }
    }

    // ── Messages: slim strips under the top row, each with its one action ──
    Column {
        id: messages
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: topDock.bottom
        z: 3
        Repeater {
            model: InlineMessageModel
            delegate: Rectangle {
                id: strip
                readonly property bool ectoBar: true
                required property int index
                required property var model
                readonly property int kind: model.type
                readonly property var payload: model.data
                readonly property string dataText: payload !== null && payload !== undefined && payload.toString !== undefined ? payload.toString() : ""
                readonly property bool isFile: dataText.startsWith("file:")
                readonly property bool isLink: /^(https?|ftp):\/\//i.test(dataText)
                width: messages.width
                height: 38
                color: kind === InlineMessageModel.Error ? Qt.tint(root.t.bg, Qt.alpha(root.t.error, 0.08)) : root.t.bg
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: root.t.border
                }
                Rectangle {
                    id: dot
                    // in the column where the bars' first words and icons start
                    x: EctoTheme.barInset + EctoTheme.controlPadding
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: strip.kind === InlineMessageModel.Error ? root.t.error
                        : strip.kind === InlineMessageModel.Warning ? root.t.warning
                        : strip.kind === InlineMessageModel.Scanned ? root.t.info
                        : root.t.success
                }
                Text {
                    anchors.left: dot.right
                    anchors.leftMargin: 10
                    anchors.right: actions.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: strip.model.text
                    textFormat: Text.StyledText
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    color: root.t.fgMuted
                    linkColor: root.t.fg
                    font.family: EctoTheme.sans
                    font.pixelSize: 13
                    onLinkActivated: link => Qt.openUrlExternally(link)
                }
                Row {
                    id: actions
                    anchors.right: parent.right
                    anchors.rightMargin: EctoTheme.barInset
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: EctoTheme.rowSpacing
                    EctoButton {
                        visible: strip.isFile
                        text: i18nc("@action:button", "Show in Folder")
                        onClicked: InlineMessageModel.openContainingFolder(strip.payload)
                    }
                    EctoButton {
                        visible: strip.kind === InlineMessageModel.Scanned && strip.isLink
                        text: i18nc("@action:button open what a QR code says", "Open")
                        onClicked: Qt.openUrlExternally(strip.dataText)
                    }
                    EctoButton {
                        visible: strip.kind === InlineMessageModel.Scanned && !strip.isLink && strip.dataText.length > 0
                        text: i18nc("@action:button", "Copy")
                        onClicked: InlineMessageModel.copyToClipboard(strip.payload)
                    }
                    EctoButton {
                        visible: strip.kind === InlineMessageModel.Extracted && strip.dataText.length > 0
                        text: i18nc("@action:button", "Show Text")
                        onClicked: textMenu.show(strip.dataText, strip)
                    }
                    EctoButton {
                        iconName: "window-close"
                        iconOnly: true
                        text: i18nc("@action:button close inline notification", "Close")
                        onClicked: InlineMessageModel.pop(strip.index)
                    }
                }
                // what was copied or shared goes after a while; saved, errors and found codes stay until closed
                Timer {
                    running: strip.kind === InlineMessageModel.Copied || strip.kind === InlineMessageModel.Shared
                    interval: 10000
                    onTriggered: InlineMessageModel.pop(strip.index)
                }
            }
        }
    }

    // ── The tool in use's settings, floating under the top row; Crop's own row. Each lines up with
    // the button of the tool that called it, as the overlay's options row does: for a tool on the toolbar's right half the row's
    // last control stands right under the tool, on the left half its first one; kept in the window. ──
    readonly property rect toolRect: {
        // positions it follows (mapToItem alone does not)
        const follows = [root.width, topDock.width, tools.x, tools.width, tools.currentTool?.x ?? 0]
        const t = tools.currentTool
        if (!t || follows.length === 0) {
            return Qt.rect(root.width / 2, topDock.y, 0, topDock.height)
        }
        const p = t.mapToItem(root, 0, 0)
        return Qt.rect(p.x, p.y, t.width, t.height)
    }
    function toolRowX(row) {
        const fromRight = toolRect.x + toolRect.width / 2 > tools.x + tools.width / 2
        const x = fromRight ? toolRect.x + toolRect.width + row.sidePadding - row.width : toolRect.x - row.sidePadding
        return Math.round(Math.max(EctoTheme.barInset, Math.min(x, root.width - EctoTheme.barInset - row.width)))
    }
    EctoRow {
        id: optionsRow
        objectName: "windowOptionsRow"
        followsTool: true
        x: root.toolRowX(optionsRow)
        y: topDock.height + messages.height + EctoTheme.rowGap
        z: 2
        visible: root.picture && root.toolType !== AnnotationTool.CropTool && toolOptions.hasOptions
        EctoToolOptions {
            id: toolOptions
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    EctoRow {
        id: cropRow
        objectName: "cropRow"
        readonly property rect geometry: root.document.tool.geometry ?? Qt.rect(0, 0, 0, 0)
        readonly property bool hasArea: geometry.width > 0 && geometry.height > 0
        followsTool: true
        x: root.toolRowX(cropRow)
        y: optionsRow.y
        z: 2
        visible: root.picture && root.toolType === AnnotationTool.CropTool
        EctoLabel {
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: cropRow.hasArea
                    ? i18nc("@info the size a crop will make", "Crop to %1×%2", String(Math.round(cropRow.geometry.width * root.document.imageDpr)), String(Math.round(cropRow.geometry.height * root.document.imageDpr)))
                    : i18nc("@info", "Drag over the picture to crop it")
                color: root.t.fgSubtle
                font.family: EctoTheme.sans
                font.pixelSize: 12
            }
        }
        EctoSeparator {}
        EctoButton {
            iconName: "dialog-cancel"
            text: i18nc("@action:button", "Cancel")
            shortcutText: i18nc("@info key name", "Esc")
            onClicked: {
                root.document.tool.geometry = undefined
                root.document.tool.type = AnnotationTool.NoTool
            }
        }
        EctoButton {
            objectName: "cropApply"
            iconName: "dialog-ok-apply"
            text: i18nc("@action:button", "Apply")
            shortcutText: i18nc("@info key name", "Enter")
            enabled: cropRow.hasArea
            onClicked: root.applyCrop()
        }
    }
    function applyCrop() {
        if (!cropRow.hasArea) {
            return
        }
        root.document.cropCanvas(root.document.tool.geometry)
        root.document.tool.geometry = undefined
        root.document.tool.type = AnnotationTool.NoTool
    }
    Shortcut {
        enabled: root.picture && root.toolType === AnnotationTool.CropTool
        sequences: ["Return", "Enter"]
        onActivated: root.applyCrop()
    }
    Shortcut {
        enabled: root.picture && root.toolType === AnnotationTool.CropTool
        sequence: "Escape"
        onActivated: {
            root.document.tool.geometry = undefined
            root.document.tool.type = AnnotationTool.NoTool
        }
    }

    // ── Bottom row: what to do with this picture at the left, New Capture at the right ──
    component Words: EctoSplitButton {
        iconOnly: root.narrow
    }
    Rectangle {
        id: bottomDock
        readonly property bool ectoBar: true
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 44
        color: root.t.bg
        Rectangle {
            width: parent.width
            height: 1
            color: root.t.border
        }
        Row {
            id: leftGroup
            anchors.left: parent.left
            anchors.leftMargin: EctoTheme.barInset
            anchors.verticalCenter: parent.verticalCenter
            spacing: EctoTheme.rowSpacing
            visible: !root.empty
            // after a recording: play, the scrubber and the time
            EctoButton {
                visible: root.video
                iconName: root.view?.playing ? "media-playback-pause" : "media-playback-start"
                iconOnly: true
                text: root.view?.playing ? i18nc("@action:button", "Pause") : i18nc("@action:button", "Play")
                enabled: root.view?.hasContent ?? false
                onClicked: root.view.togglePlay()
            }
            // the scrubber: Ectolens's slider, its thumb at the playing point, room past its ends (EctoTheme.sliderSidePadding)
            EctoSlider {
                id: scrubber
                objectName: "scrubber"
                visible: root.video && !(root.view?.hasAnimatedImage ?? false)
                anchors.verticalCenter: parent.verticalCenter
                width: root.narrow ? 80 : 140
                height: 28
                thumbSize: 12
                enabled: root.view?.player?.seekable ?? false
                value: (root.view?.player?.duration ?? 0) > 0 ? root.view.player.position / root.view.player.duration : 0
                // never closer to the end than five frames: on the paused player a seek into the last few frames
                // ends the media and the window rewinds it, so the far right showed the first frame (seen in the
                // video run; Qt's FFmpeg player, 2026-11-03)
                readonly property real tail: {
                    const fps = root.view?.player?.metaData.value(MediaMetaData.VideoFrameRate) ?? 0
                    return fps > 0 ? 5000 / fps : 100
                }
                onMoved: v => root.view.player.setPosition(Math.round(Math.max(0, Math.min(v * root.view.player.duration, root.view.player.duration - tail))))
            }
            Text {
                visible: root.video && !(root.view?.hasAnimatedImage ?? false)
                anchors.verticalCenter: parent.verticalCenter
                rightPadding: 6
                text: SpectacleCore.timeFromMilliseconds(root.view?.player?.position ?? 0) + " / " + SpectacleCore.timeFromMilliseconds(root.view?.player?.duration ?? 0)
                color: root.t.fgMuted
                font.family: EctoTheme.mono
                font.pixelSize: 12
                font.weight: Font.Medium
            }
            EctoSeparator { visible: root.video }
            Words {
                id: copySplit
                objectName: "windowCopy"
                iconName: "edit-copy"
                text: i18nc("@action:button", "Copy")
                shortcutText: root.video ? "" : CaptureTools.keyText("Ctrl+C")
                toolTipText: root.video ? i18nc("@info:tooltip", "Copy the file's path") : i18nc("@info:tooltip", "Copy the picture")
                menuOpen: copyMenu.opened
                onClicked: root.video ? CaptureTools.copyCurrentPath() : contextWindow.copyImage()
                onArrowClicked: copyMenu.toggleAt(copySplit, "above")
            }
            Words {
                id: saveSplit
                objectName: "windowSave"
                iconName: "document-save"
                text: i18nc("@action:button", "Save")
                shortcutText: CaptureTools.keyText("Ctrl+S")
                toolTipText: i18nc("@info:tooltip", "Save to Screenshots")
                menuOpen: saveMenu.opened
                onClicked: contextWindow.save()
                onArrowClicked: saveMenu.toggleAt(saveSplit, "above")
            }
            EctoSeparator { visible: extractSplit.visible }
            Words {
                id: extractSplit
                objectName: "windowExtract"
                visible: root.picture && SpectacleCore.ocrAvailable
                iconName: "document-scan"
                text: i18nc("@action", "Extract Text")
                toolTipText: i18nc("@info:tooltip", "Copy the words in the picture")
                button.enabled: SpectacleCore.ocrStatus !== 1
                menuOpen: languageMenu.opened
                onClicked: SpectacleCore.startOcrExtraction()
                onArrowClicked: languageMenu.toggleAt(extractSplit, "above")
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: EctoTheme.barInset
            anchors.verticalCenter: parent.verticalCenter
            spacing: EctoTheme.rowSpacing
            EctoButton {
                id: zoomButton
                objectName: "windowZoom"
                visible: root.picture && !root.narrow
                quiet: true
                text: !root.view || root.view.fitMode ? i18nc("@action:button zoom to fit the window", "Fit") : i18nc("@info zoom percent", "%1%", Math.round(root.view.currentZoom * 100))
                toolTipText: i18nc("@info:tooltip", "Zoom")
                menuOpen: zoomMenu.opened
                onClicked: zoomMenu.toggleAt(zoomButton, "above")
                contentItem: Item {
                    implicitWidth: zoomRow.implicitWidth
                    Row {
                        id: zoomRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: zoomButton.text
                            color: root.t.fgMuted
                            font.family: EctoTheme.sans
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        EctoChevron {
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.t.fg
                        }
                    }
                }
            }
            Words {
                id: newSplit
                objectName: "newCapture"
                iconName: "camera-photo"
                text: i18nc("@action:button", "New Capture")
                shortcutText: CaptureTools.keyText("Ctrl+N")
                toolTipText: i18nc("@info:tooltip", "Frame a window, an area or the desktop")
                menuOpen: newMenu.opened
                onClicked: root.newCapture(CaptureModeModel.RectangularRegion)
                onArrowClicked: newMenu.toggleAt(newSplit, "above")
            }
        }
    }
    function newCapture(mode) {
        SpectacleCore.takeNewScreenshot(mode, Math.round(Settings.captureDelay * 1000), Settings.includePointer, Settings.includeDecorations, Settings.includeShadow)
    }

    // measuring rows: what the rows need with words, to know when they don't fit
    Row {
        id: wideBottom
        visible: false
        spacing: 2
        EctoSplitButton { iconName: "edit-copy"; text: i18nc("@action:button", "Copy") }
        EctoSplitButton { iconName: "document-save"; text: i18nc("@action:button", "Save") }
        EctoSeparator {}
        EctoSplitButton { iconName: "document-scan"; text: i18nc("@action", "Extract Text") }
        Item { width: 40; height: 1 }
        EctoButton { quiet: true; text: "100%" }
        EctoSplitButton { iconName: "camera-photo"; text: i18nc("@action:button", "New Capture") }
    }
    EctoTools {
        id: toolsMeasure
        visible: false
        inOverlay: false
    }

    // ── Menus ──
    EctoMenu {
        id: copyMenu
        minimumWidth: 230
        EctoMenuHeader { text: i18nc("@title:menu", "Copy") }
        EctoMenuItem {
            visible: root.picture
            iconName: "image-x-generic"
            text: i18nc("@action:inmenu copy the picture", "Image")
            shortcutText: CaptureTools.keyText("Ctrl+C")
            onClicked: contextWindow.copyImage()
        }
        EctoMenuItem {
            iconName: "edit-copy-path"
            text: i18nc("@action:inmenu copy the saved file's path", "File Path")
            // a path needs a saved file
            enabled: root.video || CaptureTools.savedUrl.toString().length > 0
            reason: enabled ? "" : i18nc("@info why File Path can't be copied yet", "Save first")
            onClicked: CaptureTools.copyCurrentPath()
        }
    }
    EctoMenu {
        id: saveMenu
        minimumWidth: 260
        EctoMenuHeader { text: i18nc("@title:menu", "Save") }
        EctoMenuItem {
            iconName: "document-save-as"
            text: i18nc("@action:inmenu", "Save As…")
            shortcutText: CaptureTools.keyText("Ctrl+Shift+S")
            onClicked: contextWindow.saveAs()
        }
        EctoMenuItem {
            iconName: "document-open-folder"
            text: i18nc("@action:inmenu", "Show in Folder")
            enabled: root.video || CaptureTools.savedUrl.toString().length > 0
            onClicked: CaptureTools.showInFolder(root.video ? SpectacleCore.currentVideo : CaptureTools.savedUrl)
        }
        EctoMenuSeparator {}
        EctoMenuHeader { text: i18nc("@title:menu", "Send to") }
        EctoMenuItem {
            id: openWithItem
            iconName: "document-open"
            text: i18nc("@action:inmenu", "Open With")
            submenu: true
            onClicked: openWithMenu.openAt(openWithItem, "beside")
        }
        EctoMenuItem {
            id: shareItem
            iconName: "document-share"
            text: i18nc("@action:inmenu", "Share")
            submenu: true
            onClicked: shareMenu.openAt(shareItem, "beside")
        }
        EctoMenuItem {
            visible: root.picture
            iconName: "document-print"
            text: i18nc("@action:inmenu", "Print…")
            shortcutText: CaptureTools.keyText("Ctrl+P")
            onClicked: contextWindow.showPrintDialog()
        }
    }
    EctoMenu {
        id: openWithMenu
        minimumWidth: 220
        Repeater {
            model: openWithMenu.opened ? CaptureTools.openWithApps().filter(a => a.id !== "") : []
            EctoMenuItem {
                required property var modelData
                iconName: modelData.icon
                text: modelData.name
                onClicked: {
                    saveMenu.close()
                    CaptureTools.openCurrentWith(modelData.id)
                }
            }
        }
    }
    EctoMenu {
        id: shareMenu
        minimumWidth: 220
        Repeater {
            model: shareMenu.opened ? CaptureTools.shareTargets() : []
            EctoMenuItem {
                required property var modelData
                iconName: modelData.icon
                text: modelData.name
                onClicked: {
                    saveMenu.close()
                    CaptureTools.shareCurrent(modelData.id)
                }
            }
        }
    }
    EctoMenu {
        id: languageMenu
        minimumWidth: 220
        EctoMenuHeader { text: i18nc("@title:menu", "Extract Text in") }
        Repeater {
            model: {
                if (!languageMenu.opened) return []
                const map = SpectacleCore.ocrAvailableLanguages()
                return Object.keys(map).map(code => ({code: code, name: map[code]})).sort((a, b) => a.name.localeCompare(b.name))
            }
            EctoMenuItem {
                required property var modelData
                text: modelData.name
                onClicked: SpectacleCore.startOcrExtraction(modelData.code)
            }
        }
    }
    EctoMenu {
        id: zoomMenu
        minimumWidth: 220
        readonly property real zoom: root.view ? root.view.currentZoom : 1
        EctoMenuItem {
            tickColumn: true
            tick: root.view?.fitMode ?? true
            text: i18nc("@action:inmenu", "Fit to Window")
            shortcutText: CaptureTools.keyText("Ctrl+0")
            onClicked: root.view.fit()
        }
        Repeater {
            model: [1, 1.5, 2]
            EctoMenuItem {
                required property real modelData
                tickColumn: true
                tick: !(root.view?.fitMode ?? true) && Math.abs(zoomMenu.zoom - modelData) < 0.001
                text: modelData === 1 ? i18nc("@action:inmenu zoom to 100%", "Actual Size") : i18nc("@item zoom percent", "%1%", modelData * 100)
                shortcutText: modelData === 1 ? CaptureTools.keyText("Ctrl+1") : ""
                onClicked: root.view.zoomToPercent(modelData)
            }
        }
        EctoMenuSeparator {}
        EctoMenuItem {
            iconName: "zoom-in"
            text: i18nc("@action:inmenu", "Zoom In")
            shortcutText: CaptureTools.keyText("Ctrl++")
            closesMenu: false
            onClicked: root.view.zoomIn()
        }
        EctoMenuItem {
            iconName: "zoom-out"
            text: i18nc("@action:inmenu", "Zoom Out")
            shortcutText: CaptureTools.keyText("Ctrl+-")
            closesMenu: false
            onClicked: root.view.zoomOut()
        }
    }
    EctoMenu {
        id: newMenu
        minimumWidth: 300
        EctoMenuHeader { text: i18nc("@title:menu", "New Capture") }
        EctoMenuItem {
            iconName: "crosshairs"
            text: i18nc("@action:inmenu", "Region or Window")
            shortcutText: CaptureTools.globalShortcut("open")
            onClicked: root.newCapture(CaptureModeModel.RectangularRegion)
        }
        EctoMenuItem {
            iconName: "monitor"
            text: i18nc("@action:inmenu", "Full Screen")
            shortcutText: CaptureTools.globalShortcut("fullScreen")
            onClicked: root.newCapture(CaptureModeModel.AllScreens)
        }
        EctoMenuItem {
            iconName: "window"
            text: i18nc("@action:inmenu", "Active Window")
            shortcutText: CaptureTools.globalShortcut("activeWindow")
            onClicked: root.newCapture(CaptureModeModel.ActiveWindow)
        }
        EctoMenuSeparator {}
        component Check: EctoMenuRow {
            id: c
            property string setting
            checked: Settings[c.setting]
            onToggled: on => {
                Settings[c.setting] = on
                Settings.save()
            }
        }
        Check { setting: "includePointer"; text: i18nc("@option:check", "Include mouse pointer") }
        Check { setting: "includeDecorations"; text: i18nc("@option:check", "Include window titlebar and borders") }
        Check { setting: "includeShadow"; text: i18nc("@option:check", "Include window shadow") }
        EctoDelay {}
    }
    EctoMenu {
        id: gearMenu
        minimumWidth: 260
        EctoMenuItem {
            iconName: "configure"
            text: i18nc("@action:inmenu", "Settings…")
            shortcutText: CaptureTools.keyText("Ctrl+Shift+,")
            onClicked: CaptureTools.openSettings()
        }
        EctoMenuItem {
            iconName: "configure-shortcuts"
            text: i18nc("@action:inmenu", "Keyboard Shortcuts…")
            onClicked: CaptureTools.openShortcuts()
        }
        EctoMenuSeparator {}
        EctoMenuItem {
            iconName: "help-contents"
            text: i18nc("@action:inmenu", "Ectolens Help")
            shortcutText: CaptureTools.keyText("F1")
            onClicked: CaptureTools.help("help")
        }
        EctoMenuItem {
            iconName: "tools-report-bug"
            text: i18nc("@action:inmenu", "Report a Bug…")
            onClicked: CaptureTools.help("bug")
        }
        EctoMenuItem {
            iconName: "help-about"
            text: i18nc("@action:inmenu", "About Ectolens")
            onClicked: CaptureTools.help("about")
        }
    }
    // Show Text: the words read from the picture, to read and copy
    EctoMenu {
        id: textMenu
        minimumWidth: 420
        property string text: ""
        function show(text, item) {
            textMenu.text = text
            openAt(item, "below")
        }
        QQC.ScrollView {
            readonly property bool fillsWidth: true
            implicitHeight: Math.min(320, textArea.implicitHeight + 8)
            QQC.TextArea {
                id: textArea
                // the words start and end where every menu line's do
                leftPadding: EctoTheme.controlPadding
                rightPadding: EctoTheme.controlPadding
                text: textMenu.text
                readOnly: true
                wrapMode: Text.Wrap
                selectByMouse: true
                color: root.t.fg
                font.family: EctoTheme.mono
                font.pixelSize: 12
                background: null
            }
        }
        EctoMenuSeparator {}
        EctoMenuItem {
            iconName: "edit-copy"
            text: i18nc("@action:inmenu", "Copy the Text")
            onClicked: CaptureTools.copyText(textMenu.text)
        }
    }

    // Zoom keys (gap 3)
    Shortcut {
        enabled: root.picture && root.view !== null
        sequences: [StandardKey.ZoomIn]
        onActivated: root.view.zoomIn()
    }
    Shortcut {
        enabled: root.picture && root.view !== null
        sequences: [StandardKey.ZoomOut]
        onActivated: root.view.zoomOut()
    }
    Shortcut {
        enabled: root.picture && root.view !== null
        sequence: "Ctrl+0"
        onActivated: root.view.fit()
    }
    Shortcut {
        enabled: root.picture && root.view !== null
        sequence: "Ctrl+1"
        onActivated: root.view.zoomToPercent(1)
    }
    // FIXME: This shortcut only exists here because spectacle interprets "Ctrl+Shift+,"
    // as "Ctrl+Shift+<" for some reason unless we use a QML Shortcut.
    Shortcut {
        sequences: [StandardKey.Preferences]
        onActivated: contextWindow.showPreferencesDialog()
    }
}
