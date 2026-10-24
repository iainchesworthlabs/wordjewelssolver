// SPDX-License-Identifier: GPL-3.0-or-later
// What shake_script_test.cpp asks of apps/app/qml/scene/ShakeScript.js.
// Plain JavaScript run in Qt's own engine, so the checks are of the script the
// scene plays and not of a copy: the script's own text is evaluated and run(S) is
// given its functions and the dice simulation (a DiceSim) it drives. It returns
// the list of faults (empty when all hold), one line for each group of checks in
// `summary`, which the test prints, and the work a budget holds in `work`.
//
// What it holds down is the part of the scene the library's roll reaches: that a
// die lands with the face the roll says is up, shows other faces on the way, and
// that none of this moves a die before it is sent home or changes where it lands.

function run(S, only) {
    var summary = [];
    var faults = [];
    // The groups of checks below are independent of one another, and the plays
    // under them are the cost of the whole: a test case asks for one group
    // (`only` is its name) so that ctest runs the groups at once. With no name,
    // all of them run.
    function want(group) {
        return !only || only === group;
    }
    function check(ok, what) {
        if (!ok) {
            faults.push(what);
        }
    }
    function near(a, b) {
        return Math.abs(a - b) < 1e-6;
    }

    // Each of the six sides can be put up, with its letter upright (the staged
    // dice are turned by these, and the table is the simulation's).
    if (want("sides")) {
        for (var k = 0; k < 6; ++k) {
            var q = S.faceUp(k, 0);
            var top = S.topSide(q);
            check(top.side === k && near(top.height, 1) && top.tilt < 1e-3, "side " + k + " is not up when asked");
            var r = S.quatRotate(q, S.sides[k].r);
            var u = S.quatRotate(q, S.sides[k].u);
            check(near(r[0], 1) && near(u[1], 1), "side " + k + " is not upright when it is up");
        }
        summary.push("six sides: each goes up, letter upright");
    }

    function board(size, seed) {
        // A pretend roll: which face is up, from a small generator.
        var x = (seed * 2654435761) >>> 0;
        var up = [];
        var faces = [];
        for (var i = 0; i < size * size; ++i) {
            x = (Math.imul(x, 1664525) + 1013904223) >>> 0;
            up.push(Math.floor(x / 4294967296 * 6));
            faces.push(["A", "B", "C", "D", "E", "F"]);
        }
        return { rows: size, columns: size, faces: faces, up: up };
    }

    function layout(weighted, reduced) {
        return { tile: 100, gap: 8, pitch: 108, reduced: reduced, weighted: weighted, lidTiles: 3.0,
                 slackTiles: weighted ? 1.4 : 0.6, sim: S.sim };
    }

    // A play ends with every die on the face the board says, flat, and the dice
    // show other faces on the way (not under reduced motion, which does not
    // tumble). The same board and seed make the same play.
    var scripts = [["weighted", true, false], ["staged", false, false], ["reduced", false, true]];
    var minimumSeen = { weighted: 2.5, staged: 1.5, reduced: 1 };
    for (var s = 0; s < scripts.length; ++s) {
        var name = scripts[s][0];
        if (!want("play " + name)) {
            continue;
        }
        var landed = 0;
        var dice = 0;
        var seenTotal = 0;
        var plays = 0;
        for (var size = 3; size <= 4; ++size) {
            for (var seed = 1; seed <= 3; ++seed) {
                var report = S.faceReport(board(size, seed), seed, layout(scripts[s][1], scripts[s][2]));
                check(report.finished, name + " " + size + "x" + size + " did not finish");
                landed += report.landed;
                dice += report.dice;
                seenTotal += report.facesSeenMean;
                plays += 1;
            }
        }
        check(landed === dice, name + ": " + landed + " of " + dice + " dice landed with the face that is up");
        var seenMean = seenTotal / plays;
        check(seenMean >= minimumSeen[name], name + ": a die showed " + seenMean.toFixed(2)
              + " faces on top on average, not " + minimumSeen[name] + " or more");
        if (name === "reduced") {
            check(seenMean === 1, "reduced motion tumbled");
        }
        summary.push(name + ": " + landed + " of " + dice + " dice land on the face that is up; a die had "
                     + seenMean.toFixed(2) + " faces on top on average");
    }

    // The same board, seed and script make the same play; another seed, or
    // another face up on one die, does not.
    for (var w = 0; w < 2 && want("replay"); ++w) {
        var weighted = w === 0;
        var lay = layout(weighted, false);
        var b = board(4, 9);
        var first = S.digest(b, 100, lay);
        check(first === S.digest(b, 100, lay), (weighted ? "weighted" : "staged") + " replay differs");
        check(first !== S.digest(b, 101, lay), (weighted ? "weighted" : "staged") + " seeds agree");
        var other = board(4, 9);
        other.up[0] = (other.up[0] + 1) % 6;
        check(first !== S.digest(other, 100, lay), (weighted ? "weighted" : "staged") + " ignores the face that is up");
    }
    if (want("replay")) {
        summary.push("replay: the same board and seed repeat; another seed or face up does not");
    }

    // The faces do not move a die until it is sent home: with another face up on
    // every die, the positions are the same frame for frame while the dice are
    // being shaken (the staged dice, which the physics does not send home, at
    // every time), and the staged dice's turn differs where the face does.
    for (var m = 0; m < 2 && want("faces"); ++m) {
        var isWeighted = m === 0;
        var lay2 = layout(isWeighted, false);
        var one = board(4, 5);
        var two = { rows: 4, columns: 4, faces: one.faces, up: one.up.map(function (v) { return (v + 3) % 6; }) };
        var moved = 0;
        var turned = 0;
        var times = isWeighted ? [0, 200, 400, 600] : [0, 300, 700, 1100, 1500, 2100];
        for (var ti = 0; ti < times.length; ++ti) {
            var sa = S.at(one, 77, lay2, times[ti]);
            var sb = S.at(two, 77, lay2, times[ti]);
            for (var i = 0; i < sa.count; ++i) {
                if (sa.px[i] !== sb.px[i] || sa.py[i] !== sb.py[i] || sa.pz[i] !== sb.pz[i]) {
                    moved += 1;
                }
                if (!isWeighted && (sa.rx[i] !== sb.rx[i] || sa.ry[i] !== sb.ry[i] || sa.rz[i] !== sb.rz[i])) {
                    moved += 1;
                }
                if (sa.tw[i] !== sb.tw[i] || sa.tx[i] !== sb.tx[i] || sa.ty[i] !== sb.ty[i] || sa.tz[i] !== sb.tz[i]) {
                    turned += 1;
                }
            }
        }
        check(moved === 0, (isWeighted ? "weighted" : "staged") + ": the faces moved " + moved + " dice");
        if (!isWeighted) {
            check(turned > 0, "staged: the face that is up did not turn anything");
        }
    }
    if (want("faces")) {
        summary.push("faces do not move a die before it is sent home: positions (and angles) equal for another set of faces up");
    }

    // A board that is shown, not played, has its faces up at once.
    for (var v = 0; v < 2 && want("show"); ++v) {
        var bb = board(4, 11);
        var state = S.create(bb, 11, layout(v === 0, false));
        S.finish(state);
        var good = 0;
        for (var d = 0; d < state.count; ++d) {
            var tt = S.topSide(v === 0 ? [state.qw[d], state.qx[d], state.qy[d], state.qz[d]]
                                       : [state.tw[d], state.tx[d], state.ty[d], state.tz[d]]);
            if (tt.side === bb.up[d] && tt.height > 0.9999) {
                good += 1;
            }
        }
        check(good === state.count, "a shown board has " + good + " of " + state.count + " dice face up");
    }
    if (want("show")) {
        summary.push("show: a board that is not played has its faces up at once");
    }

    // The work a budget holds (libs/core/bench/budgets.hpp): one weighted 4x4 play at
    // 60 Hz, frame by frame, to rest, counted in the simulation's whole steps;
    // and the cells of a board's atlas, the glyph cells and the jewel cells the
    // scene makes and draws. Both are counts, not times, so the same number is
    // expected of every machine and compiler.
    var work = { frames: 0, steps: 0, atlas: 0, atlasLetters: 0, atlasJewels: 0 };
    if (want("work")) {
        var workBoard = board(4, 7);
        var workLayout = layout(true, false);
        var workState = S.create(workBoard, 7, workLayout);
        var workTracker = S.newTracker();
        var workFrames = 0;
        while (workState.stage !== "idle" && workFrames < 1000) {
            S.advance(workState, 1000 / 60, workTracker);
            S.evaluate(workState);
            workFrames += 1;
        }
        check(workState.stage === "idle", "the work play did not finish in 1000 frames");
        var workFaces = [];
        for (var wd = 0; wd < 16; ++wd) {
            workFaces.push(wd < 8 ? ["A", "B", "C", "D", "E", "F"] : ["", "", "", "", "", ""]);
        }
        var workAtlas = S.atlasCells(4, workFaces, 128, 3, 2);
        work = {
            frames: workFrames,
            steps: S.sim.steps(),
            atlas: workAtlas.cells,
            atlasLetters: workAtlas.letters.length,
            atlasJewels: workAtlas.jewels.length
        };
        check(work.steps > 0, "the work play took no simulation step");
        check(work.atlas === 6 * 16, "a 4x4 atlas is not six cells a die");
        check(work.atlasLetters + work.atlasJewels === work.atlas, "the atlas's cells do not add up");
        summary.push("work: a 4x4 play ran " + work.frames + " frames and " + work.steps
                     + " simulation steps; a 4x4 atlas has " + work.atlas + " cells ("
                     + work.atlasLetters + " letters, " + work.atlasJewels + " jewels)");
    }

    return { faults: faults, summary: summary, work: work };
}
