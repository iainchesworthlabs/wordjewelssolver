// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import "ShakeScript.js" as Script

// The board scene of Solo Play (docs/design/board.md, "The shake and the
// drop"): the one component the application puts the shake and the drop in, in
// the QML module WordJewelsSolver.Scene. SoloScreen uses it; the prototype
// harness (apps/app/src/prototype/shake) drives the same file, so what it measures is
// what the application plays. A 2D implementation of the same interface is kept
// in fallback/BoardScene.qml for D2 and is not built.
//
// What it is given: a finished board, the dice it was rolled with, and a seed.
// What it does: plays the shake, the lid and the drop, and says when the dice
// are settled. It decides nothing about the game: a face is an opaque string to
// it, "" is a jewel, and the letters on the dice are the ones it was handed.
// Nothing outside this file touches the scene, and everything it needs is in
// this file's interface:
//
//   in    play(board, seed), show(board), prepare(board), reducedMotion,
//         pushMotion(...)
//   out   settled(), landed(index, pitch), rattled(), busy, and the geometry the
//         2D layer over it lays out by: boardRect, tileSize, tileGap, and how
//         far past its item the scene draws: overdraw
//
// board is { rows, columns, tiles, faces, up }, each of the three arrays with an
// entry for each tile, row-major: tiles[t] is the letters of the finished
// board's tile t, faces[t] the six strings on the die that is on it ("" is the
// jewel, and a dead jewel is a die of six), and up[t] which of the six is up, so
// that faces[t][up[t]] === tiles[t]. The scene draws each die with its six
// faces, shows the others as it tumbles, and lands it with the up face on top,
// the letter upright. It reads no letter and decides no face: they are the
// library's roll (wjs::roll_with_placements), passed through. A board with no
// faces (the phonics board, a game brought back from the store) is drawn with
// the tile's letters on all six sides, as the first scene did.
//
// An empty board is the closed box: no dice and the lid on (solo.box before the
// first shake, and after the lid goes back on).
//
// The dice are simulated, from first principles (apps/app/src/scene/sim): sixteen
// rigid cubes in a box with a floor, walls and a lid, accelerated by the
// negative of what the device's accelerometer reads and by the turning of its
// gyroscope, colliding, sliding and tumbling by the equations of motion, with
// the same step and the same arithmetic each time, so a replay of a board, a
// seed and a motion is the same shake (digest() shows it). When the shaking is
// over each die is sent to a cell of the board, hopping and sliding there past
// the others: the physics decides how it looks, never where a die ends, nor
// which of its faces is up. ShakeScript.js holds the clock and the stages and
// has no renderer in it, so a 2D scene would draw the same script. This file
// turns the script's poses into Qt Quick 3D nodes. The rattle answers the
// device's motion sample for sample: pushMotion() takes the accelerometer's
// reading and the gyroscope's, about a hundred a second. With no device (the
// Shake button, a test) a seeded stand-in makes the stream.
//
// The View3D is made once and kept: a new board is a new set of dice inside it.
// The application makes this scene behind home, not on the Shake press, and
// calls prepare() with a board a frame or more before play() or show() of it.
Item {
    id: root

    // ---- The seam: in ------------------------------------------------------
    // Cross-fades instead of moving (docs/design/application.md, "Motion").
    property bool reducedMotion: false

    // One sample of the device's motion: the accelerometer's reading in g,
    // gravity included (a device lying face up reads 0, 0, 1; x to the right,
    // y up the screen, z out of the screen), and the gyroscope's in rad/s.
    // Call it for every sample the sensors give, about a hundred a second.
    function pushMotion(ax, ay, az, wx, wy, wz) {
        Script.feed(tracker, ax, ay, az, wx, wy, wz);
        lastMotionMs = Date.now();
    }

    // ---- The seam: colours -------------------------------------------------
    // The Jewel Box's tokens (Theme.tray, gold, die, dieEdge, dieInk, jewelDead,
    // lid), which the screen passes in: this module cannot import the
    // application's Theme, and the tray, rim and dice of the 3D scene have to be
    // the flat board's own colours or the cross-fade between them changes colour.
    // The defaults are the same values, for the prototype harness.
    property color trayColor: "#1B3A44"
    property color rimColor: "#BF8019"
    property color dieColor: "#FFF7E3"
    property color dieEdgeColor: "#20323A"
    property color dieInkColor: "#1F2A30"
    property color jewelColor: "#7A8DA6"
    property color lidColor: "#F7E8C6"
    // How far the settled board's tray stands off the tiles: the flat board's
    // own outset (Theme.trayOutset with the area's inset), so the two trays are
    // one. Alone it is the token's wish, capped to the inset.
    property real trayInset: Math.max(0, Math.min(Math.max(10, tile * 0.08), inset - 6))

    // ---- The seam: out -----------------------------------------------------
    // The dice have stopped. Emitted once, at the end of the settle.
    signal settled()
    // A die has hit the floor: its index in the board, and a pitch to play its
    // click at (0.9 to 1.15, a little different for each die).
    signal landed(int index, real pitch)
    // The box rattles: at most 8 a second, for the haptic tap.
    signal rattled()

    readonly property bool busy: stage !== "idle"
    // The board's size and square, in this item's own coordinates, by the rule
    // in docs/design/board.md ("The rule"): the largest square that fits less an
    // inset, tiles a gap apart. The 2D layer for tracing is laid out by these.
    readonly property int rows: boardRows
    readonly property int columns: boardColumns
    readonly property real tileSize: tile
    readonly property real tileGap: gap
    readonly property rect boardRect: Qt.rect((width - spanX) / 2, (height - spanY) / 2, spanX, spanY)
    // How far past this item's edges the scene draws, on every side: a die that
    // tumbles at the edge of the board, and the box's rim as it tips, are not
    // cut off. The item is still the board's area and boardRect is still by the
    // rule; the application only has to leave the item unclipped (no `clip`).
    readonly property real overdraw: tile * 0.8

    // Plays the shake, the lid and the drop for a board. The dice are in the
    // box before the first frame, and `settled` follows the last of them.
    function play(board, seed) {
        frames.running = false;
        const shown = sceneBoard(board);
        if (shown.tiles.length === 0) {
            // No board to play: the closed box, as show() draws it.
            show(shown);
            return;
        }
        // The frame of the press: the board is set, the script is made and every
        // die is posed once before the first frame of the play.
        Profiler.begin("scene.play");
        // A device that has sent a sample in the last 300 ms is the shake; if
        // none has, the stand-in shakes (a button, a test, a replay).
        const live = Date.now() - lastMotionMs < 300;
        if (!live) {
            Script.resetTracker(tracker);
        }
        begin(shown, seed, !live);
        stage = current.stage;
        apply();
        frames.running = true;
        Profiler.end();
    }

    // Draws a board's faces ahead of play() or show() of that board, so that
    // the frame in which the board arrives has less to do: they are one
    // texture of up to 216 cells, which the frame would make and draw
    // (docs/design/shake-prototype.md, "The real faces"). It changes nothing on
    // screen: the dice keep the faces of the board they
    // show until play() or show() is given one, and a board that was not
    // prepared is drawn when it arrives, as it always was. Call it with the
    // board you are about to play a frame or more before, where one frame a
    // little longer is not seen (behind the layout stage, a transition). A
    // second call for another board replaces the one prepared. It holds a
    // second atlas (a texture the size of the board's, 14 MB at 6 x 6).
    function prepare(board) {
        const shown = sceneBoard(board);
        if (shown.tiles.length === 0) {
            return;
        }
        const key = facesKey(shown);
        if (key === atlasKey || key === spareKey) {
            return;
        }
        // The atlas build: one texture of six cells a die, drawn ahead of the
        // board it belongs to. The value is the cells, the faces it drew.
        Profiler.begin("scene.prepare");
        fillAtlas(spareAtlas, shown);
        spareKey = key;
        Profiler.value(shown.rows * shown.columns * 6);
        Profiler.end();
    }

    // Shows a board already dropped: no motion. For a game that is resumed, or
    // a window that is rotated. An empty board is the closed box.
    function show(board) {
        Profiler.begin("scene.show");
        frames.running = false;
        const shown = setBoard(board);
        if (shown.tiles.length === 0) {
            current = null;
            stage = "idle";
            apply();
            Profiler.end();
            return;
        }
        begin(shown, 0);
        Script.finish(current);
        if (warmUp && !warmedOnce) {
            warmedOnce = true;
            warmFrames = 3;
            frames.running = true;
        }
        apply();
        stage = "idle";
        Profiler.end();
    }

    // A checksum of every pose the board and seed produce, frame by frame at
    // 60 Hz with the device still. Two plays of the same board and seed give
    // the same string; it is how a replay is tested.
    function digest(board, seed) {
        return Script.digest(sceneBoard(board), seed, layout());
    }

    // Every die's pose at every frame of a play, as text (see ShakeScript.trace).
    function trace(board, seed) {
        return Script.trace(sceneBoard(board), seed, layout());
    }

    // What a play of the board and seed does to the faces, device still: how
    // many dice end with their up face on top, and how many different faces each
    // die showed uppermost on the way.
    function faceReport(board, seed) {
        return Script.faceReport(sceneBoard(board), seed, layout());
    }

    // Puts the scene where the script for a board and seed is `ms` into the
    // play, with the device still, and holds it there. For tests and for
    // pictures: the same board, seed and time always give the same picture.
    function seek(board, seed, ms) {
        frames.running = false;
        const shown = setBoard(board);
        current = Script.at(shown, seed, layout(), ms);
        stage = current.stage;
        apply();
    }

    // ---- Prototype settings, not part of the seam --------------------------
    // How far the camera leans from straight down while the dice are in the
    // air, in degrees. It comes upright as they settle, so the 2D layer lines up.
    property real lean: 0
    // "none", "procedural" (Qt's sky, prefiltered on the first frame) or
    // "baked" (the same, prefiltered in advance and read from a file). None by
    // default: the sky is blue, and it tinted the cream dice grey-blue. The
    // dice are lit by the warm lights below and by their own face map instead.
    // The probe stays for the prototype's comparison.
    property string probeMode: "none"
    property real cameraDistance: 2.2   // times the board's side
    // Simulated dice: rigid cubes thrown about the box by the device's motion
    // that collide and then go to their cells (ShakeScript.js, DiceSim). False
    // is the prescribed curves.
    property bool weighted: true
    // How long the stand-in's burst of shaking lasts, when there is no device:
    // 0 takes 0.7 to 1.0 s from the seed.
    property real mockBurstMs: 0
    // While the first board is shown, draw the lid, the shadows and the dice
    // through their blended paths for three frames at 2% opacity (Qt skips
    // anything under 1%), so the first shake does not pay for creating them.
    property bool warmUp: true

    // ---- The board and the layout -----------------------------------------
    property int boardRows: 4
    property int boardColumns: 4
    // The six faces of the die on each tile, and which is up (see above). Empty
    // when there is no board: the closed box.
    property var boardFaces: []
    property var boardUp: []
    readonly property bool boxClosed: boardFaces.length === 0

    readonly property real shortSide: Math.max(1, Math.min(width, height))
    readonly property real inset: shortSide < 600 ? 12 : 24
    readonly property real gap: shortSide < 600 ? 6 : 8
    readonly property int cellsAcross: Math.max(boardRows, boardColumns, 1)
    readonly property real tile: Math.max(1, (shortSide - 2 * inset - (cellsAcross - 1) * gap) / cellsAcross)
    readonly property real pitch: tile + gap
    readonly property real spanX: boardColumns * tile + (boardColumns - 1) * gap
    readonly property real spanY: boardRows * tile + (boardRows - 1) * gap
    readonly property real rimWidth: inset * 0.7
    // The box's cavity is roomier than the board, so that dice have room to
    // roll over and tumble when it is shaken, and taller. The camera stands back
    // while it is shaken so that the whole box is in the item, and comes in as
    // the dice go home.
    readonly property real slackTiles: weighted ? 1.4 : 0.6
    readonly property real lidTiles: weighted ? 3.0 : 2.45
    readonly property real slack: tile * slackTiles
    readonly property real boxX: spanX + 2 * slack
    readonly property real boxY: spanY + 2 * slack
    readonly property real boxDepth: tile * 0.34
    readonly property real lidHeight: tile * lidTiles
    // How much smaller the lid is made than the box so that it projects to the
    // same size: the camera's distance to the lid over its distance to the rim.
    readonly property real lidFit: Math.min(1, Math.max(0.5, (cameraRig.position.z - lidHeight)
                                                        / Math.max(1, cameraRig.position.z)))

    // ---- The play in progress ---------------------------------------------
    property string stage: "idle"
    // The script being played (see script()), or null before the first board.
    property var current: null
    // The Die objects the repeater has made, by tile: the scene poses them every
    // frame, and a die that a new board adds is posed as it arrives. (Held as
    // plain values because a Repeater3D's objectAt is a bare QQuick3DObject to
    // qmllint, which then cannot see the Die type's members.)
    property var diceObjects: []
    property int warmFrames: 0
    // The device's motion as it has come in (see pushMotion), and when.
    property var tracker: Script.newTracker()
    property double lastMotionMs: -1e9
    property bool warmedOnce: false

    // ---- Driving the scene --------------------------------------------------
    // What the script needs of the layout, in this item's pixels.
    function layout() {
        return { tile: tile, gap: gap, pitch: pitch, reduced: reducedMotion, weighted: weighted,
                 lidTiles: lidTiles, slackTiles: slackTiles, mock: true, mockBurstMs: mockBurstMs,
                 sim: diceSim };
    }

    // A board in the shape the scene takes. A board with no faces (the phonics
    // board, or a game the store brought back, whose roll's seed is not kept) is
    // one string on each of a die's six sides, the face that is up the one a
    // cube at rest has on top (+z, 4): the letter is drawn as the first scene
    // drew it. An empty tile list is the closed box, and has no faces at all.
    function sceneBoard(board) {
        const rows = Math.max(0, board.rows || 0);
        const columns = Math.max(0, board.columns || 0);
        const tiles = board.tiles ? board.tiles : [];
        if (tiles.length === 0) {
            return { rows: rows, columns: columns, tiles: [], faces: [], up: [] };
        }
        let faces = board.faces;
        let up = board.up;
        if (!faces || faces.length !== tiles.length) {
            faces = [];
            up = [];
            for (let t = 0; t < tiles.length; ++t) {
                const tile = tiles[t] === undefined ? "" : tiles[t];
                faces.push([tile, tile, tile, tile, tile, tile]);
                up.push(4);
            }
        } else if (!up || up.length !== faces.length) {
            up = faces.map(() => 4);
        }
        return { rows: rows, columns: columns, tiles: tiles, faces: faces, up: up };
    }

    // The board's dice. A board whose faces do not make its tiles is drawn as
    // given and reported: the scene lands the up face, so a mismatch would show
    // the wrong letter without a sound. Returns what it set, in the shape the
    // script takes.
    function setBoard(board) {
        const shown = sceneBoard(board);
        useAtlasFor(shown);
        boardRows = shown.rows;
        boardColumns = shown.columns;
        boardFaces = shown.faces;
        boardUp = shown.up;
        for (let t = 0; t < shown.tiles.length; ++t) {
            if (shown.faces[t][shown.up[t]] !== shown.tiles[t]) {
                console.warn("BoardScene: tile " + t + " is '" + shown.tiles[t]
                             + "' but the face that is up is '" + shown.faces[t][shown.up[t]] + "'");
                break;
            }
        }
        return shown;
    }

    // What identifies the faces an atlas draws: the size and the six strings
    // of every die. Two boards with the same key draw the same atlas.
    function facesKey(board) {
        return board.rows + "x" + board.columns + ":" + board.faces.map(sides => sides.join("|")).join("/");
    }

    function fillAtlas(atlas, board) {
        atlas.fill(board.rows, board.columns, board.faces);
    }

    // The dice show the atlas of the board they are given: the one prepared if
    // it is that board's, else the one they have, drawn again.
    function useAtlasFor(board) {
        const key = facesKey(board);
        if (key === atlasKey) {
            return;
        }
        if (key === spareKey) {
            const shown = atlasItem;
            atlasItem = spareAtlas;
            spareAtlas = shown;
            spareKey = atlasKey;
            atlasKey = key;
            return;
        }
        fillAtlas(atlasItem, board);
        atlasKey = key;
    }

    function begin(board, seed, standIn) {
        const shown = setBoard(board);
        const settings = layout();
        settings.mock = standIn !== false;
        current = Script.create(shown, seed, settings);
        // A new play starts with every letter square on every die.
        for (let i = 0; i < diceObjects.length; ++i) {
            const die = diceObjects[i];
            if (die) {
                die.geometry.clearTurns();
            }
        }
        // The dice are made as the count changes; a die that is not there yet
        // is posed when it arrives (onObjectAdded below).
    }

    function apply() {
        const state = current;
        if (!state) {
            // The closed box: no dice, the lid on, nothing moving.
            for (let i = 0; i < diceObjects.length; ++i) {
                const die = diceObjects[i];
                if (die) {
                    die.body.visible = false;
                    die.shadow.visible = false;
                }
            }
            boxNode.eulerRotation = Qt.vector3d(0, 0, 0);
            lidHinge.eulerRotation = Qt.vector3d(0, 0, 0);
            lidHinge.position = Qt.vector3d(0, boxY / 2 + rimWidth, lidHeight);
            lid.opacity = boxClosed ? 1 : 0;
            lidHinge.visible = boxClosed;
            leanNow = 0;
            zoomNow = 0;
            return;
        }
        const warm = warmFrames > 0;
        for (let i = 0; i < state.count; ++i) {
            const die = diceObjects[i];
            if (!die) {
                continue;
            }
            die.body.position = Qt.vector3d(state.px[i] - die.x, state.py[i] - die.y, state.pz[i]);
            if (state.useQuaternion) {
                die.body.rotation = Qt.quaternion(state.qw[i], state.qx[i], state.qy[i], state.qz[i]);
            } else {
                die.body.eulerRotation = Qt.vector3d(state.rx[i], state.ry[i], state.rz[i]);
            }
            // The cube's own turn: which face is up, for the staged dice (the
            // simulated ones are turned by the body, and this is no turn).
            die.cube.rotation = Qt.quaternion(state.tw[i], state.tx[i], state.ty[i], state.tz[i]);
            die.cell = state.cell ? state.cell[i] : i;
            die.body.visible = state.opacity[i] > 0.001;
            die.body.opacity = warm ? 0.98 : state.opacity[i];
            const lift = Math.max(0, state.pz[i] - tile / 2);
            die.shadow.position = Qt.vector3d(state.px[i] - die.x + 0.45 * lift, state.py[i] - die.y - 0.45 * lift, 0.2);
            const radius = state.shadowRadius[i] / 50;
            die.shadow.scale = Qt.vector3d(radius, radius, 0.02);
            // The flat board has no shadows: once the dice are down, only the
            // scene's cross-fade is left, and a shadow would move with it.
            // (The warm-up keeps 2% so the shadow's resources are made.)
            die.shadow.opacity = warm ? Math.max(state.shadowOpacity[i], 0.02)
                : (state.stage === "idle" ? 0 : state.shadowOpacity[i]);
        }
        // The letter on the face that is up is turned to read upright.
        if (state.turnChanges && state.turnChanges.length > 0) {
            for (let k = 0; k < state.turnChanges.length; ++k) {
                const change = state.turnChanges[k];
                const die = diceObjects[change.die];
                if (die) {
                    die.geometry.setTurns(change.face, change.turns);
                }
            }
            state.turnChanges = [];
        }
        // The box answers the device while it is being shaken, and comes square
        // with the camera as the dice settle: at rest it has to be the flat
        // board's square, not the angle the phone happens to be held at.
        const tilt = state.leanFraction;
        boxNode.eulerRotation = Qt.vector3d(state.boxRx * tilt, state.boxRy * tilt, 0);
        lidHinge.eulerRotation = Qt.vector3d(state.lidAngle, 0, 0);
        lidHinge.position = Qt.vector3d(0, boxY / 2 + rimWidth, lidHeight + state.lidLift);
        lid.opacity = warm ? Math.max(state.lidOpacity, 0.02) : state.lidOpacity;
        lidHinge.visible = warm || state.lidOpacity > 0.001;
        leanNow = lean * state.leanFraction;
        zoomNow = state.useQuaternion ? state.leanFraction : 0;
    }

    // The zone a frame of the play is in, by the stage the script is at: the
    // shake, the lid and the drop, or the settle. Empty when nothing moves. One
    // zone a frame, not one an inner loop.
    function stageZone() {
        switch (stage) {
        case "shake":
            return "scene.shake";
        case "lid":
        case "drop":
            return "scene.drop";
        case "settle":
            return "scene.settle";
        default:
            return "";
        }
    }

    function step(dtMs) {
        const state = current;
        if (!state) {
            return;
        }
        const zone = stageZone();
        if (zone !== "") {
            Profiler.begin(zone);
        }
        if (warmFrames > 0) {
            warmFrames -= 1;
            if (state.stage === "idle") {
                apply();
                if (warmFrames === 0) {
                    frames.running = false;
                }
                if (zone !== "") {
                    Profiler.end();
                }
                return;
            }
        }
        Script.advance(state, dtMs, tracker);
        Script.evaluate(state);
        apply();
        stage = state.stage;
        const events = state.events;
        state.events = [];
        for (let k = 0; k < events.length; ++k) {
            const event = events[k];
            if (event.kind === "rattled") {
                rattled();
            } else if (event.kind === "landed") {
                landed(event.index, event.pitch);
            } else if (event.kind === "settled") {
                frames.running = false;
                settled();
            }
        }
        if (zone !== "") {
            Profiler.end();
        }
    }

    property real leanNow: lean
    property real zoomNow: 0

    // One tick for each frame the window draws; a stalled frame counts as 50 ms,
    // so a hitch slows the play down instead of skipping through it.
    FrameAnimation {
        id: frames

        running: false
        onTriggered: root.step(Math.min(frameTime * 1000, 50))
    }

    // The board changing size creates the dice; give them their poses as they
    // arrive.
    onBoardFacesChanged: Qt.callLater(apply)

    // ---- The scene ----------------------------------------------------------
    // The dice's physics; the script drives it.
    DiceSim {
        id: diceSim
    }

    // The face atlases. The dice show one (atlasItem) and prepare() draws the
    // next board into the other (spareAtlas); they swap when the board it drew
    // arrives. atlasKey and spareKey say which faces each holds (facesKey).
    //
    // Each is a layer, drawn once into one texture that every die's Texture
    // reads. An atlas that is only a Texture's `sourceItem`, with no parent and
    // no layer, cost far more, and more the more dice there were (the render
    // thread's time for the frame of a new 6 x 6 board was 33 to 35 ms and with
    // a layer it is 5 ms; for a 3 x 3 board, with a ninth of the dice, 12 ms and
    // 3 ms): each of a board's textures seems to draw it for itself. The atlases are in the window's
    // tree, a long way off its left edge; Quick 3D puts a source item that has
    // no parent under the window's root item in any case, and keeps it out of
    // sight.
    property Item atlasItem: atlasOne
    property Item spareAtlas: atlasTwo
    property string atlasKey: ""
    property string spareKey: ""
    property Item atlasOne: FaceAtlas {
        parent: root
        x: -100000
        faceColor: root.dieColor
        edgeColor: root.dieEdgeColor
        inkColor: root.dieInkColor
        jewelColor: root.jewelColor
        goldColor: root.rimColor
    }
    property Item atlasTwo: FaceAtlas {
        parent: root
        x: -100000
        faceColor: root.dieColor
        edgeColor: root.dieEdgeColor
        inkColor: root.dieInkColor
        jewelColor: root.jewelColor
        goldColor: root.rimColor
    }

    // The atlases become layers after this component has completed. Turning the
    // layer on in the declaration completes the item's layer while the scene is
    // still completing, and a debug build of Qt asserts in
    // QQuickItemLayer::componentComplete when that happens twice; the layers are
    // needed from the first frame, and this is before it.
    Component.onCompleted: {
        atlasOne.layer.enabled = true;
        atlasTwo.layer.enabled = true;
    }

    // Made once, when the scene is, for the mode asked for: a Texture has to
    // live in the 3D scene, so it is made under the scene's own root.
    property var probeTexture: root.probeMode === "none" ? null
        : (root.probeMode === "baked" ? bakedProbe.createObject(probeHost)
                                      : proceduralProbe.createObject(probeHost))

    Component {
        id: proceduralProbe

        // Qt's own sky, used only as a light. Qt prefilters it into a cube map
        // on the first frame of the view.
        Texture {
            textureData: ProceduralSkyTextureData {}
        }
    }

    Component {
        id: bakedProbe

        // The same sky, prefiltered in advance by tools/lightprobe, so Qt does
        // no prefiltering at run time.
        Texture {
            source: "lightprobe/sky.ktx"
        }
    }

    // The distance at which, upright, one scene unit is one pixel on the top
    // of the dice, and the field of view that gives, for the view, which is the
    // item and its overdraw. While the camera leans it also stands a little back
    // (and comes in as it comes upright), so a board that fills its square
    // upright is not cut off by the lean.
    readonly property real viewDistance: cameraDistance * Math.max(spanX, spanY)
    readonly property real fieldOfView: 2 * Math.atan((height + 2 * overdraw) / 2 / viewDistance) * 180 / Math.PI
    // How far the camera stands back so that the whole box is in the item: the
    // box's size over the item's, with a tenth to spare for the dice nearer the
    // camera at the lid.
    readonly property real zoomOut: Math.max(0, 1.1 * Math.max(boxX / Math.max(1, width), boxY / Math.max(1, height)) - 1)
    readonly property real standBack: 1 + 0.3 * (lean > 0 ? Math.min(1, leanNow / lean) : 0) + zoomOut * zoomNow

    // The flat board's tray, for the settled board. It is 2D, behind the View3D:
    // the box's shell has faded by then, so the dice sit in this and the
    // application's own board area shows around it, exactly as FlatBoard draws
    // them (same inset, radius, tray and gold edge). The scene is the theatre
    // for the shake, and at rest it is the board the cross-fade leaves.
    Rectangle {
        id: settleTray

        readonly property real inset: root.trayInset

        x: root.boardRect.x - inset
        y: root.boardRect.y - inset
        width: root.boardRect.width + 2 * inset
        height: root.boardRect.height + 2 * inset
        radius: 20
        color: root.trayColor
        border.width: 2
        border.color: root.rimColor
        visible: opacity > 0.001
        opacity: root.stage === "idle" && !root.boxClosed ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: 200
                easing.type: Easing.OutCubic
            }
        }
    }

    View3D {
        id: view

        x: -root.overdraw
        y: -root.overdraw
        width: root.width + 2 * root.overdraw
        height: root.height + 2 * root.overdraw

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.Medium
            lightProbe: root.probeTexture
            // Only used if a probe is asked for above; a quarter of the old
            // exposure, so it cannot tint the dice.
            probeExposure: 0.2
        }

        // The camera rig: looks at the middle of the dice's top faces, from a
        // distance chosen so that, upright, one scene unit is one pixel at that
        // height. While the dice are in the air the perspective camera below
        // leans and stands back with the box; at rest the orthographic one
        // shows the board parallel, exactly over the flat board.
        Node {
            id: cameraRig

            readonly property real radians: root.leanNow * Math.PI / 180

            position: Qt.vector3d(0, -root.viewDistance * root.standBack * Math.sin(radians),
                                  root.tile + root.viewDistance * root.standBack * Math.cos(radians))
            eulerRotation: Qt.vector3d(root.leanNow, 0, 0)

            PerspectiveCamera {
                id: sceneCamera

                fieldOfView: root.fieldOfView
                fieldOfViewOrientation: PerspectiveCamera.Vertical
                clipNear: 10
                clipFar: root.viewDistance * 5
            }

            // The settled board's view: parallel projection, one scene unit to
            // one pixel at magnification 1, so the tops of the dice are the
            // flat board's tiles, edge for edge, and no side of a die is seen.
            // The cross-fade to FlatBoard then moves nothing. It is also
            // zoomed out by standBack while the box is still open.
            OrthographicCamera {
                id: boardCamera

                verticalMagnification: 1 / root.standBack
                horizontalMagnification: 1 / root.standBack
                clipNear: 10
                clipFar: root.viewDistance * 5
            }
        }

        // The theatre until the dice are down; the board itself once they are
        // (an empty board is the closed box, and keeps the perspective view).
        camera: root.stage === "idle" && !root.boxClosed ? boardCamera : sceneCamera

        DirectionalLight {
            eulerRotation: Qt.vector3d(-38, -28, 0)
            brightness: 0.8
            // The same warm paper the screens are made of, so the cream dice
            // and the gold rim sit in the same light. Warm, not orange: the
            // light must not add a colour the flat board does not have.
            ambientColor: Qt.rgba(0.30, 0.288, 0.264, 1)
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(28, 24, 0)
            brightness: 0.26
        }

        // Where the probe's texture lives.
        Node {
            id: probeHost
        }

        // The box, with the dice and the lid in it, tilts as one.
        Node {
            id: boxNode

            // The shell is a picture of the tray, not a lit object of its own:
            // unlit, so it keeps the flat board's exact gold and deep teal under
            // the warm dice lights.
            DefaultMaterial {
                id: rimMaterial

                // Theme.gold.
                diffuseColor: root.rimColor
                lighting: DefaultMaterial.NoLighting
            }

            // The floor and the low rim, the box's shell. When a board is down
            // it fades away: the flat board's tray (drawn behind the View3D
            // below) is what the settled dice then sit in, so the scene becomes
            // the board instead of a dark box around it.
            Node {
                id: boxShell

                // The warm-up draws the shell at 2% for its three frames (Qt
                // skips anything under 1%), so the first shake does not pay for
                // the floor's and rim's pipelines; at rest it is gone.
                opacity: root.stage === "idle" && !root.boxClosed
                    ? (root.warmFrames > 0 ? 0.02 : 0) : 1
                Behavior on opacity {
                    NumberAnimation {
                        duration: 200
                        easing.type: Easing.OutCubic
                    }
                }

                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, 0, -root.boxDepth * 0.15)
                    scale: Qt.vector3d((root.boxX + 2 * root.rimWidth) / 100,
                                       (root.boxY + 2 * root.rimWidth) / 100,
                                       root.boxDepth * 0.3 / 100)
                    castsShadows: false
                    receivesShadows: false
                    materials: DefaultMaterial {
                        // Theme.tray; the same deep tray the flat board sits in.
                        diffuseColor: root.trayColor
                        lighting: DefaultMaterial.NoLighting
                    }
                }

                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, root.boxY / 2 + root.rimWidth / 2, root.boxDepth / 2)
                    scale: Qt.vector3d((root.boxX + 2 * root.rimWidth) / 100, root.rimWidth / 100, root.boxDepth / 100)
                    materials: rimMaterial
                }

                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, -root.boxY / 2 - root.rimWidth / 2, root.boxDepth / 2)
                    scale: Qt.vector3d((root.boxX + 2 * root.rimWidth) / 100, root.rimWidth / 100, root.boxDepth / 100)
                    materials: rimMaterial
                }

                Model {
                    source: "#Cube"
                    position: Qt.vector3d(root.boxX / 2 + root.rimWidth / 2, 0, root.boxDepth / 2)
                    scale: Qt.vector3d(root.rimWidth / 100, root.boxY / 100, root.boxDepth / 100)
                    materials: rimMaterial
                }

                Model {
                    source: "#Cube"
                    position: Qt.vector3d(-root.boxX / 2 - root.rimWidth / 2, 0, root.boxDepth / 2)
                    scale: Qt.vector3d(root.rimWidth / 100, root.boxY / 100, root.boxDepth / 100)
                    materials: rimMaterial
                }
            }

            Repeater3D {
                id: diceRepeater

                model: root.boxClosed ? 0 : root.boardRows * root.boardColumns

                delegate: Die {
                    id: dieSlot

                    atlas: root.atlasItem
                    columns: root.boardColumns
                    rows: root.boardRows
                    size: root.tile
                    x: ((dieSlot.index % root.boardColumns) - (root.boardColumns - 1) / 2) * root.pitch
                    y: ((root.boardRows - 1) / 2 - Math.floor(dieSlot.index / root.boardColumns)) * root.pitch
                }

                onObjectAdded: (index, object) => {
                    root.diceObjects[index] = object;
                    Qt.callLater(root.apply);
                }
                onObjectRemoved: (index, object) => {
                    root.diceObjects[index] = null;
                }
            }

            // The lid: a pane of frosted glass hinged on the far edge.
            Node {
                id: lidHinge

                position: Qt.vector3d(0, root.boxY / 2 + root.rimWidth, root.lidHeight)

                Model {
                    id: lid

                    source: "#Cube"
                    position: Qt.vector3d(0, -(root.boxY / 2 + root.rimWidth), 0)
                    // The lid is nearer the camera than the box, so in perspective it
                    // would stand out past the box's rim on every side, a pale
                    // band on the paper; it is made the size that projects onto the
                    // rim.
                    scale: Qt.vector3d(root.lidFit * (root.boxX + 2 * root.rimWidth) / 100,
                                       root.lidFit * (root.boxY + 2 * root.rimWidth) / 100,
                                       root.tile * 0.06 / 100)
                    castsShadows: false
                    receivesShadows: false
                    materials: PrincipledMaterial {
                        // Theme.lid at the same translucency as before.
                        baseColor: Qt.rgba(root.lidColor.r, root.lidColor.g, root.lidColor.b, 0.24)
                        alphaMode: PrincipledMaterial.Blend
                        roughness: 0.22
                    }
                }
            }
        }
    }
}
