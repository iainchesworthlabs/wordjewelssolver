// SPDX-License-Identifier: GPL-3.0-or-later
// TEST SUPPORT, not part of the application: the board scene the Quick Test
// suites give the Solo Play screen in place of the 3D one.
//
// The application's WordJewelsSolver.Scene (apps/app/qml/scene) draws a View3D and
// needs a display and Qt Quick 3D, so no suite can start it. This stub is the
// same component under the same interface - the one apps/app/tests/unit/
// check_scene_contract.cmake holds the 3D scene and the 2D fallback to:
//
//   prepare(board)          what a shaking screen asks for a frame ahead
//   play(board, seed)       the shake and the drop; the test reads what arrived
//   show(board)             a board already down, or the closed box (no tiles)
//   pushMotion(...)         one sample of the device; recorded, never acted on
//   settled()               once for each play
//   landed(index, pitch)    once for each die; the stub's play lands them all
//   rattled()               the box rattles
//   busy, boardRect, tileSize, tileGap, overdraw
//
// It draws nothing and knows no dice: a play is recorded in `plays`, then
// `settled` after settleMs so the screen's own flow continues. The geometry
// rule is the same one BoardGeometry (SoloLayout, apps/app/src/solo/board_layout.*)
// applies, repeated in JavaScript because this module must not import the
// module under test: the module under test imports this one.
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    // For findChild() in a suite: there is one scene in a screen.
    objectName: "boardSceneStub"

    property bool reducedMotion: false

    // The colours the screen hands the real scene (Theme's tray, gold, die and lid
    // tokens), so the screen's bindings have something to bind to.
    property color trayColor: "transparent"
    property color rimColor: "transparent"
    property color dieColor: "transparent"
    property color dieEdgeColor: "transparent"
    property color dieInkColor: "transparent"
    property color jewelColor: "transparent"
    property color lidColor: "transparent"
    property real trayInset: 0

    signal settled
    signal landed(int index, real pitch)
    signal rattled

    // --- what a test reads -------------------------------------------------
    // One { board, seed } a play() was given, and the boards prepare/show saw.
    property var plays: []
    property var preparedBoards: []
    property var shownBoards: []
    property var motions: []
    property int playCount: 0
    property int prepareCount: 0
    property int showCount: 0
    property int settleMs: 1

    // --- the interface -----------------------------------------------------
    readonly property bool busy: playing
    readonly property int rows: boardRows
    readonly property int columns: boardColumns
    readonly property int cellsAcross: Math.max(boardRows, boardColumns, 1)
    readonly property real tileGap: Math.min(width, height) < 600 ? 6 : 8
    readonly property real tileSize: Math.max(
        1, (Math.min(width, height) - 2 * inset - (cellsAcross - 1) * tileGap) / cellsAcross)
    readonly property real overdraw: 0
    readonly property rect boardRect: Qt.rect((width - spanX) / 2, (height - spanY) / 2, spanX, spanY)

    readonly property real inset: Math.min(width, height) < 600 ? 12 : 24
    readonly property real spanX: boardColumns * tileSize
                                      + Math.max(0, boardColumns - 1) * tileGap
    readonly property real spanY: boardRows * tileSize + Math.max(0, boardRows - 1) * tileGap

    property int boardRows: 4
    property int boardColumns: 4
    property var boardTiles: []
    property bool playing: false

    function prepare(board) {
        preparedBoards = preparedBoards.concat([board]);
        ++prepareCount;
    }

    function play(board, seed) {
        if (board !== null && board !== undefined) {
            boardRows = board.rows;
            boardColumns = board.columns;
            boardTiles = board.tiles;
        }
        plays = plays.concat([{ board: board, seed: seed }]);
        ++playCount;
        playing = true;
        landedCount = 0;
        landedTimer.restart();
        settleTimer.restart();
    }

    function show(board) {
        if (board !== null && board !== undefined) {
            boardRows = board.rows;
            boardColumns = board.columns;
            boardTiles = board.tiles;
        }
        shownBoards = shownBoards.concat([board]);
        ++showCount;
        playing = false;
        settleTimer.stop();
        landedTimer.stop();
    }

    function pushMotion(ax, ay, az, wx, wy, wz) {
        motions = motions.concat([{ ax: ax, ay: ay, az: az, wx: wx, wy: wy, wz: wz }]);
    }

    // A die lands: the test does not need the order, only that the screen's
    // `landed` wiring has something to receive, so every tile lands before the
    // settle, one timer tick apart.
    property int landedCount: 0

    Timer {
        id: landedTimer
        interval: 0
        repeat: true
        onTriggered: {
            if (root.landedCount >= root.boardRows * root.boardColumns) {
                landedTimer.stop();
                return;
            }
            root.landed(root.landedCount, 1.0);
            ++root.landedCount;
        }
    }

    Timer {
        id: settleTimer
        interval: root.settleMs
        repeat: false
        onTriggered: {
            root.playing = false;
            root.settled();
        }
    }
}
