/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import org.kde.spectacle.private

/// A section title in a menu ("After Capture", "Send to").
Text {
    readonly property var t: EctoTheme.of(contextWindow)
    readonly property bool fillsWidth: true
    leftPadding: EctoTheme.controlPadding
    rightPadding: EctoTheme.controlPadding
    topPadding: 8
    bottomPadding: 4
    color: t.fgSubtle
    font.family: EctoTheme.sans
    font.pixelSize: 12
    font.weight: Font.Medium
}
