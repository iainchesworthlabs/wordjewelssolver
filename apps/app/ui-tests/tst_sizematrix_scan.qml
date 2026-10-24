// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver
import WordJewelsSolver.Scan

// The size matrix over Mode A (the scan screen, the check screen and the Family
// Assistant's screen; the two modals, the letter picker and the question about
// ringed tiles, are audited open in tst_scan.qml, because the screen under a
// modal cannot be audited with the modal on top of it): 900x620, 900x1200, 1024x768,
// 2400x1200 and 150% text. Every interactive item is at least 48 dp each way,
// wholly inside the window and its clipping parents, with no truncated label and
// no two controls on top of each other.
//
// The screens need their models in a state, so each subject is a holder that
// resets the fixture and puts the models there before the screen is made. TEST
// DATA: the board is SAT across the top and PIN below it.
SizeMatrixCase {
    id: matrix

    name: "SizeMatrixScan"
    subjects: [readyComponent, cameraComponent, typedComponent, readComponent, assistComponent,
               assistFoundComponent]

    readonly property var board: ["S", "A", "T", "X", "P", "I", "N", "X",
                                  "X", "X", "X", "X", "X", "X", "X", "X"]

    // scan.ready, no camera: the picture, the message and "Try camera again".
    Component {
        id: readyComponent
        Item {
            readonly property bool prepared: { fixture.reset(); return true }
            ScanScreen { anchors.fill: parent; scan: scanModel; assist: assistModel }
        }
    }

    // scan.ready with a camera running: Capture and the typed board.
    Component {
        id: cameraComponent
        Item {
            readonly property bool prepared: { fixture.reset(); fixture.useCamera(); return true }
            ScanScreen { anchors.fill: parent; scan: scanModel; assist: assistModel }
        }
    }

    // scan.check on a typed 4x4, every tile typed: the board size, the actions.
    Component {
        id: typedComponent
        Item {
            readonly property bool prepared: {
                fixture.reset()
                scanModel.typeBoard()
                for (let i = 0; i < 16; ++i) {
                    scanModel.setTile(i, matrix.board[i])
                }
                return true
            }
            ScanScreen { anchors.fill: parent; scan: scanModel; assist: assistModel }
        }
    }

    // scan.check on a camera board with doubts: Turn, the ringed tiles, the hint.
    Component {
        id: readComponent
        Item {
            readonly property bool prepared: { fixture.reset(); fixture.readDoubtfulBoard(); return true }
            ScanScreen { anchors.fill: parent; scan: scanModel; assist: assistModel }
        }
    }

    // assist.playing with nothing found yet: the empty card, the help, New board.
    Component {
        id: assistComponent
        Item {
            readonly property bool prepared: { fixture.reset(); assistModel.begin(4, 4, matrix.board); return true }
            AssistScreen { anchors.fill: parent; assist: assistModel }
        }
    }

    // assist.playing with a word found: its row in the card.
    Component {
        id: assistFoundComponent
        Item {
            readonly property bool prepared: {
                fixture.reset()
                assistModel.begin(4, 4, matrix.board)
                assistModel.tapTile(0)
                assistModel.tapTile(1)
                assistModel.tapTile(2)
                assistModel.done()
                return true
            }
            AssistScreen { anchors.fill: parent; assist: assistModel }
        }
    }
}
