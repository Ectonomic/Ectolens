/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/**
 * Words, and anything drawn with them (an icon, a color, the recording dot), standing in a row
 * the way a button does, without its surface: as tall as a button, with a button's room at its
 * ends (EctoTheme.controlPadding) and its gap between an icon and words. A row that begins or
 * ends with words keeps the distance it would keep with a button there (spacing is
 * equalized and standardized across the bars).
 */
Item {
    id: root
    default property alias content: row.data
    property alias spacing: row.spacing
    implicitHeight: EctoTheme.buttonHeight
    // in whole pixels: words measure in fractions
    implicitWidth: Math.ceil(row.implicitWidth) + EctoTheme.controlPadding * 2
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
    Row {
        id: row
        x: EctoTheme.controlPadding
        height: parent.height
        // the gap EctoButton keeps between its icon and its words
        spacing: 6
    }
}
