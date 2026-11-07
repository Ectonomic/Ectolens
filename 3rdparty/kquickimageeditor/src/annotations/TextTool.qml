/* SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>
 * SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.kde.kquickimageeditor.ectolens
import 'private' as P

AnimatedLoader {
    id: root
    required property AnnotationViewport viewport
    readonly property AnnotationDocument document: viewport.document
    readonly property bool shouldShow: enabled
        && document.selectedItem.options & AnnotationTool.TextOption
        && (document.tool.options & AnnotationTool.TextOption
            || document.tool.type === AnnotationTool.SelectTool)

    state: shouldShow ? "active" : "inactive"

    sourceComponent: T.TextArea {
        id: textField
        readonly property bool mirrored: effectiveHorizontalAlignment === TextInput.AlignRight

        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true

        // Ectolens: the words' own rect (a label's box is padded around it, see insets). The
        // width is set, not only implied: Qt Quick's TextEdit wraps only at a width set on it.
        width: implicitWidth
        height: implicitHeight
        Binding on implicitWidth {
            value: root.document.selectedItem.geometryPath.boundingRect.width
            restoreMode: Binding.RestoreNone
            when: root.shouldShow
        }
        Binding on implicitHeight {
            value: root.document.selectedItem.geometryPath.boundingRect.height
            restoreMode: Binding.RestoreNone
            when: root.shouldShow
        }
        Binding {
            target: root
            property: "x"
            when: root.shouldShow
            value: root.document.selectedItem.geometryPath.boundingRect.x - root.document.canvasRect.x
            restoreMode: Binding.RestoreNone
        }
        Binding {
            target: root
            property: "y"
            when: root.shouldShow
            value: root.document.selectedItem.geometryPath.boundingRect.y - root.document.canvasRect.y
            restoreMode: Binding.RestoreNone
        }
        // Ectolens: a text mark with a showing fill is a label; its words (and so the cursor and
        // selection here) take near-black or white, whichever reads on the fill
        readonly property bool isLabel: root.document.selectedItem.options & AnnotationTool.FillOption
            && root.document.selectedItem.fillColor.a > 0
        // Ectolens: a press anywhere in a box or a label, its padding too, places the cursor (instead of
        // reaching the canvas below and starting a new mark)
        containmentMask: padded ? outsideArea : null
        // Ectolens: a Text mark drawn as a box (a drag with the Text tool): its words wrap inside it,
        // inside the same padding a label has, so the box is where it was drawn with or without a label
        readonly property bool isBox: root.document.selectedItem.textBox
        readonly property bool padded: isLabel || isBox
        readonly property size labelPadding: padded ? Utils.textLabelPadding(font) : Qt.size(0, 0)
        property color textColor
        Binding on textColor {
            value: textField.isLabel ? Utils.labelTextColor(root.document.selectedItem.fillColor)
                : root.document.selectedItem.options & AnnotationTool.TextOption ?
                root.document.selectedItem.fontColor : root.document.tool.fontColor
            restoreMode: Binding.RestoreNone
            when: root.shouldShow
        }
        color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0)
        Binding on font {
            value: root.document.selectedItem.options & AnnotationTool.TextOption ?
                root.document.selectedItem.font : root.document.tool.font
            restoreMode: Binding.RestoreNone
            when: root.shouldShow
        }

        focus: true
        selectByMouse: true
        selectionColor: Qt.rgba(1-textColor.r, 1-textColor.g, 1-textColor.b, 1)
        selectedTextColor: Qt.rgba(textColor.r, textColor.g, textColor.b, 1)
        cursorPosition: {
            const mapped = mapFromItem(root.viewport, root.viewport.pressPosition)
            return positionAt(mapped.x, mapped.y)
        }
        cursorDelegate: Item {
            id: cursor
            visible: textField.cursorVisible
            Rectangle {
                // prevent the cursor from overlapping with the background
                x: {
                    const atStart = textField.cursorPosition === 0;
                    const atEnd = textField.cursorPosition === textField.length;
                    const atNewLine = !atEnd && textField.text.charAt(textField.cursorPosition) === '\n';
                    const afterNewLine = !atStart && textField.text.charAt(textField.cursorPosition - 1) === '\n';
                    if (!atStart && (atEnd || atNewLine) && !afterNewLine) {
                        return -width;
                    }
                    return 0;
                }
                // Ensure cursor is at least 1 physical pixel thick
                width: Utils.clamp(Utils.dprRound(Math.max(1, fontMetrics.xHeight / 12), Screen.devicePixelRatio),
                                   1 / Screen.devicePixelRatio) / root.viewport.scale
                height: parent.height
                color: Qt.rgba(textField.textColor.r, textField.textColor.g, textField.textColor.b, 1)
            }
            Connections {
                target: textField
                function onCursorPositionChanged(): void {
                    if (textField.cursorVisible) {
                        Qt.callLater(blinkAnimation.restart)
                    }
                }
            }
            SequentialAnimation {
                id: blinkAnimation
                running: textField.cursorVisible
                loops: Animation.Infinite
                PropertyAction {
                    target: cursor
                    property: "visible"
                    value: true
                }
                PauseAnimation {
                    duration: Qt.styleHints.cursorFlashTime / 2
                }
                PropertyAction {
                    target: cursor
                    property: "visible"
                    value: false
                }
                PauseAnimation {
                    duration: Qt.styleHints.cursorFlashTime / 2
                }
            }
        }

        // Keep this in sync with the value used in Traits::createTextPath
        // Ectolens: from the font itself, so it follows a font change (Utils.textTabStopDistance)
        tabStopDistance: Utils.textTabStopDistance(font)
        // QPainter::drawText doesn't support rich text.
        // We could consider using QStaticText to add rich text support.
        // We probably shouldn't use QTextDocument because that's unnecessarily heavy.
        textFormat: TextEdit.PlainText
        // Ectolens: where the words sit, as Traits::drawTextWords places them (the mark's own alignment,
        // or the Text tool's before there are words): across their box or label, and up and down a box
        readonly property int wordsAlignment: root.document.selectedItem.options & AnnotationTool.TextOption
            ? root.document.selectedItem.textAlignment : root.document.tool.textAlignment
        horizontalAlignment: wordsAlignment & (Qt.AlignLeft | Qt.AlignHCenter | Qt.AlignRight) || TextEdit.AlignLeft
        verticalAlignment: isBox ? wordsAlignment & (Qt.AlignTop | Qt.AlignVCenter | Qt.AlignBottom) || TextEdit.AlignTop
            : TextEdit.AlignTop
        // Ectolens: a box's words wrap at word boundaries, or anywhere in a word too long for a line,
        // as Traits::drawTextWords paints them
        wrapMode: isBox ? TextEdit.Wrap : TextEdit.NoWrap

        // QPainter uses native antialiasing
        renderType: TextEdit.NativeRendering

        // Ectolens: while nothing is shown the value follows the editing box's own words, so when a mark is
        // taken again before the box has finished hiding, the Binding's first, late value changes nothing
        // (it was the last mark's empty text, which onTextChanged then wrote into the new mark, wiping its words)
        Binding on text {
            value: root.shouldShow ? root.document.selectedItem.text : textField.text
            restoreMode: Binding.RestoreNone
            when: root.shouldShow
        }
        onTextChanged: {
            if (root.document.selectedItem.text === text) {
                return
            }
            const wasEmpty = root.document.selectedItem.text.length === 0
            root.document.selectedItem.text = text
            if (wasEmpty) {
                // saved at once, so the mark is real in the history (painted, exported, kept)
                root.document.selectedItem.commitChanges()
            } else {
                // Ectolens: saved a moment after the last key, taking the first letter's step (commitTyping), so
                // words typed in one go are one step to undo; the document saves words not yet saved before the
                // mark is let go, before an undo or redo and before a crop (savePendingWords)
                commitChangesTimer.restart()
            }
        }

        // Ectolens: one history for everything. Ctrl+Z and Ctrl+Shift+Z go to the picture's history, typing
        // too: the latest step, typing, a handle drag, a move or a choice in the options rail, is undone (or
        // redone). The editing box claims these keys for its own undo whenever it can be typed in
        // (QQuickTextControl's common text shortcuts), which kept the picture's history out of reach while a
        // mark was written, and its own undo could not stay in step with the picture's.
        Keys.onShortcutOverride: (event) => {
            if (event.matches(StandardKey.Undo) || event.matches(StandardKey.Redo)) {
                event.accepted = true
            }
        }
        Keys.onPressed: (event) => {
            if (event.matches(StandardKey.Undo)) {
                commitChangesTimer.stop()
                root.document.undo()
                event.accepted = true
            } else if (event.matches(StandardKey.Redo)) {
                commitChangesTimer.stop()
                root.document.redo()
                event.accepted = true
            }
        }

        Keys.onDeletePressed: (event) => {
            event.accepted = text.length === 0
            if (event.accepted) {
                root.document.deleteSelectedItem()
            }
        }

        Timer {
            id: commitChangesTimer
            interval: 250
            onTriggered: root.document.selectedItem.commitTyping()
        }

        Connections {
            target: root.document
            function onSelectedItemWrapperChanged(): void {
                commitChangesTimer.stop()
            }
        }

        // Ectolens: the outline goes around a label's box, not just its words
        leftInset: -background.strokeWidth - labelPadding.width
        rightInset: -background.strokeWidth - labelPadding.width
        topInset: -background.strokeWidth - labelPadding.height
        bottomInset: -background.strokeWidth - labelPadding.height
        background: P.DashedOutline {
            pathHints: ShapePath.PathLinear
            strokeWidth: Utils.clamp(Utils.dprRound(1, Screen.devicePixelRatio),
                                     1 / Screen.devicePixelRatio)  / root.viewport.scale
        }

        FontMetrics {
            id: fontMetrics
            font: textField.font
        }

        // Ectolens: while a drawn box has no words yet, its label shows here, so the box is seen as
        // it's drawn (the picture gets a label only once there are words)
        Rectangle {
            z: -1
            visible: textField.isBox && textField.text.length === 0
                && root.document.selectedItem.options & AnnotationTool.FillOption
                && root.document.selectedItem.fillColor.a > 0
            x: -textField.labelPadding.width
            y: -textField.labelPadding.height
            width: textField.width + textField.labelPadding.width * 2
            height: textField.height + textField.labelPadding.height * 2
            radius: Utils.textLabelRadius(textField.font)
            color: root.document.selectedItem.fillColor
            border.width: 1
            border.color: Utils.labelRingColor(root.document.selectedItem.fillColor)
        }

        // Ectolens: handles on a box's edges and corners change its size and the words rewrap. Each takes
        // the pointer on its part outside the box and a thin band along the box's edge, so none covers the words;
        // a MouseArea (not a DragHandler) because a drag whose motion reaches Qt only with the release
        // must still resize: DragHandler drops a release's position (seen in the off-screen textbox run).
        // A side handle keeps the height the box was drawn to, so the box can shrink back as words go.
        // the box or label with its padding and outline: where a press places the cursor
        Item {
            id: outsideArea
            x: boxHandles.x
            y: boxHandles.y
            width: boxHandles.width
            height: boxHandles.height
        }
        Item {
            id: boxHandles
            readonly property real offset: textField.background.strokeWidth
            x: -textField.labelPadding.width - offset
            y: -textField.labelPadding.height - offset
            width: textField.width + (textField.labelPadding.width + offset) * 2
            height: textField.height + (textField.labelPadding.height + offset) * 2
            visible: textField.isBox
            Repeater {
                objectName: "boxHandleMaker"
                model: [Qt.TopEdge | Qt.LeftEdge, Qt.TopEdge, Qt.TopEdge | Qt.RightEdge, Qt.LeftEdge,
                        Qt.RightEdge, Qt.BottomEdge | Qt.LeftEdge, Qt.BottomEdge, Qt.BottomEdge | Qt.RightEdge]
                delegate: P.Handle {
                    id: boxHandle
                    required property int modelData
                    objectName: "textBoxHandle" + modelData
                    edges: modelData
                    visualWidth: dprRoundEven(14) / root.viewport.scale
                    visualHeight: visualWidth
                    strokeWidth: 1 / Screen.devicePixelRatio / root.viewport.scale
                    x: relativeXForEdges(boxHandles, edges)
                    y: relativeYForEdges(boxHandles, edges)
                    MouseArea {
                        id: boxDrag
                        anchors.fill: parent
                        // the handle's square, except inside the box past a 2-pixel band along its edge
                        containmentMask: QtObject {
                            // typed, so it is invokable as contains(QPointF), which a mask needs
                            function contains(point: point): bool {
                                if (point.x < 0 || point.y < 0 || point.x > boxDrag.width || point.y > boxDrag.height) {
                                    return false
                                }
                                const p = boxDrag.mapToItem(boxHandles, point.x, point.y)
                                const band = 2 / root.viewport.scale
                                return !(p.x > band && p.x < boxHandles.width - band && p.y > band && p.y < boxHandles.height - band)
                            }
                        }
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: boxHandle.cursorShapeForEdges(boxHandle.edges)
                        property point pressDocumentPos
                        property rect pressOutside
                        property real pressBoxHeight
                        function resizeTo(mouse) {
                            const now = Utils.sceneToDocumentPoint(mapToItem(null, mouse.x, mouse.y), root.viewport)
                            const dx = now.x - pressDocumentPos.x
                            const dy = now.y - pressDocumentPos.y
                            const minimum = Utils.textBoxMinimumSize(textField.font)
                            const vertical = boxHandle.edges & (Qt.TopEdge | Qt.BottomEdge)
                            let left = pressOutside.x, right = pressOutside.x + pressOutside.width
                            let top = pressOutside.y
                            let bottom = pressOutside.y + (vertical ? pressOutside.height : pressBoxHeight)
                            // an edge stops at the smallest box rather than passing the other edge
                            if (boxHandle.edges & Qt.LeftEdge) left = Math.min(left + dx, right - minimum.width)
                            if (boxHandle.edges & Qt.RightEdge) right = Math.max(right + dx, left + minimum.width)
                            if (boxHandle.edges & Qt.TopEdge) top = Math.min(top + dy, bottom - minimum.height)
                            if (boxHandle.edges & Qt.BottomEdge) bottom = Math.max(bottom + dy, top + minimum.height)
                            root.document.selectedItem.setTextBox(Qt.rect(left, top, right - left, bottom - top))
                        }
                        onPressed: (mouse) => {
                            // words typed and not yet saved are their own step, before the drag's
                            commitChangesTimer.stop()
                            root.document.selectedItem.commitTyping()
                            pressDocumentPos = Utils.sceneToDocumentPoint(mapToItem(null, mouse.x, mouse.y), root.viewport)
                            pressOutside = root.document.selectedItem.textOutsideRect
                            pressBoxHeight = root.document.selectedItem.textBoxSize.height
                        }
                        onPositionChanged: (mouse) => {
                            if (pressed) {
                                resizeTo(mouse)
                            }
                        }
                        onReleased: (mouse) => {
                            resizeTo(mouse)
                            root.document.selectedItem.commitChanges()
                        }
                    }
                }
            }
        }

        Rectangle {
            id: handle
            objectName: "textMoveHandle"
            implicitHeight: fontMetrics.height % 2 ? fontMetrics.height + 1 : fontMetrics.height
            implicitWidth: implicitHeight
            // Ectolens: past a box's handles (sized on screen), at any zoom
            readonly property real gap: textField.isBox
                ? textField.labelPadding.width + boxHandles.offset + (7 + Kirigami.Units.smallSpacing) / root.viewport.scale
                : Kirigami.Units.mediumSpacing + textField.labelPadding.width
            // Ectolens: beside right-aligned words on the right, otherwise on the left (centered words
            // too); placed by x, not by anchors, as the alignment can change while the words are written
            // (anchoring one side after the other leaves the handle as wide as both made it)
            width: implicitWidth
            height: implicitHeight
            x: textField.effectiveHorizontalAlignment === TextInput.AlignRight ? parent.width + gap : -width - gap
            radius: height / 2
            color: Kirigami.Theme.backgroundColor
            Kirigami.Icon {
                height: Kirigami.Units.iconSizes.roundedIconSize(parent.height)
                width: height
                anchors.centerIn: parent
                source: "transform-move"
            }
            DragHandler {
                id: dragHandler
                target: null
                cursorShape: Qt.SizeAllCursor
                dragThreshold: 0
                property point lastDocumentPos
                onActiveTranslationChanged: if (active && lastDocumentPos !== undefined) {
                    // We use the difference between the current document position
                    // and a stored document press position instead of activeTranslation
                    // so that the real translation can be tracked regardless of how the view is scaled.
                    let documentMousePos = Utils.sceneToDocumentPoint(centroid.scenePosition, root.viewport)

                    let matrix = Qt.matrix4x4()
                    matrix.translate(
                        // We want the relative mouse movement since the last onActiveTranslationChanged
                        Qt.vector2d(documentMousePos.x - lastDocumentPos.x, documentMousePos.y - lastDocumentPos.y)
                    )
                    root.document.selectedItem.applyTransform(matrix)
                    lastDocumentPos = documentMousePos;
                }
                // DragHandler::activeChanged is emitted before DragHandler::translationChanged.
                // HandlerPoint is completely updated, so we don't want to do onCentroidChanged.
                // Otherwise, we would update our document press position when we don't want to.
                onActiveChanged: if (active) {
                    // Ectolens: words typed and not yet saved are their own step, before the move's
                    commitChangesTimer.stop()
                    root.document.selectedItem.commitTyping()
                    lastDocumentPos = Utils.sceneToDocumentPoint(centroid.scenePressPosition, root.viewport)
                } else {
                    root.document.selectedItem.commitChanges()
                }
            }
            TapHandler {
                cursorShape: Qt.SizeAllCursor
            }
            HoverHandler {
                cursorShape: Qt.SizeAllCursor
            }
        }
        P.TextContextMenu {
            id: contextMenu
            target: textField
            // Ectolens: its Undo and Redo are the picture's, as Ctrl+Z is
            document: root.document
        }
        transform: Matrix4x4 {
            matrix: root.document.transform
        }
        onPressed: (event) => {
            if (event.button === Qt.RightButton) {
                contextMenu.popup(event.x, event.y)
            }
        }
        onPressAndHold: (event) => {
            if (event.flags !== Qt.MouseEventNotSynthesized) {
                contextMenu.popup(event.x, event.y)
            }
        }
        Component.onCompleted: forceActiveFocus()
    }
}

