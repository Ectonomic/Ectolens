/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private
import org.kde.kquickimageeditor.ectolens

/**
 * The marking tools, in Spectacle's order with a gap between kinds: undo and redo, crop
 * and select, lines, shapes, hiding, then text and numbers. The tool in use carries the
 * accent line; undo and redo stay faint until there is something to undo.
 *
 * In the overlay the first tool is "frame" (no tool: move and resize the frame); in the
 * Ectolens window it is Crop.
 */
Row {
    id: root
    readonly property AnnotationDocument document: SpectacleCore.annotationDocument
    readonly property AnnotationTool tool: document.tool
    readonly property int toolType: tool?.type ?? AnnotationTool.NoTool
    /// the overlay: the crop button means "no tool" (adjust the frame)
    property bool inOverlay: true
    spacing: 2
    // the button of the tool in use (what the options in its row belong to), or null
    readonly property Item currentTool: {
        root.toolType // follows the tool
        for (const c of children) {
            if (c.hasOwnProperty("type") && c.current && c.visible) {
                return c
            }
        }
        return null
    }

    component Tool: EctoButton {
        property int type: AnnotationTool.NoTool
        property string help: ""
        iconOnly: true
        toolTipText: help.length > 0 ? text + " · " + help : text
        current: root.toolType === type
        onClicked: root.tool.type = (root.toolType === type && type !== AnnotationTool.NoTool && !root.inOverlay) ? AnnotationTool.NoTool : type
    }

    EctoButton {
        objectName: "undoButton"
        iconName: "edit-undo"
        iconOnly: true
        text: i18nc("@action", "Undo")
        shortcutText: CaptureTools.keyText("Ctrl+Z")
        // in the overlay the frame's steps too (FrameHistory), in the window the marks'
        enabled: root.inOverlay ? FrameHistory.canUndo : root.document.undoStackDepth > 0
        onClicked: root.inOverlay ? FrameHistory.undo() : root.document.undo()
    }
    EctoButton {
        objectName: "redoButton"
        iconName: "edit-redo"
        iconOnly: true
        text: i18nc("@action", "Redo")
        shortcutText: CaptureTools.keyText("Ctrl+Shift+Z")
        enabled: root.inOverlay ? FrameHistory.canRedo : root.document.redoStackDepth > 0
        onClicked: root.inOverlay ? FrameHistory.redo() : root.document.redo()
    }
    EctoSeparator {}
    Tool {
        objectName: "frameTool"
        iconName: "transform-crop"
        text: root.inOverlay ? i18nc("@action:intoolbar", "Adjust the frame") : i18nc("@action:intoolbar crop image tool", "Crop")
        type: root.inOverlay ? AnnotationTool.NoTool : AnnotationTool.CropTool
    }
    Tool {
        objectName: "selectTool"
        iconName: "edit-select"
        text: i18nc("@action:intoolbar select annotation tool", "Select")
        type: AnnotationTool.SelectTool
        enabled: root.document.undoStackDepth > 0
    }
    EctoSeparator {}
    Tool {
        iconName: "draw-freehand"
        text: i18nc("@action:intoolbar freehand line tool", "Freehand")
        help: i18nc("@info:tooltip", "Shift: Snap to straight lines")
        type: AnnotationTool.FreehandTool
    }
    Tool {
        iconName: "draw-highlight"
        text: i18nc("@action:intoolbar highlighter line tool", "Highlighter")
        help: i18nc("@info:tooltip", "Shift: Snap to straight lines")
        type: AnnotationTool.HighlighterTool
    }
    Tool {
        iconName: "draw-line"
        text: i18nc("@action:intoolbar straight line tool", "Line")
        help: i18nc("@info:tooltip", "Shift: Snap to 45 degree increments")
        type: AnnotationTool.LineTool
    }
    Tool {
        objectName: "arrowTool"
        iconName: "draw-arrow"
        text: i18nc("@action:intoolbar arrow line tool", "Arrow")
        help: i18nc("@info:tooltip", "Shift: Snap to 45 degree increments")
        type: AnnotationTool.ArrowTool
    }
    EctoSeparator {}
    Tool {
        objectName: "rectangleTool"
        iconName: "draw-rectangle"
        text: i18nc("@action:intoolbar rectangle tool", "Rectangle")
        help: i18nc("@info:tooltip", "Shift: Snap to diagonal movements. Control: Resize from center")
        type: AnnotationTool.RectangleTool
    }
    Tool {
        iconName: "draw-ellipse"
        text: i18nc("@action:intoolbar ellipse tool", "Ellipse")
        help: i18nc("@info:tooltip", "Shift: Snap to diagonal movements. Control: Resize from center")
        type: AnnotationTool.EllipseTool
    }
    EctoSeparator {}
    Tool {
        iconName: "pixelate"
        text: i18nc("@action:intoolbar pixelate tool", "Pixelate")
        type: AnnotationTool.PixelateTool
    }
    Tool {
        iconName: "blur"
        text: i18nc("@action:intoolbar blur tool", "Blur")
        type: AnnotationTool.BlurTool
    }
    EctoSeparator {}
    Tool {
        objectName: "textTool"
        iconName: "draw-text"
        text: i18nc("@action:intoolbar text tool", "Text")
        type: AnnotationTool.TextTool
    }
    Tool {
        objectName: "numberTool"
        iconName: "draw-number"
        text: i18nc("@action:intoolbar number stamp tool", "Number")
        type: AnnotationTool.NumberTool
    }
}
