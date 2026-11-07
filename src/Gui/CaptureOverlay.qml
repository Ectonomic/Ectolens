/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>
 * SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Window
import QtQuick.Shapes
import QtQuick.Controls as QQC
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private
import org.kde.kquickimageeditor.ectolens

/**
 * The capture overlay, design proposal 2 (design/mock/ui-v2.html, approved 2026-11-02).
 *
 * Pointing: the window under the pointer stays bright, the rest dims, its label sits above
 * it, and one line at the top says how to choose. Framed: above the frame, the frame label
 * (it opens the size menu) and the marking tools; below it, Capture and Record side by
 * side, the picture tools, and the gear. Both rows are centered on the frame and slide
 * sideways only to stay on screen; a row with no room on its side moves just inside the
 * frame. Recording: the frame becomes a dashed outline and the row holds the time, a Mic
 * marker, Pause and Stop, centered under the frame like the rows before it.
 *
 * - `contextWindow` is the CaptureWindow showing this overlay (one per screen).
 * - `SelectionEditor` and `Selection` are the frame; their coordinates are logical and global.
 */
MouseArea {
    // This needs to be a mousearea in orcer for the proper mouse events to be correctly filtered
    id: root
    readonly property var t: EctoTheme.overlay
    readonly property rect viewportRect: Geometry.mapFromPlatformRect(screenToFollow.geometry, screenToFollow.devicePixelRatio)
    /// an AI agent's request is asking (its dialog up, the rest dimmed), until Adjust First or an answer
    readonly property bool agentModal: SpectacleCore.agentAsking && !SpectacleCore.agentAdjusting && root.framed && !root.recording
    readonly property AnnotationDocument document: annotationsLoader.item?.document ?? null
    readonly property size rawSelectionSize: {
        const logicalSize = SelectionEditor.selection.empty
            ? Qt.size(SelectionEditor.screensRect.width, SelectionEditor.screensRect.height)
            : SelectionEditor.selection.size
        const scale = () => {
            if (Geometry.rectContains(root.viewportRect, SelectionEditor.selection.rect)) {
                return contextWindow.devicePixelRatio
            } else if (!SpectacleCore.videoMode) {
                return SpectacleCore.annotationDocument.imageDpr
            }
            // The scale for KWin's region screencasting raw size is the largest of the overlapped screen scales.
            let dpr = 0
            for (let window of contextWindow.captureWindows) {
                if (Geometry.rectIntersects(SelectionEditor.selection.rect, window.logicalX, window.logicalY, window.width, window.height)) {
                    dpr = Math.max(dpr, window.devicePixelRatio)
                }
            }
            return dpr
        }
        return Geometry.rawSize(logicalSize, scale())
    }
    readonly property bool recording: SpectacleCore.videoPlatform.isRecording
    // from Stop until the file is saved and the overlay closes: the overlay draws nothing and
    // takes no clicks. It used to fall back to the frame with Record and the gear for about half a
    // second, while the file was finished and saved (the recorder's Rendering, then Finished until
    // the save closes the overlay). A recording's overlay never goes back to framing, so once a
    // recording has ended it stays blank.
    property bool recorded: false
    onRecordingChanged: if (recording) recorded = true
    readonly property bool finishing: SpectacleCore.videoPlatform.isFinishing || (recorded && !recording)
    opacity: finishing ? 0 : 1
    readonly property bool framed: !SelectionEditor.selection.empty
    readonly property bool pressed: SelectionEditor.dragLocation !== SelectionEditor.None || (annotationsLoader.item?.anyPressed ?? false)
    /// Pick Color or Measure: the pointer works on the picture instead of the frame.
    /// Shared by every screen's overlay, so Esc works whichever screen has the keyboard.
    readonly property string mode: CaptureTools.overlayMode
    // Where Record's center and its row were when it was pressed; Stop goes there.

    // the rows' spacing rules (design proposal 2, section 06)
    readonly property real edgeMargin: 8
    // whether this overlay shows the screen holding the frame's right edge
    function frameWindow() {
        const f = SelectionEditor.selection.rect
        const px = f.x + Math.max(0, f.width - 1), py = f.y + f.height / 2
        const v = root.viewportRect
        return px >= v.x && px < v.x + v.width && py >= v.y && py < v.y + v.height ? contextWindow : null
    }
    readonly property real frameGap: 15 // the handle plus some air

    focus: true
    acceptedButtons: recording ? Qt.NoButton : Qt.LeftButton | Qt.RightButton
    hoverEnabled: !recording
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    anchors.fill: parent

    Keys.onEscapePressed: event => {
        if (root.mode !== "") {
            CaptureTools.overlayMode = ""
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    function isWholeScreen(r) {
        for (let i = 0; i < Qt.application.screens.length; ++i) {
            const s = Qt.application.screens[i]
            if (r.x === s.virtualX && r.y === s.virtualY && r.width === s.width && r.height === s.height) {
                return true
            }
        }
        return false
    }
    function showToast(text) {
        toast.text = text
        toastTimer.restart()
    }

    // Undo and Redo step back through the frame's moves and resizes as well as the marks,
    // in the order they happened (FrameHistory)
    Shortcut {
        enabled: root.framed && !root.recording && root.mode === "" && !root.agentModal
        sequences: [StandardKey.Undo]
        onActivated: FrameHistory.undo()
    }
    Shortcut {
        enabled: root.framed && !root.recording && root.mode === "" && !root.agentModal
        sequences: [StandardKey.Redo]
        onActivated: FrameHistory.redo()
    }

    AnimatedLoader {
        id: annotationsLoader
        anchors.fill: parent
        state: !SpectacleCore.videoMode ? "active" : "inactive"
        animationDuration: Kirigami.Units.veryLongDuration
        sourceComponent: AnnotationEditor {
            undoKeys: false
            enabled: contextWindow.annotating && root.mode === ""
            document: SpectacleCore.annotationDocument
            viewportRect: root.viewportRect
        }
    }

    // ── Dimming: the rest of the screen darkens around the frame, or around the window
    // under the pointer while pointing (Settings › Appearance › Dim the rest). ──
    readonly property rect brightRect: {
        if (root.framed) {
            return SelectionEditor.selection.rect
        }
        if (hoverTarget.visible) {
            return SelectionEditor.hoverRect
        }
        return Qt.rect(0, 0, 0, 0)
    }
    Item {
        id: bright
        x: root.brightRect.x - root.viewportRect.x
        y: root.brightRect.y - root.viewportRect.y
        width: root.brightRect.width
        height: root.brightRect.height
    }
    component Dim: Rectangle {
        color: Settings.useLightMaskColor ? "white" : "black"
        opacity: if (root.recording
            || (annotationsLoader.visible && !root.framed
                && root.document && (!root.document.tool.isNoTool || root.document.undoStackDepth > 0))) {
            return 0
        } else {
            return EctoTheme.dim
        }
        LayoutMirroring.enabled: false
        Behavior on opacity {
            NumberAnimation {
                duration: Kirigami.Units.longDuration
                easing.type: Easing.OutCubic
            }
        }
    }
    readonly property bool hasBright: brightRect.width > 0 && brightRect.height > 0
    Dim { // above, or everything when nothing is bright
        id: dimTop
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.hasBright ? Math.max(0, bright.y) : parent.height
    }
    Dim { // below
        visible: root.hasBright
        anchors.left: parent.left
        anchors.right: parent.right
        y: Math.max(0, bright.y + bright.height)
        height: Math.max(0, parent.height - y)
    }
    Dim { // left
        visible: root.hasBright
        x: 0
        y: Math.max(0, bright.y)
        width: Math.max(0, bright.x)
        height: Math.max(0, Math.min(parent.height, bright.y + bright.height) - y)
    }
    Dim { // right
        visible: root.hasBright
        x: Math.max(0, bright.x + bright.width)
        y: Math.max(0, bright.y)
        width: Math.max(0, parent.width - x)
        height: Math.max(0, Math.min(parent.height, bright.y + bright.height) - y)
    }

    // ── The frame, in the accent; dashed while recording ──
    DashedOutline {
        id: selectionRectangle
        objectName: "frameLine"
        // We need to be a bit careful about staying out of the recorded area
        function roundPos(value: real) : real {
            return root.recording ? dprFloor(value - (strokeWidth + 1 / contextWindow.devicePixelRatio)) : dprRound(value - strokeWidth)
        }
        function roundSize(value: real) : real {
            return root.recording ? dprCeil(value + (strokeWidth + 1 / contextWindow.devicePixelRatio)) : dprRound(value + strokeWidth)
        }
        pathHints: ShapePath.PathLinear
        dashSvgPath: root.recording ? svgPath : ""
        visible: root.framed
            && Geometry.rectIntersects(Qt.rect(x,y,width,height), Qt.rect(0,0,parent.width, parent.height))
        enabled: root.document?.tool.isNoTool || SpectacleCore.videoMode
        strokeWidth: dprRound(1)
        // while recording, dashes of the accent with gaps you see through
        strokeColor: root.recording ? "transparent" : enabled ? EctoTheme.accent : Qt.alpha(EctoTheme.accent, 0.45)
        dashColor: root.recording ? EctoTheme.accent : strokeColor
        x: roundPos(SelectionEditor.selection.x - root.viewportRect.x)
        y: roundPos(SelectionEditor.selection.y - root.viewportRect.y)
        width: roundSize(SelectionEditor.selection.right - root.viewportRect.x) - x
        height: roundSize(SelectionEditor.selection.bottom - root.viewportRect.y) - y
    }
    // on the frame line itself: the middle of its stroke, as the line is placed (outside the frame, on
    // whole device pixels), so each handle's circle is centered on the line at any scale
    // (placing them on the frame's own edge put them half a line inward)
    FrameHandles {
        objectName: "frameHandles"
        // not while an agent's dialog asks: the frame takes no drag then (the dim does), so it shows
        // no grips; Adjust First brings them back
        visible: root.framed && !root.recording && selectionRectangle.enabled && root.mode === ""
            && SelectionEditor.dragLocation === SelectionEditor.None && !root.agentModal
        line: selectionRectangle.strokeWidth
        x: selectionRectangle.x + line / 2
        y: selectionRectangle.y + line / 2
        width: selectionRectangle.width - line
        height: selectionRectangle.height - line
    }

    // ── Pointing: the window under the pointer, outlined in the accent ──
    Item {
        id: hoverTarget
        readonly property rect target: SelectionEditor.hoverRect
        readonly property bool isDesktop: !SelectionEditor.hoverIsWindow
        visible: !root.framed && target.width > 0 && !root.recording && SelectionEditor.dragLocation === SelectionEditor.None
        x: target.x - root.viewportRect.x
        y: target.y - root.viewportRect.y
        width: target.width
        height: target.height
        z: 1
        // the window, or over the desktop the whole screen: what a click takes
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: 2
            border.color: EctoTheme.accent
        }
    }

    // ── Pick Color and Measure take the pointer over the picture ──
    property point toolPoint: Qt.point(-1, -1) // global, logical
    property point measureStart: Qt.point(-1, -1)
    property point measureEnd: Qt.point(-1, -1)
    readonly property color pickedColor: root.mode === "color" && toolPoint.x >= 0 ? CaptureTools.colorAt(toolPoint) : "transparent"
    function hex(c) {
        const h = v => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return ("#" + h(c.r) + h(c.g) + h(c.b)).toUpperCase()
    }
    MouseArea {
        id: toolArea
        anchors.fill: parent
        z: 40
        enabled: root.mode !== "" && !root.recording
        visible: enabled
        hoverEnabled: true
        cursorShape: Qt.CrossCursor
        function global(m) {
            return Qt.point(m.x + root.viewportRect.x, m.y + root.viewportRect.y)
        }
        // a passive handler sees the pointer even while nothing is pressed
        HoverHandler {
            onPointChanged: if (toolArea.enabled) {
                root.toolPoint = Qt.point(point.position.x + root.viewportRect.x, point.position.y + root.viewportRect.y)
            }
        }
        function snapped(m) {
            let p = global(m)
            if (m.modifiers & Qt.ShiftModifier) {
                // Shift keeps the line straight across or straight down
                if (Math.abs(p.x - root.measureStart.x) > Math.abs(p.y - root.measureStart.y)) {
                    p.y = root.measureStart.y
                } else {
                    p.x = root.measureStart.x
                }
            }
            return p
        }
        onPositionChanged: m => {
            root.toolPoint = global(m)
            if (root.mode === "measure" && pressed) {
                root.measureEnd = snapped(m)
            }
        }
        onPressed: m => {
            if (root.mode === "measure") {
                root.measureStart = global(m)
                root.measureEnd = root.measureStart
            }
        }
        onClicked: m => {
            if (root.mode === "color") {
                const hex = root.hex(CaptureTools.colorAt(global(m)))
                CaptureTools.copyText(hex)
                root.showToast(i18nc("@info %1 a color like #A1B2C3", "Copied %1", hex))
                CaptureTools.overlayMode = ""
            }
        }
    }

    // ── Everything placed in global, logical coordinates (all screens) ──
    Item {
        id: screensRectItem
        z: 60
        width: SelectionEditor.screensRect.width
        height: SelectionEditor.screensRect.height
        x: -root.viewportRect.x
        y: -root.viewportRect.y

        readonly property bool showRows: !root.recording && !root.pressed

        function screenAt(px, py) {
            for (let i = 0; i < Qt.application.screens.length; ++i) {
                const s = Qt.application.screens[i]
                if (px >= s.virtualX && px < s.virtualX + s.width && py >= s.virtualY && py < s.virtualY + s.height) {
                    return Qt.rect(s.virtualX, s.virtualY, s.width, s.height)
                }
            }
            return root.viewportRect
        }
        // Centered on the frame, kept on its screen.
        function rowX(w, F, bounds) {
            return Math.round(Math.max(bounds.x + root.edgeMargin,
                                       Math.min(F.x + F.width / 2 - w / 2, bounds.x + bounds.width - root.edgeMargin - w)))
        }
        // Above the frame, or just inside its top edge when there is no room.
        function topY(h, F, bounds) {
            let y = F.y - root.frameGap - h
            if (y < bounds.y + root.edgeMargin) {
                y = F.y + root.frameGap
            }
            return Math.round(y)
        }
        // Below the frame, or just inside its bottom edge when there is no room.
        function bottomY(h, F, bounds) {
            let y = F.y + F.height + root.frameGap
            if (y + h > bounds.y + bounds.height - root.edgeMargin) {
                y = F.y + F.height - root.frameGap - h
            }
            return Math.round(y)
        }
        readonly property rect frame: SelectionEditor.selection.rect
        readonly property rect topBounds: screenAt(frame.x + frame.width / 2, Math.max(frame.y, 0) + 1)
        readonly property rect bottomBounds: screenAt(frame.x + frame.width / 2, frame.y + frame.height - 1)
        readonly property rect mouseScreen: screenAt(SelectionEditor.mousePosition.x, SelectionEditor.mousePosition.y)

        // Magnifier (Shift while framing, and Pick Color)
        Loader {
            id: magnifierLoader
            readonly property bool picking: root.mode === "color" && root.toolPoint.x >= 0
            readonly property point targetPoint: {
                if (picking) {
                    return root.toolPoint
                }
                if (SelectionEditor.magnifierLocation === SelectionEditor.FollowMouse) {
                    return SelectionEditor.mousePosition
                } else {
                    let x = -width
                    let y = -height
                    if (SelectionEditor.magnifierLocation === SelectionEditor.TopLeft
                        || SelectionEditor.magnifierLocation === SelectionEditor.Left
                        || SelectionEditor.magnifierLocation === SelectionEditor.BottomLeft) {
                        x = SelectionEditor.selection.left
                    } else if (SelectionEditor.magnifierLocation === SelectionEditor.TopRight
                        || SelectionEditor.magnifierLocation === SelectionEditor.Right
                        || SelectionEditor.magnifierLocation === SelectionEditor.BottomRight) {
                        x = SelectionEditor.selection.right
                    } else if (SelectionEditor.magnifierLocation === SelectionEditor.Top
                        || SelectionEditor.magnifierLocation === SelectionEditor.Bottom) {
                        x = SelectionEditor.dragLocation !== SelectionEditor.None ? SelectionEditor.mousePosition.x : SelectionEditor.selection.horizontalCenter
                    }
                    if (SelectionEditor.magnifierLocation === SelectionEditor.TopLeft
                        || SelectionEditor.magnifierLocation === SelectionEditor.Top
                        || SelectionEditor.magnifierLocation === SelectionEditor.TopRight) {
                        y = SelectionEditor.selection.top
                    } else if (SelectionEditor.magnifierLocation === SelectionEditor.BottomLeft
                        || SelectionEditor.magnifierLocation === SelectionEditor.Bottom
                        || SelectionEditor.magnifierLocation === SelectionEditor.BottomRight) {
                        y = SelectionEditor.selection.bottom
                    } else if (SelectionEditor.magnifierLocation === SelectionEditor.Left
                        || SelectionEditor.magnifierLocation === SelectionEditor.Right) {
                        y = SelectionEditor.dragLocation !== SelectionEditor.None ? SelectionEditor.mousePosition.y : SelectionEditor.selection.verticalCenter
                    }
                    return Qt.point(x, y)
                }
            }
            readonly property rect rect: {
                const margin = Kirigami.Units.gridUnit
                let x = targetPoint.x + margin
                let y = targetPoint.y + margin
                if (!picking && SelectionEditor.magnifierLocation !== SelectionEditor.FollowMouse) {
                    const loc = SelectionEditor.magnifierLocation
                    if (loc === SelectionEditor.TopLeft || loc === SelectionEditor.Left || loc === SelectionEditor.BottomLeft) {
                        x = targetPoint.x - width - margin
                    } else if (loc === SelectionEditor.Top || loc === SelectionEditor.Bottom) {
                        x = targetPoint.x - width / 2
                    }
                    if (loc === SelectionEditor.TopLeft || loc === SelectionEditor.Top || loc === SelectionEditor.TopRight) {
                        y = targetPoint.y - width - margin
                    } else if (loc === SelectionEditor.Left || loc === SelectionEditor.Right) {
                        y = targetPoint.y - height / 2
                    }
                }
                return Geometry.rectBounded(dprRound(x), dprRound(y), width, height, SelectionEditor.screensRect)
            }
            x: rect.x
            y: rect.y
            z: 100
            visible: active && (picking
                || (SelectionEditor.showMagnifier && SelectionEditor.magnifierLocation !== SelectionEditor.None))
                && Geometry.rectIntersects(rect, root.viewportRect)
            active: !SpectacleCore.videoMode && annotationsLoader.item !== null
                && (picking || (Settings.showMagnifier !== Settings.ShowMagnifierNever && (root.document?.tool.isNoTool ?? false)))
            sourceComponent: Magnifier {
                viewport: annotationsLoader.item
                targetPoint: magnifierLoader.targetPoint
            }
        }

        // Pick Color: the color under the pointer, beside the magnifier
        EctoRow {
            visible: magnifierLoader.picking
            z: 101
            x: magnifierLoader.x
            y: magnifierLoader.y + magnifierLoader.height + 6
            EctoLabel {
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    radius: 4
                    color: root.pickedColor
                    border.width: 1
                    border.color: root.t.borderStrong
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.hex(root.pickedColor)
                    color: root.t.fg
                    font.family: EctoTheme.mono
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 2
                    text: i18nc("@info", "Click to copy")
                    color: root.t.fgSubtle
                    font.family: EctoTheme.sans
                    font.pixelSize: 12
                }
            }
        }

        // Measure: a line between two points and its length in pixels
        Shape {
            visible: root.mode === "measure" && root.measureStart.x >= 0
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: EctoTheme.accent
                strokeWidth: 1.5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: root.measureStart.x
                startY: root.measureStart.y
                PathLine { x: root.measureEnd.x; y: root.measureEnd.y }
            }
        }
        Repeater {
            model: root.mode === "measure" && root.measureStart.x >= 0 ? [root.measureStart, root.measureEnd] : []
            Rectangle {
                required property point modelData
                x: modelData.x - 4
                y: modelData.y - 4
                width: 8
                height: 8
                radius: 4
                color: EctoTheme.accent
                border.width: 1
                border.color: "#0a0a0a"
            }
        }
        EctoRow {
            id: measureLabel
            readonly property real dx: Math.abs(root.measureEnd.x - root.measureStart.x) * contextWindow.devicePixelRatio
            readonly property real dy: Math.abs(root.measureEnd.y - root.measureStart.y) * contextWindow.devicePixelRatio
            visible: root.mode === "measure" && root.measureStart.x >= 0 && (dx > 0 || dy > 0)
            z: 101
            x: (root.measureStart.x + root.measureEnd.x) / 2 - width / 2
            y: Math.min(root.measureStart.y, root.measureEnd.y) - height - 10
            EctoLabel {
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: i18nc("@info %1 the distance, %2×%3 across and down, in pixels", "%1 px  ·  %2 × %3",
                                String(Math.round(Math.hypot(measureLabel.dx, measureLabel.dy))), String(Math.round(measureLabel.dx)), String(Math.round(measureLabel.dy)))
                    color: root.t.fg
                    font.family: EctoTheme.mono
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }
        }

        // The size while dragging a frame
        AnimatedLoader {
            z: 100
            state: SelectionEditor.dragLocation && Geometry.rectIntersects(root.viewportRect, SelectionEditor.selection.rect)
                && Geometry.rectContains(root.viewportRect, x, y, width, height)
                ? "active" : "inactive"
            active: SelectionEditor.dragLocation || visible
            height: implicitHeight
            width: implicitWidth
            x: {
                let x = SelectionEditor.mousePosition.x + Kirigami.Units.gridUnit
                if (x + width > root.viewportRect.right) {
                    x -= Kirigami.Units.gridUnit * 2 + width
                }
                return dprRound(x)
            }
            y: {
                let y = SelectionEditor.mousePosition.y - height / 2
                if (y < root.viewportRect.top) {
                    y += height / 2
                } else if (y + height > root.viewportRect.bottom) {
                    y -= height / 2
                }
                return dprRound(y)
            }
            sourceComponent: EctoRow {
                EctoLabel {
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: i18nc("@info width×height in pixels", "%1×%2", String(Math.round(root.rawSelectionSize.width)), String(Math.round(root.rawSelectionSize.height)))
                        color: root.t.fg
                        font.family: EctoTheme.mono
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }
            }
        }

        // ── Pointing: one line at the top says how to choose ──
        EctoRow {
            id: hintRow
            objectName: "hintRow"
            visible: !root.framed && !root.recording && SelectionEditor.dragLocation === SelectionEditor.None && root.mode === ""
                && Geometry.rectIntersects(screensRectItem.mouseScreen, root.viewportRect)
            x: Math.round(screensRectItem.mouseScreen.x + screensRectItem.mouseScreen.width / 2 - width / 2)
            y: screensRectItem.mouseScreen.y + 16
            EctoLabel {
                Kirigami.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    source: "crosshairs"
                    color: root.t.fgMuted
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: i18nc("@info how to choose what to capture", "Click a window or the desktop, or drag an area")
                    color: root.t.fg
                    font.family: EctoTheme.sans
                    font.pixelSize: 13
                }
            }
            EctoKey { text: i18nc("@info key name", "Enter") }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 6
                rightPadding: 12
                text: i18nc("@info what Enter does while pointing", "all screens")
                color: root.t.fgMuted
                font.family: EctoTheme.sans
                font.pixelSize: 13
            }
            EctoKey { text: i18nc("@info key name", "Esc") }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 6
                text: i18nc("@info what Esc does", "cancel")
                color: root.t.fgMuted
                font.family: EctoTheme.sans
                font.pixelSize: 13
            }
            EctoSeparator {}
            EctoButton {
                id: hintGear
                iconSource: "qrc:/icons/ectolens-gear.svg"
                iconOnly: true
                text: i18nc("@action:button", "Options")
                menuOpen: optionsMenu.opened
                onClicked: optionsMenu.toggleAt(hintGear, "below")
            }
        }

        // ── Pointing: the label above the window under the pointer ──
        EctoRow {
            id: pointLabel
            visible: hoverTarget.visible && root.mode === ""
            readonly property rect bounds: screensRectItem.screenAt(SelectionEditor.hoverRect.x + SelectionEditor.hoverRect.width / 2, SelectionEditor.hoverRect.y + 1)
            x: screensRectItem.rowX(width, SelectionEditor.hoverRect, bounds)
            y: {
                if (hoverTarget.isDesktop) {
                    // the whole screen: under the hint line
                    return hintRow.y + hintRow.height + 8
                }
                let y = screensRectItem.topY(height, SelectionEditor.hoverRect, bounds)
                // keep clear of the hint line
                if (hintRow.visible && y < hintRow.y + hintRow.height + 6 && x < hintRow.x + hintRow.width && x + width > hintRow.x) {
                    y = hintRow.y + hintRow.height + 8
                }
                return y
            }
            FrameLabel {
                iconName: hoverTarget.isDesktop ? "monitor" : SelectionEditor.hoverIconName
                pixelSize: Geometry.rawSize(Qt.size(SelectionEditor.hoverRect.width, SelectionEditor.hoverRect.height), contextWindow.devicePixelRatio)
                // a click takes the window and opens its size menu
                onClicked: {
                    if (SelectionEditor.takeHovered()) {
                        sizeMenu.openAt(frameLabel, "below")
                    }
                }
            }
        }

        // ── Framed: above the frame, the label and the marking tools ──
        EctoRow {
            id: topRow
            objectName: "topRow"
            visible: root.framed && screensRectItem.showRows && annotationsLoader.visible
            x: screensRectItem.rowX(width, screensRectItem.frame, screensRectItem.topBounds)
            y: screensRectItem.topY(height, screensRectItem.frame, screensRectItem.topBounds)
            FrameLabel {
                id: frameLabel
                objectName: "frameLabel"
                iconName: SelectionEditor.frameIsWindow ? SelectionEditor.frameIconName
                    : root.isWholeScreen(SelectionEditor.selection.rect) ? "monitor" : ""
                pixelSize: root.rawSelectionSize
                menuOpen: sizeMenu.opened
                onClicked: sizeMenu.toggleAt(frameLabel, topRow.y < screensRectItem.frame.y ? "above" : "below")
            }
            EctoSeparator {}
            EctoTools {
                id: overlayTools
                objectName: "overlayTools"
                anchors.verticalCenter: parent.verticalCenter
                inOverlay: true
            }
        }

        // ── Framed: the tool in use's options, in a row above the tool row, lined up with the button of
        // the tool that called them: for a tool on the row's
        // right half its last option stands right above the tool, the row reaching left; on the left
        // half its first option does, the row reaching right (the rule EctoMenu lines menus up by).
        // Below the tool row when there is no room above it; kept on the screen. ──
        EctoRow {
            id: optionsRow
            objectName: "optionsRow"
            followsTool: true
            readonly property rect bounds: screensRectItem.topBounds
            // the calling tool's button, on the screens
            readonly property rect toolRect: {
                // positions it follows (mapToItem alone does not)
                const follows = [topRow.x, topRow.y, topRow.width, overlayTools.x, overlayTools.currentTool?.x ?? 0]
                const t = overlayTools.currentTool
                if (!t || follows.length === 0) {
                    return Qt.rect(topRow.x + topRow.width / 2, topRow.y, 0, topRow.height)
                }
                const p = t.mapToItem(screensRectItem, 0, 0)
                return Qt.rect(p.x, p.y, t.width, t.height)
            }
            readonly property bool fromRight: toolRect.x + toolRect.width / 2 > topRow.x + topRow.width / 2
            // close to the tool row it belongs to (the gap menus keep from their button)
            readonly property real gap: EctoTheme.rowGap
            readonly property real aboveY: topRow.y - gap - height
            readonly property bool above: aboveY >= bounds.y + root.edgeMargin
            visible: topRow.visible && toolOptions.hasOptions
            x: Math.round(Math.max(bounds.x + root.edgeMargin,
                                   Math.min(fromRight ? toolRect.x + toolRect.width + sidePadding - width : toolRect.x - sidePadding,
                                            bounds.x + bounds.width - root.edgeMargin - width)))
            y: Math.round(above ? aboveY : topRow.y + topRow.height + gap)
            EctoToolOptions {
                id: toolOptions
                anchors.verticalCenter: parent.verticalCenter
                menuSide: optionsRow.above ? "above" : "below"
            }
            // A watch (clicking the text box or number tool sometimes did not bring up the bar;
            // not reproduced off-screen): when a tool with options is picked and
            // the row is hidden, or sits on no screen, say what it saw in Ectolens's log.
            Connections {
                target: toolOptions.tool
                function onTypeChanged() {
                    Qt.callLater(optionsRow.checkShown)
                }
            }
            function checkShown() {
                if (!root.framed || !toolOptions.hasOptions || contextWindow !== root.frameWindow()) {
                    return
                }
                const r = Qt.rect(x, y, width, height)
                const onScreen = Qt.application.screens.some(s => r.x < s.virtualX + s.width && r.x + r.width > s.virtualX
                                                             && r.y < s.virtualY + s.height && r.y + r.height > s.virtualY)
                if (!visible || !onScreen) {
                    console.warn("ECTOLENS-OPTIONS-MISSING tool", toolOptions.tool.type, "visible", visible, "onScreen", onScreen,
                                 "row", r, "bounds", bounds, "topRow", topRow.visible, Qt.rect(topRow.x, topRow.y, topRow.width, topRow.height),
                                 "options", toolOptions.options, "viewport", root.viewportRect)
                }
            }
        }

        // ── Framed: below the frame, what to make of it ──
        EctoRow {
            id: bottomRow
            objectName: "bottomRow"
            visible: root.framed && screensRectItem.showRows
            x: screensRectItem.rowX(width, screensRectItem.frame, screensRectItem.bottomBounds)
            y: screensRectItem.bottomY(height, screensRectItem.frame, screensRectItem.bottomBounds)
            // An AI agent asks for this (Settings › Model Context Protocol (MCP), ectolens-mcp): who and why, then
            // Send or Don't Send. Nothing reaches the agent until Send.
            EctoLabel {
                visible: SpectacleCore.agentAsking && !agentCard.visible
                Kirigami.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    // a question in a speech bubble: someone asks (Breeze's one-color action icon)
                    source: "help-contextual"
                    color: root.t.fgMuted
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: SpectacleCore.agentKind === "record"
                        ? i18nc("@info %1 an AI agent's name", "%1 asks to record this", SpectacleCore.agentClient)
                        : i18nc("@info %1 an AI agent's name", "%1 asks for this picture", SpectacleCore.agentClient)
                    color: root.t.fg
                    font.family: EctoTheme.sans
                    font.pixelSize: 13
                }
                Text {
                    visible: SpectacleCore.agentReason.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, 320)
                    elide: Text.ElideRight
                    text: SpectacleCore.agentReason
                    color: root.t.fgMuted
                    font.family: EctoTheme.sans
                    font.pixelSize: 13
                }
            }
            EctoButton {
                id: agentSendButton
                objectName: "agentSend"
                visible: SpectacleCore.agentAsking && !agentCard.visible
                iconName: SpectacleCore.agentKind === "record" ? "media-record" : "document-send"
                text: SpectacleCore.agentKind === "record" ? i18nc("@action record for an AI agent", "Record") : i18nc("@action give the picture to an AI agent", "Send")
                shortcutText: i18nc("@info key name", "Enter")
                toolTipText: SpectacleCore.agentKind === "record"
                    ? i18nc("@info:tooltip %1 an AI agent's name", "Record this for %1", SpectacleCore.agentClient)
                    : i18nc("@info:tooltip %1 an AI agent's name", "Give this picture to %1", SpectacleCore.agentClient)
                // not while Hide Private Info reads the frame: the picture would leave unblurred
                enabled: !CaptureTools.hidingPrivateInfo
                onClicked: SpectacleCore.agentSend()
            }
            EctoButton {
                objectName: "agentRefuse"
                visible: SpectacleCore.agentAsking && !agentCard.visible
                iconName: "dialog-cancel"
                text: i18nc("@action refuse an AI agent's request", "Don't Send")
                shortcutText: i18nc("@info key name", "Esc")
                onClicked: SpectacleCore.agentRefuse()
            }
            EctoSplitButton {
                id: captureSplit
                objectName: "capture"
                visible: !SpectacleCore.videoMode && !SpectacleCore.agentAsking
                iconName: "camera-photo"
                text: i18nc("@action:button finish the capture", "Capture")
                shortcutText: i18nc("@info key name", "Enter")
                toolTipText: i18nc("@info:tooltip", "Capture what is framed")
                arrowToolTip: i18nc("@info:tooltip", "What happens after a capture")
                menuOpen: afterMenu.opened || shareChooser.opened
                // not while Hide Private Info reads the frame: the picture would leave unblurred
                button.enabled: !CaptureTools.hidingPrivateInfo
                onClicked: SelectionEditor.acceptSelection()
                onArrowClicked: afterMenu.toggleAt(captureSplit, "above")
            }
            EctoSplitButton {
                id: recordSplit
                objectName: "record"
                visible: (SpectacleCore.videoPlatform.supportedRecordingModes & VideoPlatform.Region) && !SpectacleCore.agentAsking
                iconName: "media-record"
                text: i18nc("@action start recording what is framed", "Record")
                shortcutText: CaptureTools.keyText("R")
                toolTipText: i18nc("@info:tooltip", "Record what is framed")
                arrowToolTip: i18nc("@info:tooltip", "What goes into the recording")
                menuOpen: recordPanel.opened
                onClicked: {
                    if (SpectacleCore.videoMode) {
                        SelectionEditor.acceptSelection()
                    } else {
                        SpectacleCore.recordSelection()
                    }
                }
                onArrowClicked: recordPanel.toggleAt(recordSplit, "above")
            }
            // only between groups: nothing before it while an agent's dialog is up
            EctoSeparator { visible: (extractSplit.visible || pictureTools.visible) && (captureSplit.visible || recordSplit.visible || agentSendButton.visible) }
            EctoSplitButton {
                id: extractSplit
                objectName: "extractText"
                visible: !SpectacleCore.videoMode && SpectacleCore.ocrAvailable
                iconName: "document-scan"
                text: i18nc("@action", "Extract Text")
                toolTipText: i18nc("@info:tooltip", "Copy the words in the frame")
                arrowToolTip: i18nc("@info:tooltip", "Language")
                button.enabled: SpectacleCore.ocrStatus !== 1
                menuOpen: languageMenu.opened
                onClicked: SpectacleCore.startOcrExtraction()
                onArrowClicked: languageMenu.toggleAt(extractSplit, "above")
            }
            Row {
                id: pictureTools
                visible: !SpectacleCore.videoMode
                spacing: 2
                EctoSplitButton {
                    id: qrSplit
                    objectName: "openQr"
                    visible: CaptureTools.qrText.length > 0 && Geometry.rectIntersects(CaptureTools.qrRect, SelectionEditor.selection.rect)
                    iconName: "view-barcode-qr"
                    text: CaptureTools.qrIsLink ? i18nc("@action", "Open QR Code") : i18nc("@action", "Copy QR Code")
                    toolTipText: CaptureTools.qrText
                    arrowToolTip: i18nc("@info:tooltip", "What the code says")
                    menuOpen: qrMenu.opened
                    onClicked: {
                        CaptureTools.openQr()
                        if (!CaptureTools.qrIsLink) root.showToast(i18nc("@info", "Copied what the QR code says"))
                    }
                    onArrowClicked: qrMenu.toggleAt(qrSplit, "above")
                }
                EctoButton {
                    objectName: "hidePrivateInfo"
                    iconName: "view-hidden"
                    iconOnly: true
                    text: i18nc("@action", "Hide Private Info")
                    toolTipText: i18nc("@info:tooltip", "Hide Private Info: blur email addresses, keys, tokens and home-folder paths")
                    enabled: SpectacleCore.ocrAvailable && !CaptureTools.hidingPrivateInfo
                    current: CaptureTools.hidingPrivateInfo
                    onClicked: CaptureTools.hidePrivateInfo(SelectionEditor.selection.rect)
                }
                EctoButton {
                    objectName: "pin"
                    iconName: "window-pin"
                    iconOnly: true
                    text: i18nc("@action", "Pin")
                    toolTipText: i18nc("@info:tooltip", "Pin: keep the capture floating above your windows")
                    onClicked: CaptureTools.pin(SelectionEditor.selection.rect)
                }
                EctoButton {
                    objectName: "pickColor"
                    iconName: "color-picker"
                    iconOnly: true
                    text: i18nc("@action", "Pick Color")
                    toolTipText: i18nc("@info:tooltip", "Pick Color: click a pixel to copy its color")
                    current: root.mode === "color"
                    onClicked: CaptureTools.overlayMode = root.mode === "color" ? "" : "color"
                }
                EctoButton {
                    objectName: "measure"
                    iconName: "measure"
                    iconOnly: true
                    text: i18nc("@action", "Measure")
                    toolTipText: i18nc("@info:tooltip", "Measure: drag between two points; Shift keeps it straight")
                    current: root.mode === "measure"
                    onClicked: {
                        root.measureStart = Qt.point(-1, -1)
                        CaptureTools.overlayMode = root.mode === "measure" ? "" : "measure"
                    }
                }
            }
            EctoSeparator {}
            EctoButton {
                id: gear
                objectName: "gear"
                iconSource: "qrc:/icons/ectolens-gear.svg"
                iconOnly: true
                text: i18nc("@action:button", "Options")
                menuOpen: optionsMenu.opened
                onClicked: optionsMenu.toggleAt(gear, "above")
            }
        }

        // ── An AI agent asks: a dialog in the middle of the frame's screen (the
        // request in the bottom row was easy to miss)
        // Who asks and why, a live preview of exactly what would be sent (blur included), and
        // Send (Enter), Adjust First (the frame and the tools, then the bottom row's Send) and
        // Don't Send (Esc). ──
        // Everything but the dialog recedes while it asks, on every screen (a consent prompt's dim, as
        // Windows' User Account Control does it): the frame stays readable and brighter than the rest,
        // the dialog is the clearest thing on screen. It comes with the dialog, after the frame has
        // shown, and Adjust First fades it out. (It dims more when an agent is making the call.)
        Rectangle {
            id: agentScrim
            objectName: "agentScrim"
            readonly property bool wanted: root.agentModal
            property bool shown: false
            onWantedChanged: {
                if (wanted) {
                    scrimDelay.restart()
                } else {
                    scrimDelay.stop()
                    shown = false
                }
            }
            Timer {
                id: scrimDelay
                interval: 650 // with the dialog's pop
                onTriggered: agentScrim.shown = true
            }
            z: 85
            x: root.viewportRect.x
            y: root.viewportRect.y
            width: root.width
            height: root.height
            color: "transparent"
            visible: opacity > 0
            opacity: shown ? 0.5 : 0
            // the frame stays bright: the window the agent asks about is what the person judges, so
            // only the screen around it dims (the captured window is
            // not darkened by the shadow); four bands around a hole the frame line's size
            // (the line sits just outside the frame, so it stays bright with what it marks)
            readonly property color shade: Settings.useLightMaskColor ? "white" : "black"
            readonly property real holeX: Math.max(0, Math.min(width, selectionRectangle.x))
            readonly property real holeY: Math.max(0, Math.min(height, selectionRectangle.y))
            readonly property real holeRight: Math.max(holeX, Math.min(width, selectionRectangle.x + selectionRectangle.width))
            readonly property real holeBottom: Math.max(holeY, Math.min(height, selectionRectangle.y + selectionRectangle.height))
            Rectangle { color: agentScrim.shade; width: parent.width; height: agentScrim.holeY }
            Rectangle { color: agentScrim.shade; y: agentScrim.holeBottom; width: parent.width; height: parent.height - y }
            Rectangle { color: agentScrim.shade; y: agentScrim.holeY; width: agentScrim.holeX; height: agentScrim.holeBottom - agentScrim.holeY }
            Rectangle { color: agentScrim.shade; x: agentScrim.holeRight; y: agentScrim.holeY; width: parent.width - x; height: agentScrim.holeBottom - agentScrim.holeY }
            Behavior on opacity {
                NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.OutCubic }
            }
            // a modal's backdrop: the dimmed rows and frame take no pointer while the dialog asks, so
            // nothing that looks asleep answers a click; a press here nudges the dialog instead
            // (Bootstrap 5's static backdrop), since only its buttons answer the agent
            MouseArea {
                anchors.fill: parent
                enabled: agentScrim.wanted
                hoverEnabled: true
                acceptedButtons: Qt.AllButtons
                onPressed: agentCard.nudge()
                onWheel: wheel => wheel.accepted = true
            }
        }
        Kirigami.ShadowedRectangle {
            id: agentCard
            objectName: "agentCard"
            readonly property rect bounds: screensRectItem.screenAt(screensRectItem.frame.x + screensRectItem.frame.width / 2,
                                                                     screensRectItem.frame.y + screensRectItem.frame.height / 2)
            readonly property bool wanted: root.agentModal && Geometry.rectIntersects(bounds, root.viewportRect)
            // the frame shows first, then the dialog pops up over it
            property bool popped: false
            onWantedChanged: {
                popped = false
                if (wanted) {
                    popDelay.restart()
                }
            }
            Timer {
                id: popDelay
                interval: 650
                onTriggered: agentCard.popped = true
            }
            visible: wanted
            enabled: popped
            opacity: popped ? 1 : 0
            scale: popped ? 1 : 0.94
            Behavior on opacity {
                NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.OutCubic }
            }
            Behavior on scale {
                NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.OutBack; easing.overshoot: 1.2 }
            }
            // a press outside the dialog: it swells a little and settles, to say it waits for an answer
            // (Bootstrap 5's static backdrop scales its dialog to 1.02)
            function nudge() {
                if (visible && popped) {
                    nudges += 1
                    nudgeAnimation.restart()
                }
            }
            property int nudges: 0 // the agent run reads it
            property real nudgeScale: 1
            transform: Scale {
                origin.x: agentCard.width / 2
                origin.y: agentCard.height / 2
                xScale: agentCard.nudgeScale
                yScale: agentCard.nudgeScale
            }
            SequentialAnimation {
                id: nudgeAnimation
                NumberAnimation { target: agentCard; property: "nudgeScale"; to: 1.02; duration: 120; easing.type: Easing.OutCubic }
                NumberAnimation { target: agentCard; property: "nudgeScale"; to: 1; duration: 180; easing.type: Easing.OutCubic }
            }
            z: 90
            width: 456
            height: cardColumn.implicitHeight + 2 * 20
            x: Math.round(bounds.x + (bounds.width - width) / 2)
            y: Math.round(bounds.y + (bounds.height - height) / 2)
            // raised, so lighter than the screen it sits on: a dark look shows height by lightness
            // (Material's dark theme), in the family's lift gray with its stronger border; a light KDE
            // color scheme keeps its window color, its shadow showing the height
            color: root.t.light ? root.t.bg : root.t.bgLift
            radius: EctoTheme.menuRadius
            border.width: 1
            border.color: root.t.borderStrong
            shadow.size: 48
            shadow.yOffset: 18
            shadow.color: root.t.shadow
            // takes the pointer, so a click on it never reaches the frame below
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
            }
            Column {
                id: cardColumn
                x: 20
                y: 20
                width: parent.width - 40
                spacing: 12
                Row {
                    spacing: EctoTheme.leadGap
                    Kirigami.Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20
                        height: 20
                        source: "help-contextual"
                        color: root.t.fg
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: cardColumn.width - 30
                        wrapMode: Text.Wrap
                        text: SpectacleCore.agentKind === "record"
                            ? i18nc("@title %1 an AI agent's name", "%1 asks to record this", SpectacleCore.agentClient)
                            : i18nc("@title %1 an AI agent's name", "%1 asks for this picture", SpectacleCore.agentClient)
                        color: root.t.fg
                        font.family: EctoTheme.sans
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                }
                Text {
                    visible: text.length > 0
                    width: cardColumn.width
                    wrapMode: Text.Wrap
                    text: SpectacleCore.agentReason
                    color: root.t.fgMuted
                    font.family: EctoTheme.sans
                    font.pixelSize: 13
                }
                // what would be sent, as it is now: the frozen picture and its marks inside the frame
                Rectangle {
                    width: cardColumn.width
                    height: Math.round(Math.min(220, width * Math.max(1, screensRectItem.frame.height) / Math.max(1, screensRectItem.frame.width)))
                    radius: EctoTheme.menuItemRadius
                    // the picture in a dark well, as a viewer shows one
                    color: root.t.bg
                    border.width: 1
                    border.color: root.t.border
                    clip: true
                    ShaderEffectSource {
                        id: preview
                        readonly property real k: Math.min((parent.width - 2) / Math.max(1, screensRectItem.frame.width),
                                                           (parent.height - 2) / Math.max(1, screensRectItem.frame.height))
                        anchors.centerIn: parent
                        width: Math.round(screensRectItem.frame.width * k)
                        height: Math.round(screensRectItem.frame.height * k)
                        sourceItem: annotationsLoader
                        sourceRect: Qt.rect(screensRectItem.frame.x - root.viewportRect.x, screensRectItem.frame.y - root.viewportRect.y,
                                            screensRectItem.frame.width, screensRectItem.frame.height)
                        live: agentCard.visible
                        hideSource: false
                        smooth: true
                    }
                }
                // a recording is made from the live screen, which the overlay's blurs never reach
                Text {
                    objectName: "agentCardNote"
                    visible: SpectacleCore.agentKind === "record"
                    width: cardColumn.width
                    wrapMode: Text.Wrap
                    text: i18nc("@info", "Recordings are not blurred: the agent gets what the screen shows.")
                    color: root.t.fgMuted
                    font.family: EctoTheme.sans
                    font.pixelSize: 12
                }
                // a picture: blur what looks private, or not, for this request.
                // It starts as Settings › Model Context Protocol (MCP) says; unchecked, the blurs
                // come off the preview and the picture, and the words go as they are.
                Column {
                    visible: SpectacleCore.agentKind !== "record"
                    width: cardColumn.width
                    spacing: 2
                    EctoCheck {
                        objectName: "agentCardBlur"
                        text: i18nc("@option:check", "Blur private information")
                        // the request holds the choice; a click asks it to change, so the box
                        // follows each new request's start
                        checkable: false
                        checked: SpectacleCore.agentBlur
                        onClicked: SpectacleCore.agentBlur = !SpectacleCore.agentBlur
                    }
                    Text {
                        objectName: "agentCardBlurNote"
                        readonly property int found: CaptureTools.privateInfoFound
                        x: EctoTheme.leadSize + EctoTheme.leadGap
                        width: parent.width - x
                        wrapMode: Text.Wrap
                        text: CaptureTools.hidingPrivateInfo ? i18nc("@info", "Reading the frame…")
                            : SpectacleCore.agentBlur
                                ? (found > 0 ? i18ncp("@info", "%1 found and blurred in the preview.", "%1 found and blurred in the preview.", found)
                                   : found === 0 ? i18nc("@info", "Nothing private found.")
                                   : CaptureTools.hideResult)
                                : (found > 0 ? i18ncp("@info", "%1 found. The agent gets it as it is.", "%1 found. The agent gets them as they are.", found) : "")
                        visible: text.length > 0
                        color: root.t.fgMuted
                        font.family: EctoTheme.sans
                        font.pixelSize: 12
                    }
                }
                Row {
                    spacing: EctoTheme.rowGap
                    layoutDirection: Qt.RightToLeft
                    width: cardColumn.width
                    EctoButton {
                        objectName: "agentCardSend"
                        variant: "primary"
                        iconName: SpectacleCore.agentKind === "record" ? "media-record" : "document-send"
                        text: SpectacleCore.agentKind === "record" ? i18nc("@action record for an AI agent", "Record") : i18nc("@action give the picture to an AI agent", "Send")
                        shortcutText: i18nc("@info key name", "Enter")
                        enabled: !CaptureTools.hidingPrivateInfo
                        onClicked: SpectacleCore.agentSend()
                    }
                    EctoButton {
                        objectName: "agentCardAdjust"
                        variant: "outline"
                        iconName: "transform-crop"
                        text: i18nc("@action change the frame or mark it up before sending", "Adjust First")
                        toolTipText: i18nc("@info:tooltip", "Move or resize the frame, or mark it up, then send from the row below it")
                        onClicked: SpectacleCore.agentAdjust()
                    }
                    EctoButton {
                        objectName: "agentCardRefuse"
                        variant: "outline"
                        iconName: "dialog-cancel"
                        text: i18nc("@action refuse an AI agent's request", "Don't Send")
                        shortcutText: i18nc("@info key name", "Esc")
                        onClicked: SpectacleCore.agentRefuse()
                    }
                }
            }
        }

        // a short message over the bottom row (a copied color, what Hide Private Info found)
        EctoRow {
            id: toast
            property string text: ""
            // the agent's dialog says it itself
            visible: toastTimer.running && text.length > 0 && !root.recording && !agentCard.visible
            x: bottomRow.visible ? Math.round(bottomRow.x + bottomRow.width / 2 - width / 2) : Math.round(screensRectItem.mouseScreen.x + screensRectItem.mouseScreen.width / 2 - width / 2)
            y: bottomRow.visible ? bottomRow.y - height - 8 : screensRectItem.mouseScreen.y + 60
            z: 5
            EctoLabel {
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: toast.text
                    color: root.t.fg
                    font.family: EctoTheme.sans
                    font.pixelSize: 13
                }
            }
            Timer {
                id: toastTimer
                interval: 2600
            }
        }
        Connections {
            target: CaptureTools
            function onHidingPrivateInfoChanged() {
                if (!CaptureTools.hidingPrivateInfo && CaptureTools.hideResult.length > 0 && bottomRow.visible
                    && Geometry.rectIntersects(Qt.rect(bottomRow.x, bottomRow.y, bottomRow.width, bottomRow.height), root.viewportRect)) {
                    root.showToast(CaptureTools.hideResult)
                }
            }
        }
    }

    // ── Menus ──
    SizeMenu {
        id: sizeMenu
        current: root.rawSelectionSize
        isScreen: root.isWholeScreen(SelectionEditor.selection.rect)
    }
    AfterCaptureMenu { id: afterMenu }
    RecordPanel { id: recordPanel }
    OverlayOptionsMenu { id: optionsMenu }
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
        id: qrMenu
        minimumWidth: 260
        Text {
            readonly property bool fillsWidth: true
            leftPadding: 10
            rightPadding: 10
            topPadding: 8
            bottomPadding: 6
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 4
            elide: Text.ElideRight
            text: CaptureTools.qrText
            color: root.t.fgMuted
            font.family: EctoTheme.mono
            font.pixelSize: 12
        }
        EctoMenuSeparator {}
        EctoMenuItem {
            visible: CaptureTools.qrIsLink
            iconName: "internet-services"
            text: i18nc("@action", "Open")
            onClicked: CaptureTools.openQr()
        }
        EctoMenuItem {
            iconName: "edit-copy"
            text: i18nc("@action", "Copy")
            onClicked: {
                CaptureTools.copyQr()
                root.showToast(i18nc("@info", "Copied what the QR code says"))
            }
        }
    }
    // Share set to ask each time: pick the service over Capture before it finishes.
    EctoMenu {
        id: shareChooser
        minimumWidth: 240
        property bool picked: false
        onAboutToShow: picked = false
        onClosed: if (!picked) SpectacleCore.captureSharingWith("")
        EctoMenuHeader { text: i18nc("@title:menu", "Share to") }
        Repeater {
            model: shareChooser.opened ? CaptureTools.shareTargets() : []
            EctoMenuItem {
                required property var modelData
                iconName: modelData.icon
                text: modelData.name
                onClicked: {
                    shareChooser.picked = true
                    SpectacleCore.captureSharingWith(modelData.id)
                }
            }
        }
        EctoMenuSeparator {}
        EctoMenuItem {
            text: i18nc("@action:inmenu", "Don't share this time")
            onClicked: {
                shareChooser.picked = true
                SpectacleCore.captureSharingWith("")
            }
        }
    }
    Connections {
        target: SpectacleCore
        function onShareTargetNeeded() {
            // One screen asks: the one holding the middle of the row, or with nothing framed
            // (Enter takes all screens), the one with the pointer, under the hint line.
            if (root.framed && bottomRow.visible) {
                const cx = bottomRow.x + bottomRow.width / 2 + screensRectItem.x
                const cy = bottomRow.y + bottomRow.height / 2 + screensRectItem.y
                if (cx >= 0 && cx < root.width && cy >= 0 && cy < root.height) {
                    shareChooser.openAt(captureSplit, "above")
                }
            } else if (hintRow.visible) {
                shareChooser.openAt(hintRow, "below")
            }
        }
    }

    // ── Recording: the row keeps its place under the frame, centered on it, and holds the
    // time, a Mic marker, Pause and Stop. Only this row takes clicks while recording;
    // everything else goes through to the apps. ──
    EctoRow {
        id: recordingBar
        objectName: "recordingBar"
        readonly property bool paused: SpectacleCore.videoPlatform.recordingState === VideoPlatform.Paused
        readonly property int dropped: SpectacleCore.videoPlatform.droppedFrames
        readonly property bool micOn: Settings.videoRecordMicrophone && CaptureTools.formatHasSound(Settings.preferredVideoFormat)
        // the recorded area, in this window's coordinates
        readonly property rect area: {
            const r = SelectionEditor.selection.empty ? SelectionEditor.screensRect : SelectionEditor.selection.rect
            return Qt.rect(r.x - root.viewportRect.x, r.y - root.viewportRect.y, r.width, r.height)
        }
        readonly property bool fitsBelow: area.y + area.height + root.frameGap + height <= root.height
        readonly property bool fitsAbove: area.y - root.frameGap - height >= 0
        /// no room outside the frame (a frame filling the screen): the bar sits inside it, at its
        /// bottom. KWin leaves every window of the program that records out of its recording
        /// (screencastmanager.cpp, getPid), so the bar shows on screen and never in the file.
        readonly property bool inside: !fitsBelow && !fitsAbove
        readonly property real insideBottom: Math.min(area.y + area.height, root.height)
        readonly property real centerX: area.x + area.width / 2
        // on the screen holding the middle of the frame's edge it sits by (one bar, on one screen)
        readonly property bool onThisScreen: centerX >= 0 && centerX < root.width
            && (fitsBelow ? area.y + area.height <= root.height
                : fitsAbove ? area.y >= 0
                : insideBottom > 0 && area.y < root.height)
        property double startedAt: 0

        z: 70
        // below the frame, else above it, else inside it at its bottom (on
        // screen and in the frame, but not recorded; it was hidden)
        visible: root.recording && onThisScreen
        // centered on the recorded area, like the rows before it (Stop placed
        // where Record was pulled the bar to the frame's left corner)
        x: Math.round(Math.max(root.edgeMargin, Math.min(centerX - width / 2, root.width - width - root.edgeMargin)))
        y: Math.round(fitsBelow ? area.y + area.height + root.frameGap
            : fitsAbove ? area.y - root.frameGap - height
            : insideBottom - root.frameGap - height)
        onVisibleChanged: updateInput()
        onXChanged: updateInput()
        onYChanged: updateInput()
        onWidthChanged: updateInput()
        onHeightChanged: updateInput()
        Connections {
            target: root
            function onRecordingChanged() {
                if (root.recording) {
                    recordingBar.startedAt = Date.now()
                }
                recordingBar.updateInput()
            }
            function onFinishingChanged() {
                recordingBar.updateInput()
            }
        }
        // Only while recording does the window narrow its input to this row;
        // otherwise the whole overlay takes input (framing, rows, menus).
        function updateInput() {
            if (root.finishing) {
                contextWindow.setInputRect(Qt.rect(0, 0, 0, 0))
            } else if (!root.recording) {
                contextWindow.clearInputRect()
            } else {
                contextWindow.setInputRect(visible ? Qt.rect(x, y, width, height) : Qt.rect(0, 0, 0, 0))
            }
        }

        // the dot and the time, standing like a button's icon and words
        EctoLabel {
            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                Rectangle {
                    anchors.centerIn: parent
                    width: 9
                    height: 9
                    radius: 4.5
                    color: recordingBar.paused ? root.t.fgSubtle : root.t.error
                    Rectangle {
                        anchors.centerIn: parent
                        width: 15
                        height: 15
                        radius: 7.5
                        color: "transparent"
                        border.width: 3
                        border.color: Qt.alpha(root.t.error, 0.22)
                        visible: !recordingBar.paused
                    }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    let t = recordingBar.paused ? i18nc("@info recording time while paused", "Paused at %1", SpectacleCore.recordedTime) : SpectacleCore.recordedTime
                    if (recordingBar.dropped > 0) {
                        t += " · " + i18ncp("@info", "%1 frame dropped", "%1 frames dropped", recordingBar.dropped)
                    }
                    return t
                }
                color: recordingBar.dropped > 0 ? root.t.warning : root.t.fg
                font.family: EctoTheme.mono
                font.pixelSize: 13
                font.weight: Font.Medium
            }
        }
        Rectangle {
            visible: recordingBar.micOn
            anchors.verticalCenter: parent.verticalCenter
            width: micRow.implicitWidth + 12
            height: 22
            radius: 4
            color: root.t.bgLift
            Row {
                id: micRow
                anchors.centerIn: parent
                spacing: 5
                Kirigami.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    source: "mic-on"
                    color: root.t.fgMuted
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: i18nc("@info the microphone is recording", "Mic")
                    color: root.t.fgMuted
                    font.family: EctoTheme.sans
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }
        }
        EctoSeparator {}
        EctoButton {
            iconName: recordingBar.paused ? "media-playback-start" : "media-playback-pause"
            text: recordingBar.paused ? i18nc("@action:button", "Resume") : i18nc("@action:button", "Pause")
            onClicked: recordingBar.paused ? SpectacleCore.videoPlatform.resumeRecording() : SpectacleCore.videoPlatform.pauseRecording()
        }
        EctoButton {
            id: stopButton
            objectName: "stop"
            iconName: "media-playback-stop"
            text: i18nc("@action:button stop recording", "Stop")
            toolTipText: i18nc("@info:tooltip", "Finish the recording")
            shortcutText: CaptureTools.globalShortcut("open")
            // A double click on Record must not end the recording it just started.
            onClicked: if (Date.now() - recordingBar.startedAt > 500) SpectacleCore.finishRecording()
        }
    }

    // Esc leaves Pick Color and Measure (and only then; otherwise it cancels, as always).
    Shortcut {
        sequence: "Escape"
        enabled: root.mode !== ""
        onActivated: CaptureTools.overlayMode = ""
        // every screen's overlay has this shortcut, which Qt can count as ambiguous
        onActivatedAmbiguously: CaptureTools.overlayMode = ""
    }

    // R records what is framed, unless a marking tool is in use.
    Shortcut {
        sequence: "R"
        enabled: !SpectacleCore.videoMode && !root.recording && root.mode === "" && (root.document?.tool.isNoTool ?? true) && !SpectacleCore.agentAsking
        onActivated: {
            SpectacleCore.recordSelection()
        }
    }

    // FIXME: This shortcut only exists here because spectacle interprets "Ctrl+Shift+,"
    // as "Ctrl+Shift+<" for some reason unless we use a QML Shortcut.
    Shortcut {
        // not while an agent's dialog asks: Settings would put the overlay behind it
        enabled: !root.agentModal
        sequences: [StandardKey.Preferences]
        onActivated: contextWindow.showPreferencesDialog()
    }

    Connections {
        target: contextWindow
        function onVisibilityChanged(visibility) {
            if (visibility !== Window.Hidden && visibility !== Window.Minimized) {
                contextWindow.raise()
                if (root.containsMouse) {
                    contextWindow.requestActivate()
                }
            }
        }
    }
}
