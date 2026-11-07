/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Shapes
import org.kde.spectacle.private

/**
 * The frame's handles: Spectacle's three-quarter circles at
 * the corners and half circles on the edges, curving into the frame line through a small
 * fillet instead of stopping in a notch. No rims. Radius 6 and join curve 3.5
 * (a little smaller than design/mock/ui-v2.html, which drew 7 and 4, CORNER and EDGE).
 *
 * Place it on the middle of the frame line's stroke, and set line to the stroke's width: each
 * circle is centered on the line, its flat side covering the line's inner half (the outer half
 * is the line itself). The shapes reach outside the rect.
 */
Item {
    id: root
    property color color: EctoTheme.accent
    property real handleRadius: 6
    property real fillet: 3.5
    // the frame line's width; the handles cover its inner half
    property real line: 1

    readonly property real xf: Math.sqrt((handleRadius + fillet) ** 2 - fillet ** 2)

    function onCircle(x, y) {
        const d = Math.hypot(x, y)
        return [x * handleRadius / d, y * handleRadius / d]
    }
    // a point turned by a multiple of 90 degrees and moved to (ox, oy)
    function tp(p, turn, ox, oy) {
        const [x, y] = p
        switch (turn) {
        case 90: return [ox - y, oy + x]
        case 180: return [ox - x, oy - y]
        case 270: return [ox + y, oy - x]
        default: return [ox + x, oy + y]
        }
    }
    function fmt(p) {
        return p[0].toFixed(2) + " " + p[1].toFixed(2)
    }
    // A corner: the circle around the corner point, filling the three outside quarters,
    // joining the two frame lines that leave it.
    function corner(turn, ox, oy) {
        const R = handleRadius, F = fillet, X = xf, T = line / 2
        const a = onCircle(X, -F), c = onCircle(-F, X)
        const P = q => fmt(tp(q, turn, ox, oy))
        return `M ${P([X, 0])} A ${F} ${F} 0 0 1 ${P(a)} A ${R} ${R} 0 1 0 ${P(c)} A ${F} ${F} 0 0 1 ${P([0, X])} `
             + `L ${P([T, X])} L ${P([T, T])} L ${P([X, T])} Z`
    }
    // An edge: a half circle outside the frame line, joining the line on both sides.
    function edge(turn, ox, oy) {
        const R = handleRadius, F = fillet, X = xf, T = line / 2
        const a = onCircle(-X, -F), c = onCircle(X, -F)
        const P = q => fmt(tp(q, turn, ox, oy))
        return `M ${P([-X, 0])} A ${F} ${F} 0 0 0 ${P(a)} A ${R} ${R} 0 0 1 ${P(c)} A ${F} ${F} 0 0 0 ${P([X, 0])} `
             + `L ${P([X, T])} L ${P([-X, T])} Z`
    }

    // One shape per handle: drawn as one combined path, the scene graph's triangulation lost
    // some of them (the bottom-right corner, part of the top edge) in the overlay.
    component Handle: ShapePath {
        property string d
        fillColor: root.color
        strokeColor: "transparent"
        strokeWidth: 0
        PathSvg { path: d }
    }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        Handle { d: root.corner(0, 0, 0) }
        Handle { d: root.corner(90, root.width, 0) }
        Handle { d: root.corner(180, root.width, root.height) }
        Handle { d: root.corner(270, 0, root.height) }
        Handle { d: root.edge(0, root.width / 2, 0) }
        Handle { d: root.edge(90, root.width, root.height / 2) }
        Handle { d: root.edge(180, root.width / 2, root.height) }
        Handle { d: root.edge(270, 0, root.height / 2) }
    }
}
