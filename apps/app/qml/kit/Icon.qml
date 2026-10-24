// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import WordJewelsSolver

// Line icons on a 24 x 24 grid: 2 px stroke, round caps, no fill, drawn as
// vector paths so they scale cleanly and take a colour token. A picture, never
// text: the app's buttons for a child say what they do with a drawing. The
// navigation arrows (back, forward) turn round when the layout is mirrored;
// nothing else does.
Item {
    id: root

    property string name: ""
    property color color: Theme.ink
    property real strokeWidth: 2

    readonly property bool mirrorsInRtl: name === "back" || name === "forward"
    // The attached property belongs to an Item, so it is read here.
    readonly property bool layoutMirrored: Theme.rightToLeft

    // The drawing grid; the item scales it to whatever size it is given.
    readonly property real grid: 24

    implicitWidth: grid
    implicitHeight: grid

    function pathFor(icon) {
        switch (icon) {
        case "back":
            return "M15 5 L8 12 L15 19";
        case "forward":
            return "M9 5 L16 12 L9 19";
        case "close":
            return "M6 6 L18 18 M18 6 L6 18";
        case "parent":
            // A head and shoulders: a grown-up.
            return "M12 4.5 a3.5 3.5 0 1 1 0 7 a3.5 3.5 0 1 1 0 -7 M5 20 C5 15.5 8.5 14 12 14 C15.5 14 19 15.5 19 20";
        case "history":
            // Four small boards.
            return "M4 4 h7 v7 h-7 z M13 4 h7 v7 h-7 z M4 13 h7 v7 h-7 z M13 13 h7 v7 h-7 z";
        case "jewel":
            // A cut stone from the side: the crown, the table and the point.
            return "M7.5 4.5 h9 l4 5 l-8.5 10.5 l-8.5 -10.5 z M3.5 9.5 h17 M9.5 9.5 l2.5 10.5 M14.5 9.5 l-2.5 10.5";
        case "hint":
            // A ring with a dot in it: the start tile's pulsing ring.
            return "M12 4 a8 8 0 1 1 0 16 a8 8 0 1 1 0 -16 M12 10 a2 2 0 1 1 0 4 a2 2 0 1 1 0 -4";
        case "done":
            return "M5 12.5 L9.5 17 L19 7.5";
        case "again":
            // An arrow going round: play again.
            return "M19 12 a7 7 0 1 1 -2.1 -5 M19 4.5 L19 8.5 L15 8.5";
        case "shake":
            // A die with its four pips, and a shake-arc either side of it.
            return "M8.5 6.5 h7 a2 2 0 0 1 2 2 v7 a2 2 0 0 1 -2 2 h-7 a2 2 0 0 1 -2 -2 v-7 a2 2 0 0 1 2 -2 z"
                   + " M10.5 10 l0.01 0 M13.5 10 l0.01 0 M10.5 14 l0.01 0 M13.5 14 l0.01 0"
                   + " M3.5 9 q-1.6 3 0 6 M20.5 9 q1.6 3 0 6";
        default:
            return "";
        }
    }

    Shape {
        width: root.grid
        height: root.grid
        scale: Math.min(root.width, root.height) / root.grid
        transformOrigin: Item.TopLeft
        x: (root.width - root.grid * scale) / 2
        y: (root.height - root.grid * scale) / 2
        preferredRendererType: Shape.CurveRenderer
        transform: Scale {
            origin.x: root.grid / 2
            xScale: root.mirrorsInRtl && root.layoutMirrored ? -1 : 1
        }

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: root.pathFor(root.name)
            }
        }
    }
}
