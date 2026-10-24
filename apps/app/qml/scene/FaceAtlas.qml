// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import "ShakeScript.js" as Script

// The faces of one board as one texture: for each die, a block of 3 x 2 cells,
// one cell for each of its six faces, laid out as the board is, so the die on
// the tile at row r and column c is the block at column c and row r. Side k of
// the cube is the cell at column k % 3 and row floor(k / 3) of its block (the
// mesh and ShakeScript.js's `sides` say the same). Cream face, dark letter,
// dark edge (docs/design/board.md, "The tile"), drawn the way TileFace.qml
// draws the flat board's tile: a rounded cream face, a thin dark edge, the top
// catching the light and the foot in its shadow, an Andika Bold letter at the
// same share of the tile. Every die samples its own block, and the atlas is
// drawn once for a board, not once for a die.
//
// The scene is a QML module of its own, and the prototype harness links it
// without the application's WordJewelsSolver module, so it cannot import Theme.
// The palette below is Theme.die, Theme.dieEdge, Theme.dieInk, Theme.jewelDead
// and Theme.gold, copied exactly; if Theme changes, these change with it.
//
// A cell is 128 pixels. DieGeometry reads a die's flat face from the cell's
// inner 108 (kBorder = 10 / 128 in die_geometry.cpp) and samples its bevels and
// corners at 0.03, in the outer 10. The tile is therefore drawn in that
// 108-pixel face, with the edge thin as the flat board's, and the border
// outside it is cream: the cube's bevel reads as the rounded edge of the paper
// tile, not as a heavy dark frame. The letter is sized for the die's whole
// edge (glyphScale), so at rest it is the 55% of the tile that TileFace's is.
Item {
    id: atlas

    // For each tile, row-major, the six strings of its die: "A", "QU", "SH",
    // or "" for a jewel. Set with fill(), which makes the cells of the atlas
    // from them in one pass.
    property var faces: []
    property int rows: 1
    property int columns: 1

    readonly property int cell: 128
    readonly property int blockColumns: 3
    readonly property int blockRows: 2
    readonly property int facesPerDie: blockColumns * blockRows
    // The part of a cell the cube's flat face shows (DieGeometry's kBorder),
    // and the cream tile drawn in it.
    readonly property int faceInset: 10
    readonly property int faceCell: cell - 2 * faceInset
    readonly property int faceRadius: 10
    readonly property int faceEdge: 2

    // The same cream, dark-teal edge and ink as TileFace.qml, and the dead
    // jewel and gold of Jewel.qml: the application's Theme tokens (Theme.die,
    // dieEdge, dieInk, jewelDead, gold), handed in by the scene, which gets them
    // from the screen. The values here are the same tokens' own, for a harness
    // that does not pass them.
    property color faceColor: "#FFF7E3"
    property color edgeColor: "#20323A"
    property color inkColor: "#1F2A30"
    property color jewelColor: "#7A8DA6"
    property color goldColor: "#BF8019"

    // Theme.letterShare and Theme.quShare: the letter is this share of the
    // tile, and Qu this share of that.
    readonly property real letterShare: 0.55
    readonly property real quShare: 0.78
    // DieGeometry's bevel is 0.07 of the edge, so a die's flat face is 0.86 of
    // it. ShakeScript.atlasCells sizes a one-letter glyph at 88 cell pixels of
    // a full 128 cell; the flat face shows 108 of those and covers 0.86 of the
    // die, so a glyph at 55% of the die's edge is 88 * glyphScale here. At rest
    // one scene unit is one pixel, so the letter on a die is then exactly the
    // flat board's 55%. The script's own relative sizes carry the longer faces;
    // Qu, which TileFace draws at quShare of the letter share, is exactly that.
    readonly property real bevelShare: 0.07
    readonly property real flatShare: 1 - 2 * bevelShare
    readonly property real glyphBase: 88
    readonly property real glyphUnit: letterShare * faceCell / flatShare
    readonly property real glyphScale: glyphUnit / glyphBase

    width: columns * blockColumns * cell
    height: rows * blockRows * cell

    // Where each cell is and what is on it, made by fill(): the cells with
    // letters ({ x, y, letters, pixelSize }) and the jewels ({ x, y }). The
    // items below show them by their place in these lists.
    property var letterCells: []
    property var jewelCells: []
    // How many of each item there are. They are made as a board needs them and
    // then kept: a smaller board hides the ones it does not use, and a board
    // of a size that has been shown makes none. Making them was most of what a
    // board that changed the atlas's size cost the GUI thread.
    property int letterPool: 0
    property int jewelPool: 0

    // Sets the size and the faces, and makes the cells of the atlas. The cells
    // themselves (their place and their letters) are the scene script's
    // (Script.atlasCells), so what the work budget counts is what is drawn.
    function fill(rowCount, columnCount, tileFaces) {
        const cells = Script.atlasCells(columnCount, tileFaces, cell, blockColumns, blockRows);
        rows = rowCount;
        columns = columnCount;
        faces = tileFaces;
        letterCells = cells.letters;
        jewelCells = cells.jewels;
        letterPool = Math.max(letterPool, letterCells.length);
        jewelPool = Math.max(jewelPool, jewelCells.length);
    }

    // The cream face, dark edge and dark letter of every cell that has letters.
    Repeater {
        model: atlas.letterPool

        Rectangle {
            id: cellFace
            required property int index
            readonly property var place: atlas.letterCells[index]
            // Qu is shown as the child reads it on the flat board (TileFace).
            readonly property string shown: place === undefined ? ""
                : (place.letters === "QU" ? "Qu" : place.letters)

            visible: place !== undefined
            x: place ? place.x : 0
            y: place ? place.y : 0
            width: atlas.cell
            height: atlas.cell
            // The outer border is what the cube's bevels sample; cream makes
            // them the rolled edge of the paper tile, not its dark frame.
            color: atlas.faceColor

            // The face itself, inset so that the flat face of the cube sees all
            // of it: TileFace's cream rectangle with its thin dark edge, but
            // drawn in the cell's inner 108 pixels.
            Rectangle {
                x: atlas.faceInset
                y: atlas.faceInset
                width: atlas.faceCell
                height: atlas.faceCell
                radius: atlas.faceRadius
                color: atlas.faceColor
                border.width: atlas.faceEdge
                border.color: atlas.edgeColor

                // The top catches the light and the foot sits in its
                // shadow, as on the flat tile (TileFace.qml).
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 2
                    height: 2
                    radius: 1
                    color: "#FFFFFF"
                    opacity: 0.55
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 2
                    height: 3
                    radius: 1
                    color: "#000000"
                    opacity: 0.12
                }

                Text {
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: cellFace.shown
                    color: atlas.inkColor
                    font.family: "Andika"
                    font.weight: Font.Bold
                    font.pixelSize: cellFace.place === undefined ? atlas.glyphUnit
                        : (cellFace.place.letters === "QU"
                           ? atlas.glyphUnit * atlas.quShare
                           : cellFace.place.pixelSize * atlas.glyphScale)
                }
            }
        }
    }

    // The jewel, shown from the one texture it is drawn into below.
    Repeater {
        model: atlas.jewelPool

        ShaderEffect {
            required property int index
            readonly property var place: atlas.jewelCells[index]
            // The effect's own shaders show its `source`.
            property variant source: jewelTexture

            visible: place !== undefined
            x: place ? place.x : 0
            y: place ? place.y : 0
            width: atlas.cell
            height: atlas.cell
            blending: false
        }
    }

    // The jewel is drawn once, into a texture of one cell, and every jewel cell
    // shows that texture: a Shape of four paths for each of six cells of each
    // dead die cost about 0.6 ms a cell (docs/design/shake-prototype.md). The
    // texture is not live: it is made on the first frame and kept. It sits one
    // cell outside the atlas, so that the atlas does not show it.
    Rectangle {
        id: jewel

        width: atlas.cell
        height: atlas.cell
        color: atlas.faceColor

        Rectangle {
            x: atlas.faceInset
            y: atlas.faceInset
            width: atlas.faceCell
            height: atlas.faceCell
            radius: atlas.faceRadius
            color: atlas.faceColor
            border.width: atlas.faceEdge
            border.color: atlas.edgeColor

            // The dead jewel is the same cut stone the tray shows: slate
            // facets, a gold outline and a small table, on a cream die.
            Item {
                id: gem
                anchors.centerIn: parent
                // 58% of the die, as FlatBoard's Jewel is of its tile.
                width: 73
                height: 73

                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        strokeColor: atlas.goldColor
                        strokeWidth: 2
                        fillColor: atlas.jewelColor
                        joinStyle: ShapePath.RoundJoin
                        startX: gem.width * 0.5
                        startY: gem.height * 0.04
                        PathLine { x: gem.width * 0.94; y: gem.height * 0.36 }
                        PathLine { x: gem.width * 0.74; y: gem.height * 0.94 }
                        PathLine { x: gem.width * 0.26; y: gem.height * 0.94 }
                        PathLine { x: gem.width * 0.06; y: gem.height * 0.36 }
                        PathLine { x: gem.width * 0.5; y: gem.height * 0.04 }
                    }
                    ShapePath {
                        strokeColor: atlas.edgeColor
                        strokeWidth: 1
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        startX: gem.width * 0.06
                        startY: gem.height * 0.36
                        PathLine { x: gem.width * 0.94; y: gem.height * 0.36 }
                        PathMove { x: gem.width * 0.34; y: gem.height * 0.36 }
                        PathLine { x: gem.width * 0.5; y: gem.height * 0.94 }
                        PathLine { x: gem.width * 0.66; y: gem.height * 0.36 }
                    }
                }
            }
        }
    }

    ShaderEffectSource {
        id: jewelTexture

        x: -atlas.cell
        width: atlas.cell
        height: atlas.cell
        sourceItem: jewel
        hideSource: true
        live: false
    }
}
