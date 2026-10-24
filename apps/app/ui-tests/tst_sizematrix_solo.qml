// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver

// The size matrix over Solo Play's screens: the box, a board in play with a path
// in hand, the summary sheet and the boards sheet, at 900x620, 900x1200, 1024x768,
// 2400x1200 and 150% text. Every interactive item is at least 48 dp each way,
// wholly inside the window (or inside what scrolls), carries no truncated label
// and lies on no other control (apps/app/ui-tests/SizeMatrix.js).
//
// Each subject is a host that makes a fresh game first (the model is the
// application's own, over the fake game), then the screen over it, then puts the
// screen where the subject is: a shake dealt, a path tapped, the board finished.
SizeMatrixCase {
    name: "SizeMatrixSolo"
    // The shake, the drop and the panel's slide-in take a moment to be over.
    settle: 900
    subjects: [boxComponent, playComponent, summaryComponent, historyComponent]

    Component {
        id: boxComponent
        Item {
            id: host
            readonly property var game: { fixture.reset(); return soloModel }
            SoloScreen {
                anchors.fill: parent
                solo: host.game
            }
        }
    }

    Component {
        id: playComponent
        Item {
            id: host
            readonly property var game: { fixture.reset(); return soloModel }
            SoloScreen {
                anchors.fill: parent
                solo: host.game
            }
            Timer {
                interval: 50
                running: true
                onTriggered: host.game.shake()
            }
            // A path in hand: the word and its line show, Clear and Done are live.
            Timer {
                interval: 600
                running: true
                onTriggered: {
                    host.game.tapTile(2)
                    host.game.tapTile(5)
                }
            }
        }
    }

    Component {
        id: summaryComponent
        Item {
            id: host
            readonly property var game: { fixture.reset(); return soloModel }
            SoloScreen {
                id: screen
                anchors.fill: parent
                solo: host.game
            }
            Timer {
                interval: 50
                running: true
                onTriggered: host.game.shake()
            }
            Timer {
                interval: 500
                running: true
                onTriggered: screen.finishBoard()
            }
        }
    }

    Component {
        id: historyComponent
        Item {
            id: host
            readonly property var game: { fixture.reset(); return soloModel }
            HistorySheet {
                anchors.fill: parent
                solo: host.game
                open: true
            }
        }
    }
}
