// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import WordJewelsSolver

// The picture shown in the viewfinder's place when the device has no camera: a
// camera with a stroke through it, drawn on the tray in the dice's cream. It says
// "no camera" without a sentence, so the words under the frame (and the typed
// board, which is the way on) are a caption to it and not the whole message.
// Vector paths on a 96 x 72 grid, scaled to whatever room the item is given.
Item {
    id: root

    property color color: Theme.die

    readonly property real gridWidth: 96
    readonly property real gridHeight: 72

    implicitWidth: gridWidth
    implicitHeight: gridHeight
    Accessible.ignored: true

    Shape {
        width: root.gridWidth
        height: root.gridHeight
        scale: Math.min(root.width / root.gridWidth, root.height / root.gridHeight)
        transformOrigin: Item.TopLeft
        x: (root.width - root.gridWidth * scale) / 2
        y: (root.height - root.gridHeight * scale) / 2
        preferredRendererType: Shape.CurveRenderer

        // The body, with the bump of the viewfinder on top, and the lens.
        ShapePath {
            strokeColor: root.color
            strokeWidth: 4
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: "M12 16 H30 L34 10 H62 L66 16 H84 A6 6 0 0 1 90 22 V58 A6 6 0 0 1 84 64 H12 "
                      + "A6 6 0 0 1 6 58 V22 A6 6 0 0 1 12 16 Z M48 27 a13 13 0 1 1 0 26 a13 13 0 1 1 0 -26"
            }
        }
        // The stroke through it: a wide band of the tray's own colour first, so the
        // line stands clear of the lines it crosses.
        ShapePath {
            strokeColor: Theme.tray
            strokeWidth: 12
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathSvg {
                path: "M14 66 L82 6"
            }
        }
        ShapePath {
            strokeColor: root.color
            strokeWidth: 4
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathSvg {
                path: "M14 66 L82 6"
            }
        }
    }
}
