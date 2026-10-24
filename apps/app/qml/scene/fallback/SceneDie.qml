// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import WordJewelsSolver

// One die of the 2D placeholder scene: a square with square corners, a cream face,
// a dark letter and a dark edge (docs/design/board.md, "The tile"). A dead jewel is
// a faceted shape with no letter. Only x, y, rotation, scale and opacity are
// animated by the scene; there is no shader and no effect layer.
Item {
    id: root

    property string letters: ""
    property real shadow: 0

    readonly property bool dead: letters === ""

    // The flat ellipse under the die, which grows as the die falls.
    Rectangle {
        visible: root.shadow > 0
        width: root.width * (0.5 + 0.5 * root.shadow)
        height: root.height * 0.16
        radius: height / 2
        x: (root.width - width) / 2
        y: root.height - height * 0.5
        color: "#000000"
        opacity: 0.25 * root.shadow
    }

    TileFace {
        visible: !root.dead
        anchors.fill: parent
        letters: root.letters
    }

    Shape {
        visible: root.dead
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.dieEdge
            strokeWidth: Theme.rule
            fillColor: Theme.jewelDead
            joinStyle: ShapePath.RoundJoin
            startX: root.width * 0.5
            startY: root.height * 0.08
            PathLine { x: root.width * 0.92; y: root.height * 0.38 }
            PathLine { x: root.width * 0.74; y: root.height * 0.92 }
            PathLine { x: root.width * 0.26; y: root.height * 0.92 }
            PathLine { x: root.width * 0.08; y: root.height * 0.38 }
            PathLine { x: root.width * 0.5; y: root.height * 0.08 }
        }
        ShapePath {
            strokeColor: Theme.dieEdge
            strokeWidth: Theme.hair
            fillColor: "transparent"
            startX: root.width * 0.08
            startY: root.height * 0.38
            PathLine { x: root.width * 0.92; y: root.height * 0.38 }
            PathMove { x: root.width * 0.34; y: root.height * 0.38 }
            PathLine { x: root.width * 0.5; y: root.height * 0.92 }
            PathLine { x: root.width * 0.66; y: root.height * 0.38 }
        }
    }
}
