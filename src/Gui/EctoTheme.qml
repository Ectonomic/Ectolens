/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
pragma Singleton

import QtQuick
import org.kde.spectacle.private

/**
 * Ectolens's look, design proposal 2 (design/mock/ui-v2.html, approved 2026-11-02).
 *
 * The colors are Ectonomic's design system's (src/app/globals.css,
 * dark and light sets), and the shapes from its components/ui/button.tsx and bits.tsx:
 * rows are fully round and 36 px tall, buttons round and 28 px; menus here round their
 * corners at 12 px, a little more than the design system's 8, as the mock draws them. The accent marks where you are (the frame, the window under the pointer,
 * the tool in use, checked boxes) and never fills a button.
 *
 * Settings > Appearance picks the look (Ecto, or your KDE colors and fonts), the accent
 * (Ecto green, your KDE accent, or a color you pick) and how dark the rest of the screen gets.
 *
 * The overlay always uses the dark set, because it sits over your screen. The Ectolens
 * window follows Plasma: the light set in a light color scheme.
 */
QtObject {
    id: theme

    readonly property bool ectoLook: Settings.look !== Settings.KdeLook
    readonly property SystemPalette system: systemPalette
    readonly property bool systemIsLight: systemPalette ? systemPalette.window.hslLightness > 0.5 : false

    readonly property color ectoGreen: "#c8f22e"
    readonly property color accent: Settings.accent === Settings.KdeAccent ? systemPalette.highlight
        : Settings.accent === Settings.CustomAccent ? Settings.customAccentColor
        : ectoGreen
    /// what reads on top of the accent (the check mark in a checked box)
    readonly property color onAccent: accent.hslLightness > 0.55 ? "#0a0a0a" : "#ffffff"
    /// How dark the rest of the screen gets around the frame (0 to 0.8).
    readonly property real dim: Math.max(0, Math.min(0.8, Settings.dimAmount / 100))

    readonly property string sans: ectoLook ? "Geist" : Qt.application.font.family
    readonly property string mono: ectoLook ? "Geist Mono" : "monospace"

    // The sets are plain values, so they exist before anything reads them.
    // Ectonomic's design system, dark: globals.css :root.dark
    readonly property var dark: ({
        light: false,
        bg: "#0a0a0a", bgSubtle: "#111113", bgMuted: "#18181b", bgLift: "#2a2a2e",
        border: "#27272a", borderStrong: "#3f3f46",
        fg: "#fafafa", fgMuted: "#a1a1aa", fgSubtle: "#8b8b94",
        error: "#f87171", success: "#4ade80", info: "#60a5fa", warning: "#fbbf24", shadow: Qt.rgba(0, 0, 0, 0.6),
    })
    // Ectonomic's design system, light: globals.css :root
    readonly property var light: ({
        light: true,
        bg: "#ffffff", bgSubtle: "#fafafa", bgMuted: "#f4f4f5", bgLift: "#f4f4f5",
        border: "#e4e4e7", borderStrong: "#d4d4d8",
        fg: "#18181b", fgMuted: "#52525b", fgSubtle: "#71717a",
        error: "#b91c1c", success: "#15803d", info: "#1d4ed8", warning: "#b45309", shadow: Qt.rgba(0, 0, 0, 0.25),
    })
    // Your KDE colors: the same roles read from the color scheme.
    readonly property var kde: {
        const p = systemPalette
        const tx = p.windowText
        const a = v => Qt.rgba(tx.r, tx.g, tx.b, v)
        return {
            light: theme.systemIsLight,
            bg: p.window, bgSubtle: p.window, bgMuted: p.button, bgLift: Qt.tint(p.button, a(0.1)),
            border: a(0.15), borderStrong: a(0.3),
            fg: tx, fgMuted: a(0.72), fgSubtle: a(0.58),
            error: "#da4453", success: "#27ae60", info: "#3daee9", warning: "#f67400", shadow: Qt.rgba(0, 0, 0, 0.4),
        }
    }
    readonly property SystemPalette systemPalette: SystemPalette {
        colorGroup: SystemPalette.Active
    }

    readonly property var overlay: ectoLook ? dark : kde
    readonly property var window: !ectoLook ? kde : systemIsLight ? light : dark

    /// The tokens for the window an item is in: the window's set in the Ectolens window,
    /// the overlay's everywhere else.
    function of(win) {
        return win && win.isViewer ? window : overlay
    }

    /// The marking tools' colors (the color menu's swatches): Ecto green first, then a hue
    /// circle of clear, saturated colors, then white to black. Text and Number start in Ecto green.
    readonly property var markColors: ["#c8f22e", "#facc15", "#fb923c", "#ef4444", "#ec4899", "#a855f7",
                                       "#3b82f6", "#22d3ee", "#ffffff", "#a1a1aa", "#3f3f46", "#0a0a0a"]

    // sizes
    readonly property real rowHeight: 36
    readonly property real buttonHeight: 28
    /// A row's padding on every side, its ends too (the gap around a button at a
    /// row's end must be even all the way around). A button then sits as far from the row's rounded end
    /// as from its top and bottom, concentric with it: Apple's concentric corners, where the inner
    /// radius is the outer radius less the distance between them (18 − 4 = 14, a button's radius).
    readonly property real rowInset: (rowHeight - buttonHeight) / 2
    /// From a control's rounded end to its first ink, an icon or words (Capture, Copy, the frame's
    /// size); words standing alone at a row's end keep the same room (EctoLabel).
    readonly property real controlPadding: 10
    /// Between neighbors in a row; a separator stands between groups.
    readonly property real rowSpacing: 2
    /// Between a row and what belongs to it: the options row and its tool row, a menu and its button.
    readonly property real rowGap: 8
    /// A full-width bar's padding (the Ectolens window's top and bottom bars, its notices): as far
    /// from the window's sides as a button sits from the bar's top and bottom ((44 − 28) / 2).
    readonly property real barInset: 8
    // a slider's room past each end of its track (the strength slider's ends were
    // cramped), so its thumb at 0 and at 100% stands clear of what is beside it; the thumb itself
    // stays inside the track, as a browser's range slider keeps it (EctoSlider)
    readonly property real sliderSidePadding: 8
    readonly property real menuRadius: 12
    /// A menu's padding, as a row's: every line's surface (an item's hover, the font menu's search
    /// field and weight) stands this far from the menu's sides, and the first and last line from its
    /// top and bottom, so its corners are concentric with the menu's (the rows'
    /// rule carried into the menus).
    readonly property real menuInset: rowInset
    /// a menu line's corners: the menu's less the distance between them (12 − 4 = 8)
    readonly property real menuItemRadius: menuRadius - menuInset
    /// A line's words, marks and end controls stand a control's room inside its surface, so every
    /// menu's content runs from menuInset + controlPadding (14) to the same distance from its end:
    /// headers, ticks, icons, check boxes, fields, shortcuts, reasons, arrows, a row's choice.
    readonly property real menuContentInset: menuInset + controlPadding
    /// the slot a line's first mark stands in (an icon, a tick, a check's box) and the gap after it,
    /// so the words after any of them start in one column (14 + 16 + 10 = 40 in a menu)
    readonly property real leadSize: 16
    readonly property real leadGap: 10
    readonly property real fontSize: 13
    readonly property int monoSize: 12
}
