// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShakePrototype
import WordJewelsSolver.Scene
// The scene's script, one file for the application and this harness. The scene
// module owns it; the harness reads it from the module's resource directory for
// its stand-in shake detector (the scene's pushMotion is what the dice answer).
import "qrc:/qt/qml/WordJewelsSolver/Scene/ShakeScript.js" as Script

// The harness around BoardScene: a window with a Shake button, a mouse drag
// that stands in for the accelerometer, a 2D layer over the scene to show the
// tiles line up, and the measurement run (--auto).
//
// Nothing here is the scene. BoardScene is made once, from its file, by
// createScene(): that is the "time to create the View3D" the page reports.
ApplicationWindow {
    id: window

    required property bool autoRun
    required property bool shotsOnly
    required property bool digestOnly
    required property bool prepareFirst
    required property int startBoard
    required property bool changeOnly
    required property string boardKind
    required property var autoSizes
    required property int framesPerSecond
    required property string outDir
    required property string probeMode
    required property real leanDegrees
    required property int runCount
    required property bool warmUp
    required property bool weighted
    required property int startWidth
    required property int startHeight

    width: startWidth > 0 ? startWidth : 560
    height: startHeight > 0 ? startHeight : 900
    visible: true
    title: qsTr("Board scene prototype")
    color: "#EFE8D8"

    // ---- What is being shown -------------------------------------------------
    property int boardSize: startBoard
    // "standard": the library's roll of the shipped dice. "phonics": a stand-in
    // for the phonics board (see makeBoard), which is not the library's.
    property string kind: boardKind
    property int seed: 7
    property bool reduced: false
    property bool showTiles: true
    property var scene: null
    property var litTiles: ({})

    // The library's dice, embedded: the standard board is its roll.
    DiceRoller {
        id: dice
    }

    // A board in the shape the scene is given: { rows, columns, tiles, faces,
    // up }. This is a stand-in for the library's seeded board
    // (docs/design/board.md, "The phase board"): a pool of phonics-ish tiles
    // with a few digraphs and a Qu, and some dead jewels (""), chosen from the
    // seed. The library has no dice for it (the design's open choice is whether
    // its dice are phase dice or decoy faces drawn for show), so this gives each
    // tile a die whose other five faces are drawn from the pool, six different
    // letters: a stand-in, to exercise the scene, and not what the design will
    // decide.
    function makeBoard(size, boardSeed, deadCount) {
        const pool = ["S", "A", "T", "P", "I", "N", "M", "D", "G", "O", "C", "K", "E", "U", "R",
                      "H", "B", "F", "L", "SH", "CH", "TH", "QU", "A", "O", "E", "I"];
        let a = (boardSeed * 2654435761) >>> 0;
        const next = () => {
            a = (Math.imul(a, 1664525) + 1013904223) >>> 0;
            return a / 4294967296;
        };
        const count = size * size;
        const tiles = [];
        for (let i = 0; i < count; ++i) {
            tiles.push(pool[Math.floor(next() * pool.length)]);
        }
        const dead = size === 3 ? Math.min(1, deadCount) : deadCount;
        for (let d = 0; d < dead; ++d) {
            tiles[Math.floor(next() * count)] = "";
        }
        // A die for each tile: the tile on a face of its own, and others from
        // the pool; a dead jewel is a die of six jewels.
        const faces = [];
        const up = [];
        const letters = Array.from(new Set(pool));
        for (let i = 0; i < count; ++i) {
            const sides = [];
            const where = Math.floor(next() * 6);
            if (tiles[i] === "") {
                sides.push("", "", "", "", "", "");
            } else {
                // Six different letters, as on a real die.
                const unused = letters.filter(letter => letter !== tiles[i]);
                for (let k = 0; k < 5; ++k) {
                    sides.push(unused.splice(Math.floor(next() * unused.length), 1)[0]);
                }
                sides.splice(where, 0, tiles[i]);
            }
            faces.push(sides);
            up.push(where);
        }
        return { rows: size, columns: size, tiles: tiles, faces: faces, up: up };
    }

    // The board for the size and seed: the library's roll of the shipped dice
    // for the standard board, the stand-in for the other.
    function boardFor(size, boardSeed) {
        if (kind === "standard" && dice.hasSet(size)) {
            return dice.roll(size, boardSeed);
        }
        return makeBoard(size, boardSeed, 2);
    }

    function currentBoard() {
        return boardFor(boardSize, seed);
    }

    // ---- Making the scene, once ---------------------------------------------
    property double loadedMs: 0
    property double createdMs: 0
    property var creation: ({})

    function createScene() {
        const startedAt = stats.now();
        stats.begin();
        // The application's scene, from the module it is built in.
        const component = Qt.createComponent("qrc:/qt/qml/WordJewelsSolver/Scene/BoardScene.qml");
        if (component.status !== Component.Ready) {
            console.error("BoardScene.qml: " + component.errorString());
            Qt.quit();
            return;
        }
        const loadedAt = stats.now();
        const made = component.createObject(sceneHost, {
            probeMode: window.probeMode,
            lean: window.leanDegrees,
            warmUp: window.warmUp,
            weighted: window.weighted
        });
        const createdAt = stats.now();
        made.anchors.fill = sceneHost;
        window.scene = made;
        window.creation = { startedAtMs: startedAt, loadMs: loadedAt - startedAt, createMs: createdAt - loadedAt, createdAtMs: createdAt };
        made.show(currentBoard());
        // Let a few frames go, then read what the first of them cost.
        firstFrames.restart();
    }

    // Waits until the scene has drawn a frame, however long the first one takes
    // (a software renderer can take minutes), then a little longer for any that
    // follow, and reads what they cost.
    property bool sawFirstFrame: false

    Timer {
        id: firstFrames

        interval: 50
        repeat: true
        onTriggered: {
            if (stats.recordedFrames() < 1) {
                return;
            }
            if (!window.sawFirstFrame) {
                window.sawFirstFrame = true;
                return;
            }
            // One more tick (50 ms) per frame already seen is not needed: the
            // warm-up's three frames take 50 ms or so.
            if (stats.recordedFrames() < 4 && ++window.settleTicks < 8) {
                return;
            }
            stop();
            const result = stats.end();
            window.creation.firstSwapAfterCreateMs = result.firstSwapAtMs - window.creation.createdAtMs;
            window.creation.firstFrames = result;
            if (window.autoRun) {
                if (window.digestOnly) {
                    window.runDigest();
                } else {
                    window.startAuto();
                }
            }
        }
    }

    Connections {
        target: stats

        function onFirstFrame(atMs) {
            window.windowFirstFrameMs = atMs;
            Qt.callLater(window.createScene);
        }
    }

    property double windowFirstFrameMs: 0
    property int settleTicks: 0

    FrameStats {
        id: stats

        window: window
    }

    // ---- The device's motion, from the mouse --------------------------------
    // Dragging the board moves the "device" under the pointer. The pointer's
    // position at each event is differentiated twice into an acceleration (the
    // board is taken as 16 cm across), so the stream is as jerky as the hand:
    // a spike at every start and stop. Where the pointer is in the scene tips
    // the device up to 8 degrees, and its speed twists it. Every sample goes
    // to the scene, and to a stand-in for the shake detector (the library's),
    // which starts a play when the hand is shaking hard enough.
    property var detector: Script.newTracker()
    property bool armed: true
    // What the measurement run saw of the hand, for the run driven by the pointer.
    property real peakLevel: 0
    property real peakTilt: 0

    QtObject {
        id: pointer

        property bool down: false
        property double at: 0
        property real x: 0
        property real y: 0
        property real vx: 0
        property real vy: 0
    }

    function clampTo(value, limit) {
        return Math.max(-limit, Math.min(limit, value));
    }

    // The pointer is at (px, py) in the scene now: one sample of the device.
    function pointerSample(px, py, atMs) {
        if (window.scene === null) {
            return;
        }
        const dt = Math.max(0.002, (atMs - pointer.at) / 1000);
        const vx = (px - pointer.x) / dt;
        const vy = (py - pointer.y) / dt;
        const ax = (vx - pointer.vx) / dt;
        const ay = (vy - pointer.vy) / dt;
        pointer.x = px;
        pointer.y = py;
        pointer.vx = vx;
        pointer.vy = vy;
        pointer.at = atMs;
        const toG = 0.16 / Math.max(1, window.scene.boardRect.width) / 9.81;
        const handX = clampTo(ax * toG, 5);
        const handY = clampTo(-ay * toG, 5);
        const roll = clampTo((px / sceneHost.width - 0.5) * 2, 1) * 0.14;
        const pitch = clampTo((0.5 - py / sceneHost.height) * 2, 1) * 0.14;
        const sx = -Math.sin(roll) + handX;
        const sy = Math.sin(pitch) + handY;
        // A mouse cannot lift the device. A hand that shakes one sideways also
        // moves it up and down, in time with the strokes, so that is added: the
        // up and down follows the sideways acceleration.
        const lift = clampTo(0.8 * (handX + handY), 4);
        const sz = Math.cos(roll) * Math.cos(pitch) + lift;
        const wx = clampTo(-vy * 0.001, 4);
        const wy = clampTo(vx * 0.001, 4);
        window.scene.pushMotion(sx, sy, sz, wx, wy, 0);
        Script.feed(window.detector, sx, sy, sz, wx, wy, 0);
        window.peakLevel = Math.max(window.peakLevel, window.detector.level);
        window.peakTilt = Math.max(window.peakTilt, Math.abs(roll), Math.abs(pitch));
        if (window.armed && pointer.down && !window.scene.busy
                && window.detector.level > Script.shakeStartLevel) {
            window.armed = false;
            window.seed += 1;
            window.shake();
        }
    }

    // While the hand is down or the dice are moving the device keeps sending
    // samples (a hundred a second), so a hand that has stopped reads as stopped.
    Timer {
        interval: 10
        repeat: true
        running: window.scene !== null && (window.scene.busy || pointer.down)
        onTriggered: {
            const now = stats.now();
            if (now - pointer.at > 20) {
                window.pointerSample(pointer.x, pointer.y, now);
            }
        }
    }

    Binding {
        target: window.scene
        property: "reducedMotion"
        value: window.reduced
        when: window.scene !== null
    }

    // ---- Layout: the board in the top two-thirds, the panel below ----------
    Item {
        id: sceneHost

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        // The scene draws past its item by its overdraw (0.8 of a tile); leave
        // room above it. A fixed share of the window, not the scene's own
        // number, which depends on this item's size.
        anchors.topMargin: parent.height * 0.07
        height: parent.height * 2 / 3 - anchors.topMargin

        DragHandler {
            id: drag

            target: null
            onCentroidChanged: {
                if (active) {
                    window.pointerSample(centroid.position.x, centroid.position.y, stats.now());
                }
            }
            onActiveChanged: {
                pointer.down = active;
                if (active) {
                    pointer.x = centroid.position.x;
                    pointer.y = centroid.position.y;
                    pointer.vx = 0;
                    pointer.vy = 0;
                    pointer.at = stats.now();
                    window.peakLevel = 0;
                    window.peakTilt = 0;
                } else {
                    window.armed = true;
                }
            }
        }

        // The 2D layer: the tiles a finger would trace over. Drawn only while
        // the dice are down, laid out from the numbers the scene gives.
        Repeater {
            model: window.scene && window.showTiles && !window.scene.busy
                ? window.scene.rows * window.scene.columns : 0

            Rectangle {
                id: tileLayer

                required property int index
                readonly property bool lit: window.litTiles[index] === true

                // Above the scene, which is made after this and so is later in
                // the order.
                z: 10
                x: window.scene.boardRect.x + (index % window.scene.columns) * (window.scene.tileSize + window.scene.tileGap)
                y: window.scene.boardRect.y + Math.floor(index / window.scene.columns) * (window.scene.tileSize + window.scene.tileGap)
                width: window.scene.tileSize
                height: window.scene.tileSize
                color: lit ? Qt.rgba(0.91, 0.64, 0.09, 0.35) : "transparent"
                border.color: lit ? "#E8A317" : Qt.rgba(0.9, 0.1, 0.4, 0.45)
                border.width: lit ? 4 : 1
                scale: lit ? 1.06 : 1

                HoverHandler {
                    onHoveredChanged: {
                        const next = Object.assign({}, window.litTiles);
                        next[tileLayer.index] = hovered;
                        window.litTiles = next;
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        height: parent.height / 3 - 24
        spacing: 8

        RowLayout {
            spacing: 8

            Button {
                text: qsTr("Shake")
                highlighted: true
                enabled: window.scene !== null && !window.scene.busy
                onClicked: window.shake()
            }

            Button {
                text: qsTr("New board")
                enabled: window.scene !== null && !window.scene.busy
                onClicked: {
                    window.seed += 1;
                    window.shake();
                }
            }

            ComboBox {
                model: ["3 x 3", "4 x 4", "5 x 5", "6 x 6"]
                currentIndex: Math.max(0, Math.min(3, window.boardSize - 3))
                enabled: window.scene !== null && !window.scene.busy
                onActivated: (index) => {
                    window.boardSize = index + 3;
                    window.scene.show(window.currentBoard());
                }
            }
        }

        RowLayout {
            spacing: 16

            Switch {
                text: qsTr("Reduced motion")
                checked: window.reduced
                onToggled: window.reduced = checked
            }

            Switch {
                text: qsTr("Simulated dice")
                checked: window.weighted
                onToggled: {
                    window.weighted = checked;
                    if (window.scene) {
                        window.scene.weighted = checked;
                    }
                }
            }

            Switch {
                text: qsTr("2D tile layer")
                checked: window.showTiles
                onToggled: window.showTiles = checked
            }

            Switch {
                text: qsTr("Library dice")
                checked: window.kind === "standard"
                enabled: window.scene !== null && !window.scene.busy
                onToggled: {
                    window.kind = checked ? "standard" : "phonics";
                    window.scene.show(window.currentBoard());
                }
            }
        }

        RowLayout {
            Label {
                text: qsTr("Lean %1°").arg(Math.round(leanSlider.value))
            }

            Slider {
                id: leanSlider

                Layout.fillWidth: true
                from: 0
                to: 45
                value: window.leanDegrees
                onMoved: if (window.scene) { window.scene.lean = value; }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#3B342B"
            font.pixelSize: 12
            // Not updated during a measurement run, so the harness's own text
            // is not in the frames being timed.
            text: window.autoRun ? qsTr("measuring") : window.readout
        }

        Item {
            Layout.fillHeight: true
        }
    }

    property string lastPlay: ""
    property int landedCount: 0
    property int rattleCount: 0
    readonly property string readout: qsTr("%1  |  %2  |  landed %3, rattles %4  |  drag the board to rattle it")
        .arg(window.scene ? (window.scene.busy ? window.scene.stage : "settled") : "no scene")
        .arg(window.lastPlay)
        .arg(window.landedCount)
        .arg(window.rattleCount)

    Connections {
        target: window.scene

        function onSettled() {
            window.lastPlay = qsTr("played in %1 ms").arg(Math.round(stats.now() - window.playStartedAt));
            window.playDone();
        }

        function onLanded(index, pitch) {
            window.landedCount += 1;
        }

        function onRattled() {
            window.rattleCount += 1;
        }
    }

    property double playStartedAt: 0

    function shake() {
        landedCount = 0;
        rattleCount = 0;
        playStartedAt = stats.now();
        scene.play(currentBoard(), seed);
    }

    // ---- The measurement run -----------------------------------------------
    property var jobs: []
    property var results: []
    property var job: null
    property var shots: []

    signal playDone()

    // The replay check: the same board and seed give the same checksum of
    // every pose, a different seed does not, and reduced motion is its own
    // script.
    function runDigest() {
        const lines = [];
        for (const size of autoSizes) {
            const board = boardFor(size, 7);
            scene.reducedMotion = false;
            const first = scene.digest(board, 100);
            const again = scene.digest(board, 100);
            const other = scene.digest(board, 101);
            scene.reducedMotion = true;
            const reducedFirst = scene.digest(board, 100);
            const reducedAgain = scene.digest(board, 100);
            lines.push(size + "x" + size + " seed 100: " + first + " then " + again
                       + (first === again ? " (same)" : " (DIFFERENT)")
                       + "; seed 101: " + other + (other !== first ? " (differs)" : " (SAME)")
                       + "; reduced seed 100: " + reducedFirst + " then " + reducedAgain
                       + (reducedFirst === reducedAgain ? " (same)" : " (DIFFERENT)"));
        }
        // The faces: that every die ends with the board's face up, that the faces
        // are the board's tiles, and what a viewer saw of the other faces on the
        // way. For the library's dice and the stand-in.
        for (const which of ["standard", "phonics"]) {
            for (const size of autoSizes) {
                const board = which === "standard" ? dice.roll(size, 7) : makeBoard(size, 7, 2);
                if (!board.faces) {
                    continue;
                }
                let tilesAreFaces = 0;
                let distinct = 0;
                for (let t = 0; t < board.tiles.length; ++t) {
                    if (board.faces[t][board.up[t]] === board.tiles[t]) {
                        tilesAreFaces += 1;
                    }
                    distinct += new Set(board.faces[t]).size;
                }
                // The finished board, row by row, to compare with a still.
                const rowsText = [];
                for (let r = 0; r < board.rows; ++r) {
                    rowsText.push(board.tiles.slice(r * board.columns, (r + 1) * board.columns).join(" "));
                }
                lines.push("board, " + which + " " + size + "x" + size + " seed 7: " + rowsText.join("/"));
                scene.reducedMotion = false;
                const report = scene.faceReport(board, 100);
                scene.reducedMotion = true;
                const reduced = scene.faceReport(board, 100);
                scene.reducedMotion = false;
                lines.push("faces, " + which + " " + size + "x" + size + " seed 7 (play seed 100): "
                           + tilesAreFaces + "/" + board.tiles.length + " tiles are the face up; "
                           + "mean " + (distinct / board.tiles.length).toFixed(2) + " different letters on a die; "
                           + report.landed + "/" + report.dice + " land face up; "
                           + "mean " + report.facesSeenMean.toFixed(2) + " faces on top in a play, most "
                           + report.facesSeenMost + "; reduced "
                           + reduced.landed + "/" + reduced.dice + ", " + reduced.facesSeenMean.toFixed(2));
            }
        }
        scene.reducedMotion = false;
        for (const seed of [100, 101, 102, 103]) {
            stats.writeText(outDir + "/trace-" + seed + ".txt", scene.trace(makeBoard(4, 7, 2), seed));
        }
        const text = lines.join("\n") + "\n";
        console.log("DIGEST\n" + text);
        stats.writeText(outDir + "/digest.txt", text);
        Qt.quit();
    }

    // What it costs to change the board: the frames from show() of a new
    // board, in which its dice's faces are drawn into the atlas again. For each
    // size, runCount boards, one after the other (a new seed each, so the atlas
    // is made new each time), written to change-<probe>.json. With --prepare
    // the scene is first given the board to prepare() and the frames of the
    // next 300 ms are recorded (the "prepare" of the result), and then it is
    // shown that board, made again (so that the scene tells it by its faces).
    property var changeJobs: []
    property var changeJob: null
    property var changeResults: []

    function startChange() {
        const queued = [];
        for (let r = 0; r < runCount; ++r) {
            for (const size of autoSizes) {
                queued.push({ size: size, run: r });
            }
        }
        changeJobs = queued;
        showTiles = false;
        nextChange();
    }

    function nextChange() {
        if (changeJobs.length === 0) {
            const report = { graphicsApi: stats.graphicsApi(), probeMode: probeMode, boards: kind,
                             prepared: prepareFirst,
                             window: { width: width, height: height }, runs: changeResults };
            stats.writeText(outDir + "/change-" + probeMode + ".json", JSON.stringify(report, null, 1));
            console.log("RESULTS " + outDir + "/change-" + probeMode + ".json");
            Qt.quit();
            return;
        }
        changeJob = changeJobs.shift();
        changePhase = 0;
        changePrepared = null;
        changeTimer.restart();
    }

    // 0: waiting to ask for the board; 1: waiting out the frames of prepare();
    // 2: waiting out the frames of show().
    property int changePhase: 0
    property var changePrepared: null
    property double showCallMs: 0

    // Waits 300 ms (so nothing of the last board is in the frames), then asks
    // for the new board and records the frames of the next 300 ms.
    Timer {
        id: changeTimer

        interval: 300
        onTriggered: {
            if (window.changePhase === 0) {
                stats.begin();
                window.boardSize = window.changeJob.size;
                window.seed = 500 + window.changeJob.run;
                const askedAt = stats.now();
                if (window.prepareFirst) {
                    const ahead = window.currentBoard();
                    window.scene.prepare(ahead);
                    window.showCallMs = stats.now() - askedAt;
                    window.changePhase = 1;
                } else {
                    const board = window.currentBoard();
                    window.scene.show(board);
                    window.showCallMs = stats.now() - askedAt;
                    window.changePhase = 2;
                }
                changeTimer.restart();
                return;
            }
            if (window.changePhase === 1) {
                const ahead = stats.end();
                ahead.prepareCallMs = window.showCallMs;
                window.changePrepared = ahead;
                stats.begin();
                const askedAt = stats.now();
                window.scene.show(window.currentBoard());
                window.showCallMs = stats.now() - askedAt;
                window.changePhase = 2;
                changeTimer.restart();
                return;
            }
            const measured = stats.end();
            // How long the board and show() held the GUI thread (the dice and
            // the atlas's items made), before any frame.
            measured.showCallMs = window.showCallMs;
            if (window.changePrepared !== null) {
                measured.prepare = window.changePrepared;
            }
            measured.size = window.changeJob.size;
            measured.run = window.changeJob.run;
            window.changeResults.push(measured);
            window.changeJob = null;
            Qt.callLater(window.nextChange);
        }
    }

    function startAuto() {
        if (changeOnly) {
            startChange();
            return;
        }
        if (shotsOnly) {
            takeShots();
            return;
        }
        const queued = [];
        for (const size of autoSizes) {
            for (const reducedMotion of [false, true]) {
                for (let r = 0; r < runCount; ++r) {
                    queued.push({ size: size, reduced: reducedMotion, seed: 100 + r, index: r, rattle: false });
                }
            }
        }
        // Then a shake held as hard as the device can give it, which runs the
        // rattle to its three-second limit.
        for (const size of autoSizes) {
            for (let r = 0; r < Math.min(runCount, 3); ++r) {
                queued.push({ size: size, reduced: false, seed: 200 + r, index: r, rattle: true });
            }
        }
        // And one shake driven through the pointer, the way a hand does it.
        queued.push({ size: autoSizes.indexOf(4) >= 0 ? 4 : autoSizes[0], reduced: false, seed: 300,
                      index: 0, rattle: false, drag: true });
        jobs = queued;
        nextJob();
    }

    function nextJob() {
        if (jobs.length === 0) {
            finishAuto();
            return;
        }
        job = jobs.shift();
        boardSize = job.size;
        reduced = job.reduced;
        seed = job.seed;
        scene.reducedMotion = job.reduced;
        scene.mockBurstMs = 0;
        showTiles = false;
        scene.show(currentBoard());
        settleTimer.restart();
    }

    // A pause between runs, so one does not start in the tail of the last.
    Timer {
        id: settleTimer

        interval: 350
        onTriggered: {
            stats.begin();
            // A shake held as long as the script allows: a long burst.
            window.scene.mockBurstMs = window.job.rattle ? 4000 : 0;
            window.peakLevel = 0;
            window.peakTilt = 0;
            window.detector = Script.newTracker();
            window.shake();
            if (window.job.drag) {
                // Across the scene and back for about two seconds.
                stats.dragBetween(sceneHost.width * 0.15, sceneHost.height * 0.3,
                                  sceneHost.width * 0.85, sceneHost.height * 0.7, 120, 16);
            }
        }
    }

    onPlayDone: {
        if (!autoRun || job === null) {
            return;
        }
        const measured = stats.end();
        scene.mockBurstMs = 0;
        measured.size = job.size;
        measured.reduced = job.reduced;
        measured.rattle = job.rattle;
        measured.drag = job.drag === true;
        measured.peakLevel = peakLevel;
        measured.peakTilt = peakTilt;
        measured.run = job.index;
        measured.playMs = stats.now() - playStartedAt;
        results.push(measured);
        job = null;
        Qt.callLater(nextJob);
    }

    function finishAuto() {
        const report = {
            graphicsApi: stats.graphicsApi(),
            probeMode: probeMode,
            boards: kind,
            lean: leanDegrees,
            window: { width: width, height: height },
            windowFirstFrameMs: windowFirstFrameMs,
            creation: creation,
            runs: results
        };
        const text = JSON.stringify(report, null, 1);
        stats.writeText(outDir + "/results-" + probeMode + ".json", text);
        console.log("RESULTS " + outDir + "/results-" + probeMode + ".json");
        Qt.quit();
    }

    // Screenshots through one play, taken by seeking the script to fixed
    // times, so they are the same on every run and show the stages where they
    // are: the rattle, the lid, the drop in flight, the settle.
    readonly property var shotTimes: {
        if (framesPerSecond <= 0) {
            return [0, 350, 800, 1000, 1150, 1300, 1450, 1650, 1900, 2300];
        }
        // Every frame of the first 3 s: the shake, the lid, the
        // drop and the settle, and a little of the board at rest.
        const times = [];
        for (let t = 0; t <= 3000; t += 1000 / framesPerSecond) {
            times.push(Math.round(t));
        }
        return times;
    }
    property int shotIndex: 0

    function takeShots() {
        window.showTiles = false;
        window.seed = 7;
        shotIndex = 0;
        shotTimer.restart();
    }

    Timer {
        id: shotTimer

        interval: window.framesPerSecond > 0 ? 40 : 120
        onTriggered: {
            if (window.shotIndex >= window.shotTimes.length) {
                window.showTiles = true;
                window.scene.show(window.currentBoard());
                afterTiles.restart();
                return;
            }
            const ms = window.shotTimes[window.shotIndex];
            window.scene.seek(window.currentBoard(), window.seed, ms);
            grabTimer.restart();
        }
    }

    Timer {
        id: grabTimer

        interval: window.framesPerSecond > 0 ? 40 : 120
        onTriggered: {
            const name = "shot-" + String(window.shotIndex).padStart(4, "0") + "-" + window.shotTimes[window.shotIndex] + "ms.png";
            stats.grab(window.outDir + "/" + name);
            window.shotIndex += 1;
            shotTimer.restart();
        }
    }

    // The settled board with the 2D layer over it, to show they line up.
    Timer {
        id: afterTiles

        interval: 250
        onTriggered: {
            stats.grab(window.outDir + "/shot-tiles.png");
            Qt.quit();
        }
    }
}
