import QtQuick
import QtQuick.Shapes

// The faces of one board as one texture: a grid of cells, one for each tile,
// laid out as the board is, so the tile at row r and column c is the cell at
// column c and row r. Cream face, dark letter, dark edge (docs/design/board.md,
// "The tile"). Every die samples its own cell, and the atlas is drawn once for
// a board, not once for a die.
//
// A dead jewel is a cell with no letters, painted as four facets of a cut
// stone seen from above, so it reads as a jewel and not as an empty tile.
Item {
    id: atlas

    // One string for each tile, row-major: "A", "QU", "SH", or "" for a dead jewel.
    property var tiles: []
    property int rows: 1
    property int columns: 1

    readonly property int cell: 128
    readonly property color faceColor: "#F4E9CC"
    readonly property color edgeColor: "#2B2622"
    readonly property color inkColor: "#1F1B18"

    width: columns * cell
    height: rows * cell

    Repeater {
        model: atlas.tiles.length

        Rectangle {
            id: face

            required property int index
            readonly property string letters: String(atlas.tiles[index]).toUpperCase()

            x: (index % atlas.columns) * atlas.cell
            y: Math.floor(index / atlas.columns) * atlas.cell
            width: atlas.cell
            height: atlas.cell
            color: face.letters === "" ? "#2E9F92" : atlas.faceColor
            border.color: atlas.edgeColor
            border.width: 10

            // The four facets, from the middle of the cell to each edge.
            Shape {
                anchors.fill: parent
                visible: face.letters === ""

                ShapePath {
                    fillColor: "#63D6C7"
                    strokeColor: "transparent"
                    startX: 64; startY: 64
                    PathLine { x: 10; y: 10 }
                    PathLine { x: 118; y: 10 }
                    PathLine { x: 64; y: 64 }
                }
                ShapePath {
                    fillColor: "#2E9F92"
                    strokeColor: "transparent"
                    startX: 64; startY: 64
                    PathLine { x: 118; y: 10 }
                    PathLine { x: 118; y: 118 }
                    PathLine { x: 64; y: 64 }
                }
                ShapePath {
                    fillColor: "#1B6A61"
                    strokeColor: "transparent"
                    startX: 64; startY: 64
                    PathLine { x: 118; y: 118 }
                    PathLine { x: 10; y: 118 }
                    PathLine { x: 64; y: 64 }
                }
                ShapePath {
                    fillColor: "#46B8A9"
                    strokeColor: "transparent"
                    startX: 64; startY: 64
                    PathLine { x: 10; y: 118 }
                    PathLine { x: 10; y: 10 }
                    PathLine { x: 64; y: 64 }
                }
            }

            Text {
                anchors.centerIn: parent
                text: face.letters
                color: atlas.inkColor
                font.bold: true
                // About 55% of the tile for one letter; a two-letter tile (the
                // Qu face) at 80% of that, a three-letter tile smaller still.
                font.pixelSize: face.letters.length > 2 ? 48 : (face.letters.length === 2 ? 68 : 88)
            }
        }
    }
}
