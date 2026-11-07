/*
 *  SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Layouts
import org.kde.kquickimageeditor.ectolens
import org.kde.spectacle.private

AnnotationViewport {
    id: root

    document: SpectacleCore.annotationDocument
    viewportRect: Qt.rect(0, 0, width, height)

    onPressedChanged: if (pressed) {
        if (textTool.shouldShow) {
            textTool.forceActiveFocus(Qt.MouseFocusReason);
        }
    }

    Item {
        x: -root.viewportRect.x
        y: -root.viewportRect.y
        transformOrigin: Item.TopLeft
        // Ectolens: the marking tools' own outlines and handles (they draw with the palette's
        // highlight) take Ectolens's accent, like the frame and the tool in use
        palette.highlight: EctoTheme.accent
        palette.highlightedText: EctoTheme.onAccent
        TextTool {
            id: textTool
            viewport: root
        }
        AnnotationSelectionTool {
            id: selectionTool
            viewport: root
        }
        HoverOutline {
            viewport: root
            hidden: selectionTool.hovered || selectionTool.dragging
        }
    }

    // Ectolens: the overlay has its own Undo and Redo keys, for the frame's steps too (FrameHistory)
    property bool undoKeys: true
    Shortcut {
        enabled: root.enabled && root.undoKeys
        sequences: [StandardKey.Undo]
        onActivated: root.document.undo()
    }
    Shortcut {
        enabled: root.enabled && root.undoKeys
        sequences: [StandardKey.Redo]
        onActivated: root.document.redo()
    }
}
