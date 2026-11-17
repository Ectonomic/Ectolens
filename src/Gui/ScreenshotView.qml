/*
 * SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC
import org.kde.kirigami as Kirigami
import org.kde.spectacle.private
import org.kde.kquickimageeditor.ectolens

// Can't use ScrollView because ScrollView prevents mice from being able to drag the view
EmptyPage {
    id: root
    objectName: "screenshotView"

    // Ectolens: Fit shows the whole picture with room around it, and never enlarges it past its size.
    readonly property real fitZoom: Math.min(1, (flickable.width - 48) / SpectacleCore.annotationDocument.canvasRect.width,
                                             (flickable.height - 48) / SpectacleCore.annotationDocument.canvasRect.height)
    readonly property real minZoom: Math.min(fitZoom, 1)
    readonly property real maxZoom: Math.max(minZoom, 8)
    readonly property real currentZoom: annotationEditor.scale
    /// Ectolens: Fit follows the window as it resizes; a zoom from the menu or the keys holds.
    property bool fitMode: true

    // Ectolens: Pick Color and Measure on the picture, as in the overlay ("color", "measure", or "").
    // Points are in the picture's own units from its top left (the picture item's coordinates).
    property string inspect: ""
    property point inspectPoint: Qt.point(-1, -1)
    property point inspectViewPoint: Qt.point(-1, -1) // the pointer, in this view's coordinates
    property point measureStart: Qt.point(-1, -1)
    property point measureEnd: Qt.point(-1, -1)
    readonly property real imageDpr: SpectacleCore.annotationDocument.imageDpr > 0 ? SpectacleCore.annotationDocument.imageDpr : 1
    readonly property color pickedColor: inspect === "color" && inspectPoint.x >= 0 ? CaptureTools.colorAt(toCanvas(inspectPoint)) : "transparent"
    /// a click with Pick Color copied this color
    signal colorCopied(string hex)
    onInspectChanged: {
        measureStart = Qt.point(-1, -1)
        measureEnd = Qt.point(-1, -1)
        inspectPoint = Qt.point(-1, -1)
        // the keys come here while picking or measuring, so Esc leaves them
        if (inspect !== "") {
            inspectArea.forceActiveFocus()
        }
    }
    function toCanvas(p) {
        const c = SpectacleCore.annotationDocument.canvasRect
        return Qt.point(c.x + p.x, c.y + p.y)
    }
    function toView(p) {
        return Qt.point(annotationEditor.x + p.x * annotationEditor.scale, annotationEditor.y + p.y * annotationEditor.scale)
    }
    function fit() {
        fitMode = true
        annotationEditor.scale = Qt.binding(() => root.fitZoom)
    }

    function zoomToPercent(percent, center = flickable.mapToItem(flickable.contentItem,
                                                                 flickable.width / 2,
                                                                 flickable.height / 2)) {
        fitMode = false
        const oldW = SpectacleCore.annotationDocument.canvasRect.width * annotationEditor.scale
        const oldH = SpectacleCore.annotationDocument.canvasRect.height * annotationEditor.scale
        annotationEditor.scale = Math.max(root.minZoom, Math.min(percent, root.maxZoom))
        const w = SpectacleCore.annotationDocument.canvasRect.width * annotationEditor.scale
        const h = SpectacleCore.annotationDocument.canvasRect.height * annotationEditor.scale

        if (center.x !== 0) {
            const min = flickable.width - Math.max(w, flickable.contentItem.width)
            const target = dprRound(flickable.contentItem.x + center.x + (-center.x * w / oldW))
            flickable.contentX = -Math.max(min, Math.min(target, 0)) // max
        }
        if (center.y !== 0) {
            const min = flickable.height - Math.max(h, flickable.contentItem.height)
            const target = dprRound(flickable.contentItem.y + center.y + (-center.y * w / oldW))
            flickable.contentY = -Math.max(min, Math.min(target, 0)) // max
        }
        flickable.returnToBounds()
    }

    function zoomIn(centerPos = flickable.mapToItem(flickable.contentItem,
                                                    flickable.width / 2,
                                                    flickable.height / 2)) {
        let stepSize = 1
        if (currentZoom < 1) {
            stepSize = 0.25
        } else if (currentZoom < 2) {
            stepSize = 0.5
        }
        zoomToPercent(currentZoom - (currentZoom % stepSize) + stepSize, centerPos)
    }

    function zoomOut(centerPos = flickable.mapToItem(flickable.contentItem,
                                                     flickable.width / 2,
                                                     flickable.height / 2)) {
        let inverseRemainder = 1 - (currentZoom % 1)
        let stepSize = 1
        if (currentZoom <= 1) {
            stepSize = 0.25
        } else if (currentZoom <= 2) {
            stepSize = 0.5
        }
        zoomToPercent(currentZoom + (inverseRemainder % stepSize) - stepSize, centerPos)
    }

    leftPadding: mirrored && verticalScrollBar.visible ? verticalScrollBar.width : 0
    rightPadding: !mirrored && verticalScrollBar.visible ? verticalScrollBar.width : 0
    bottomPadding: horizontalScrollBar.visible ? horizontalScrollBar.height : 0

    contentItem: Flickable {
        id: flickable

        clip: contextWindow.annotating
        interactive: contextWindow.annotating
            && SpectacleCore.annotationDocument.tool.type === AnnotationTool.NoTool
            && root.inspect === ""
        boundsBehavior: Flickable.StopAtBounds
        rebound: Transition {} // Instant transition. Null doesn't do this.
        contentWidth: Math.max(width, SpectacleCore.annotationDocument.canvasRect.width * annotationEditor.scale)
        contentHeight: Math.max(height, SpectacleCore.annotationDocument.canvasRect.height * annotationEditor.scale)

        Kirigami.WheelHandler {
            property point angleDelta: Qt.point(0,0)
            Binding on angleDelta { // reset when annotation tools are hidden
                value: Qt.point(0,0)
                when: !contextWindow.annotating
                restoreMode: Binding.RestoreNone
            }
            target: flickable
            keyNavigationEnabled: true
            scrollFlickableTarget: contextWindow.annotating
            horizontalStepSize: dprRound(Application.styleHints.wheelScrollLines * 20)
            verticalStepSize: dprRound(Application.styleHints.wheelScrollLines * 20)
            onWheel: wheel => {
                    if (wheel.modifiers & Qt.ControlModifier && scrollFlickableTarget) {
                    // apparently it's impossible to add points to each other directly in QML
                    angleDelta.x += wheel.angleDelta.x
                    angleDelta.y += wheel.angleDelta.y
                    if (angleDelta.x >= 120 || angleDelta.y >= 120) {
                        angleDelta = Qt.point(0,0)
                        const centerPos = flickable.mapToItem(flickable.contentItem, wheel.x, wheel.y)
                        root.zoomIn(centerPos)
                    } else if (angleDelta.x <= -120 || angleDelta.y <= -120) {
                        angleDelta = Qt.point(0,0)
                        const centerPos = flickable.mapToItem(flickable.contentItem, wheel.x, wheel.y)
                        root.zoomOut(centerPos)
                    }
                    wheel.accepted = true
                }
            }
        }

        PinchHandler {
            id: pinchHandler
            acceptedButtons: Qt.LeftButton
            target: null
            // No 2 finger drag because it's difficult to make without the
            // viewport moving in janky ways. The viewport still moves in janky
            // ways near edges, but it's not as bad as with 2 finger drag.
            onScaleChanged: (delta) => {
                root.zoomToPercent(root.currentZoom * delta)
            }
        }

        QQC.ScrollBar.vertical: QQC.ScrollBar {
            id: verticalScrollBar
            parent: root
            z: 1
            anchors.right: parent.right
            y: root.topPadding
            height: root.availableHeight
            active: horizontalScrollBar.active
        }
        QQC.ScrollBar.horizontal: QQC.ScrollBar {
            id: horizontalScrollBar
            parent: root
            z: 1
            x: root.leftPadding
            anchors.bottom: parent.bottom
            width: root.availableWidth
            active: verticalScrollBar.active
        }

        MouseArea {
            z: -1
            anchors.fill: annotationEditor
            transformOrigin: annotationEditor.transformOrigin
            scale: annotationEditor.scale
            // Ectolens: with no tool in use, a picture larger than the window pans, and one
            // that fits drags out of the window into other apps.
            readonly property bool pannable: flickable.contentItem.width > flickable.width + 1
                || flickable.contentItem.height > flickable.height + 1
            enabled: SpectacleCore.annotationDocument.tool.type === AnnotationTool.NoTool
            cursorShape: enabled ?
                (containsPress || flickable.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
                : undefined
            onPositionChanged: if (pressed && !pannable) {
                contextWindow.startDrag()
            }
        }

        Kirigami.ShadowedRectangle {
            x: annotationEditor.x
            y: annotationEditor.y
            width: annotationEditor.width * annotationEditor.scale
            height: annotationEditor.height * annotationEditor.scale
            color: "transparent"
            radius: 2
            shadow.size: 36
            shadow.yOffset: 14
            shadow.color: Qt.rgba(0, 0, 0, EctoTheme.window.light ? 0.25 : 0.55)
        }

        AnnotationEditor {
            id: annotationEditor
            x: dprRound((flickable.contentItem.width - SpectacleCore.annotationDocument.canvasRect.width * scale) / 2)
            y: dprRound((flickable.contentItem.height - SpectacleCore.annotationDocument.canvasRect.height * scale) / 2)
            implicitWidth: SpectacleCore.annotationDocument.canvasRect.width
            implicitHeight: SpectacleCore.annotationDocument.canvasRect.height
            transformOrigin: Item.TopLeft
            scale: root.fitZoom
            visible: true
            enabled: contextWindow.annotating
            document: SpectacleCore.annotationDocument
            Keys.forwardTo: cropTool
            Keys.priority: Keys.AfterItem
        }

        // Ectolens: Pick Color and Measure take the pointer over the picture
        MouseArea {
            id: inspectArea
            objectName: "inspectArea"
            anchors.fill: annotationEditor
            transformOrigin: annotationEditor.transformOrigin
            scale: annotationEditor.scale
            z: 2
            enabled: root.inspect !== ""
            visible: enabled
            hoverEnabled: true
            cursorShape: Qt.CrossCursor
            Keys.onEscapePressed: root.inspect = ""
            function track(x, y) {
                root.inspectPoint = Qt.point(x, y)
                root.inspectViewPoint = mapToItem(root, x, y)
            }
            function snapped(m) {
                let p = Qt.point(m.x, m.y)
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
                track(m.x, m.y)
                if (root.inspect === "measure" && pressed) {
                    root.measureEnd = snapped(m)
                }
            }
            onExited: root.inspectPoint = Qt.point(-1, -1)
            onPressed: m => {
                track(m.x, m.y)
                if (root.inspect === "measure") {
                    root.measureStart = Qt.point(m.x, m.y)
                    root.measureEnd = root.measureStart
                }
            }
            onClicked: m => {
                if (root.inspect === "color") {
                    const hex = EctoTheme.hex(CaptureTools.colorAt(root.toCanvas(Qt.point(m.x, m.y))))
                    CaptureTools.copyText(hex)
                    root.colorCopied(hex)
                    root.inspect = ""
                }
            }
        }

        // Measure's line, drawn at the window's scale so it stays thin at any zoom
        EctoMeasure {
            visible: root.inspect === "measure"
            width: flickable.contentWidth
            height: flickable.contentHeight
            z: 3
            start: root.measureStart.x >= 0 ? root.toView(root.measureStart) : Qt.point(-1, -1)
            end: root.toView(root.measureEnd)
            pixelRatio: root.imageDpr / annotationEditor.scale
        }

        CropTool {
            id: cropTool
            // Ectolens: the crop frame and its handles in the accent, like the overlay's frame
            palette.highlight: EctoTheme.accent
            anchors.fill: annotationEditor
            transformOrigin: annotationEditor.transformOrigin
            scale: annotationEditor.scale
            viewport: annotationEditor
            active: contextWindow.annotating
                && SpectacleCore.annotationDocument.tool.type === AnnotationTool.CropTool
        }

        AnimatedLoader {
            parent: flickable
            anchors.centerIn: parent
            state: cropTool.item && !cropTool.item.activeFocus && cropTool.item.opacity === 0 ? "active" : "inactive"
            sourceComponent: Kirigami.Heading {
                id: cropToolHelp
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: i18nc("@info crop tool explanation", "Click and drag to make a selection.\nDouble click the selection to accept and crop.\nRight click to clear the selection.")
                padding: cropToolHelpMetrics.height - cropToolHelpMetrics.descent
                leftPadding: cropToolHelpMetrics.height
                rightPadding: cropToolHelpMetrics.height
                background: FloatingBackground {
                    color: Qt.rgba(palette.window.r, palette.window.g, palette.window.b, 0.9)
                    radius: cropToolHelpMetrics.height
                }
                FontMetrics {
                    id: cropToolHelpMetrics
                    font: cropToolHelp.font
                }
            }
        }
    }


    // Ectolens: Pick Color's magnifier and readout beside the pointer, as in the overlay
    Loader {
        id: windowMagnifier
        z: 10
        active: root.inspect === "color" && root.inspectPoint.x >= 0
        readonly property real gap: Kirigami.Units.gridUnit
        x: {
            const w = item ? item.width : 0
            const px = root.inspectViewPoint.x
            return px + gap + w > root.width ? px - gap - w : px + gap
        }
        y: {
            const h = (item ? item.height : 0) + 40
            const py = root.inspectViewPoint.y
            return py + gap + h > root.height ? Math.max(0, py - gap - h) : py + gap
        }
        sourceComponent: Magnifier {
            viewport: annotationEditor
            targetPoint: root.inspectPoint
            screenRect: Qt.rect(0, 0, annotationEditor.width, annotationEditor.height)
            pixelRatio: root.imageDpr
        }
    }
    EctoPickedColor {
        visible: windowMagnifier.active
        z: 10
        x: windowMagnifier.x
        y: windowMagnifier.y + windowMagnifier.height + 6
        color: root.pickedColor
    }

}
