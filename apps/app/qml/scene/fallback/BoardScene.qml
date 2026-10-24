// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import WordJewelsSolver
import "SceneScript.js" as Script

// THE board scene (docs/design/board.md, "The shake and the drop"): given a
// finished board and a seed, play the shake and the drop, and say when the dice are
// settled. Nothing else in the application touches it, and no game rule is in it:
// a tile is an opaque string and "" is a dead jewel.
//
// This is the plain 2D implementation of the interface, kept as the fallback if
// D2 goes against Qt Quick 3D (docs/design/shake-prototype.md, "If a 2D
// implementation is swapped in"). It is NOT built: the application's scene is
// ../BoardScene.qml, the 3D one, in the same module. It carries the same name and
// members so a build can pick this file instead, and
// apps/app/tests/unit/check_scene_contract.cmake holds it to the same list.
//
//   play(board, seed)   shake, lid, drop, settle, for board = { rows, columns, tiles,
//                       faces, up }; while busy it restarts
//   show(board)         the board already down, with no motion; no tiles is the box
//                       with its lid on (the box before the first shake, and after
//                       the lid goes back on)
//   prepare(board)      no-op: this scene has no faces to draw ahead of a board
//   reducedMotion       in: the cross-fading variant of every stage
//   pushMotion(ax, ay, az, wx, wy, wz)   in: one sample of the device's motion, the
//                       accelerometer's reading in g (gravity included) and the
//                       gyroscope's in rad/s; it changes how the box moves and how long
//                       it rattles, never where a die lands. This scene reduces it to
//                       two numbers (agitation, how hard the device is shaken now, 0 to
//                       1, and tilt, which way it leans, each axis -1 to 1), which it
//                       also takes directly
//   settled()           once for each play, when the dice have stopped
//   landed(index, pitch) a die has hit the floor: its place in the board, and a pitch
//                       for its click
//   rattled()           the box rattles, at most 8 a second, for the touch feedback
//   busy, rows, columns, tileSize, tileGap, boardRect   read-only: the geometry by the
//                       layout rule, for the 2D layer that traces over the scene
//   overdraw            read-only: 0, this scene draws inside its item
Item {
    id: root

    // --- the seam ------------------------------------------------------------
    property bool reducedMotion: false
    property real agitation: 0
    property vector2d tilt: Qt.vector2d(0, 0)

    // One sample of the device's motion (the interface of the board scene, as the
    // 3D scene takes it): how far the reading is from still is how hard the device
    // is shaken, and where gravity is in it is which way it leans.
    function pushMotion(ax, ay, az, wx, wy, wz) {
        agitation = Math.min(1, Math.sqrt(ax * ax + ay * ay + (az - 1) * (az - 1)) / 1.2);
        tilt = Qt.vector2d(Math.max(-1, Math.min(1, -ax / 0.14)),
                           Math.max(-1, Math.min(1, ay / 0.14)));
    }

    signal settled
    signal landed(int index, real pitch)
    signal rattled

    readonly property bool busy: stage !== "idle"
    readonly property int rows: boardRows
    readonly property int columns: boardColumns
    readonly property real tileSize: tile
    readonly property real tileGap: gap
    readonly property rect boardRect: Qt.rect((width - spanX) / 2, (height - spanY) / 2, spanX, spanY)
    // A 2D scene draws inside its item; the 3D one draws a little past it.
    readonly property real overdraw: 0

    // The 3D scene draws the board's faces a frame ahead; this one has none to
    // draw, so the board is drawn when it arrives, as it always was.
    function prepare(board) {
    }

    function play(board, seed) {
        // The application's seam for the profiling calls is the same for this
        // scene, so a trace says which implementation it is: this one's names
        // are scene2d.*, and it is not built (docs/profiling.md).
        Profiler.begin("scene2d.play");
        setBoard(board);
        script = Script.create(boardRows * boardColumns, seed, reducedMotion);
        landedCount = 0;
        landedMask = [];
        rattleClock = 0;
        stage = "shake";
        stageMs = 0;
        if (reducedMotion) {
            rattled();
        }
        apply();
        Profiler.end();
    }

    function show(board) {
        Profiler.begin("scene2d.show");
        setBoard(board);
        script = null;
        stage = "idle";
        stageMs = 0;
        apply();
        Profiler.end();
    }

    // --- the geometry, by the rule in board_layout.hpp ---------------------------
    readonly property int cellsAcross: Math.max(boardRows, boardColumns, 1)
    readonly property real inset: BoardGeometry.inset(width, height)
    readonly property real gap: BoardGeometry.gap(width, height)
    readonly property real tile: Math.max(1, BoardGeometry.tile(width, height, cellsAcross))
    readonly property real pitch: tile + gap
    readonly property real spanX: boardColumns * tile + (boardColumns - 1) * gap
    readonly property real spanY: boardRows * tile + (boardRows - 1) * gap

    // --- state -----------------------------------------------------------------
    property int boardRows: 4
    property int boardColumns: 4
    property var boardTiles: []
    property string stage: "idle"      // idle | shake | lid | drop | settle
    property real stageMs: 0
    property var script: null
    property var poses: []
    property real boxTilt: 0
    property real lidOpen: 1
    property int landedCount: 0
    property var landedMask: []
    property real rattleClock: 0

    readonly property bool boxClosed: boardTiles.length === 0

    function setBoard(board) {
        boardRows = board.rows;
        boardColumns = board.columns;
        boardTiles = board.tiles;
    }

    function enter(next) {
        stage = next;
        stageMs = 0;
    }

    function step(dtMs) {
        if (stage === "idle" || script === null) {
            return;
        }
        // The frame's script calls: every die's pose, the box's tilt and the lid,
        // as Script.pose and its neighbours work them out (SceneScript.js).
        Profiler.begin("scene2d.step");
        stageMs += Math.min(dtMs, 50);

        if (stage === "shake") {
            const minimum = script.reduced ? Script.stageLength(script, "shake") : Script.SHAKE_MIN;
            const held = !script.reduced && agitation > 0.05 && stageMs < Script.SHAKE_MAX;
            rattleClock += dtMs;
            if (!script.reduced && rattleClock >= Script.RATTLE_GAP) {
                rattleClock = 0;
                rattled();
            }
            if (stageMs >= minimum && !held) {
                enter("lid");
            }
        } else if (stage === "lid") {
            if (stageMs >= Script.stageLength(script, "lid")) {
                enter("drop");
            }
        } else if (stage === "drop") {
            for (let i = 0; i < script.count; ++i) {
                if (landedMask[i] !== true && stageMs >= Script.landingTime(script, i)) {
                    landedMask[i] = true;
                    ++landedCount;
                    landed(i, script.pitch[i]);
                }
            }
            if (stageMs >= Script.stageLength(script, "drop")) {
                enter("settle");
            }
        } else if (stage === "settle") {
            if (stageMs >= Script.stageLength(script, "settle")) {
                stage = "idle";
                stageMs = 0;
                apply();
                settled();
                Profiler.end();
                return;
            }
        }
        apply();
        Profiler.end();
    }

    // The poses of every die for the stage and time we are at.
    function apply() {
        const next = [];
        const count = boardRows * boardColumns;
        for (let i = 0; i < count; ++i) {
            next.push(script === null ? { x: 0, y: 0, tilt: 0, scale: 1, opacity: 1, shadow: 0 }
                                      : Script.pose(script, stage, stageMs, i, tile));
        }
        poses = next;
        boxTilt = script === null ? 0 : Script.boxTilt(script, stage, stageMs, agitation, tilt.x);
        lidOpen = script === null ? (boxClosed ? 0 : 1) : Script.lidOpen(script, stage, stageMs);
    }

    onTileChanged: apply()

    FrameAnimation {
        running: root.busy
        onTriggered: root.step(frameTime * 1000)
    }

    // --- what is drawn -----------------------------------------------------------
    // The box: a dark tray with a rim, which rattles as a whole.
    Item {
        id: box
        x: root.boardRect.x
        y: root.boardRect.y
        width: root.boardRect.width
        height: root.boardRect.height
        rotation: root.boxTilt
        transformOrigin: Item.Center

        Rectangle {
            readonly property real rim: root.inset * 0.6
            x: -rim
            y: -rim
            width: parent.width + 2 * rim
            height: parent.height + 2 * rim
            color: Theme.tray
            border.width: Theme.rule
            border.color: Theme.trayEdge
        }

        // The empty wells of the box with its lid on: a picture of the size chosen.
        Repeater {
            model: root.boxClosed ? root.boardRows * root.boardColumns : 0

            Rectangle {
                required property int index
                x: (index % root.boardColumns) * root.pitch
                y: Math.floor(index / root.boardColumns) * root.pitch
                width: root.tile
                height: root.tile
                color: "transparent"
                border.width: Theme.rule
                border.color: Theme.well
            }
        }

        Repeater {
            model: root.boardRows * root.boardColumns

            SceneDie {
                required property int index
                readonly property var pose: root.poses[index] !== undefined
                                            ? root.poses[index]
                                            : { x: 0, y: 0, tilt: 0, scale: 1, opacity: 1, shadow: 0 }
                x: (index % root.boardColumns) * root.pitch + pose.x
                y: Math.floor(index / root.boardColumns) * root.pitch + pose.y
                width: root.tile
                height: root.tile
                rotation: pose.tilt
                scale: pose.scale
                opacity: root.boxClosed ? 0 : pose.opacity
                letters: root.boardTiles[index] !== undefined ? root.boardTiles[index] : ""
                shadow: pose.shadow
            }
        }

        // The lid: a translucent pane over the box that lifts and tilts away.
        Rectangle {
            readonly property real rim: root.inset * 0.6
            x: -rim
            y: -rim - root.lidOpen * root.tile * 0.4
            width: parent.width + 2 * rim
            height: parent.height + 2 * rim
            visible: root.lidOpen < 1
            color: Theme.lid
            opacity: 0.55 * (1 - root.lidOpen)
            rotation: -6 * root.lidOpen
            border.width: Theme.rule
            border.color: Theme.trayEdge
        }
    }
}
