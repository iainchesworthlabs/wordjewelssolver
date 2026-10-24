// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// The shake, the lid and the drop as a script: what every die, the box, the lid
// and the camera are doing at each moment of a play. It is plain JavaScript with
// no QML in it and no renderer, so the Qt Quick 3D scene (BoardScene.qml) is one
// way to draw it and a 2D scene would be another, and the two replay the same
// board and seed the same way.
//
// Where a die ends is its cell, always, and which of its faces is up: the board
// is decided before the play starts. How it gets there is computed from the
// board, the seed and the device's motion, in one of two ways (layout.weighted):
//
//   staged    every position and angle is a prescribed curve of the time: a
//             sine rattle, a fall on a parabola, one rebound. No physics.
//   weighted  the dice are simulated (layout.sim, a DiceSim: rigid cubes in a
//             box, apps/app/src/scene/sim/dice_world.hpp), in the box's own
//             frame. Each is accelerated by the *negative of what the
//             accelerometer reads* (so gravity, tilt, a shove and a jolt are all
//             one thing) and by the centrifugal, Coriolis and Euler terms of
//             the gyroscope's rotation; it slides, tumbles, hits the lid, the
//             walls and the others, with a mass, an inertia and a friction. When
//             the shaking stops the lid goes, and each die hops and slides to its
//             own cell in a seeded order (a die has faces of its own, so it is a
//             particular die and goes to the cell the roll gave it), turning
//             flat and square on the way, and over, if it has to be, to the face
//             that is up.
//
// The device's motion comes in as samples (feed): the accelerometer's reading
// in g, gravity included, and the gyroscope's in rad/s, about 100 a second.
// With no device (the Shake button, a test, a replay) a seeded stand-in makes
// the same kind of stream (mockSampleAt): strokes with sharp turnarounds, spikes
// and noise, so a replay of a board and seed is the same shake.
//
//   state = create(board, seed, layout)   the script for a play, at time zero
//   advance(state, dtMs, tracker)         moves it on; collects events
//   evaluate(state)                       writes the pose of everything now
//
// The pose is in state.px, py, pz (the die's centre, in the layout's pixels
// from the centre of the board, y up, z up from the floor) and, for the
// simulated dice, the quaternion state.qw, qx, qy, qz (state.useQuaternion), or,
// for the staged ones, rx, ry, rz (degrees); then opacity, shadowRadius,
// shadowOpacity, and state.boxRx, boxRy, lidAngle, lidLift, lidOpacity,
// leanFraction. For simulated dice, state.cell[i] is the cell of the board whose
// tile die i shows, and state.turnChanges the faces whose letters are to be
// turned a quarter turn at a time (BoardScene gives them to the dice). The
// events (state.events) are { kind: "rattled" }, { kind: "landed", index, pitch }
// and { kind: "settled" }.
//
// A die has six faces and the board says which is up (`board.faces[i]`, the six
// strings of the die on tile i, and `board.up[i]`, the index of the one that
// is). The script does not read the letters. The simulated dice lie any way the
// physics leaves them while they are shaken, so the faces show as they tumble,
// and are turned to the face that is up as they hop home. The staged dice (state.tw,
// tx, ty, tz, a quaternion for the cube alone) lie on another face in the box and
// turn over as they fall. Nothing about where a die goes or how it moves depends
// on the faces until it is sent home.

// The stages and their durations: starting values to tune on a device
// (docs/design/board.md, "The stages").
var shakeMinMs = 700;
var shakeMaxMs = 3000;
var shakeStillMs = 300;       // reduced motion: the one soft rattle
var lidMs = 250;
var lidFadeMs = 120;          // reduced motion
var dropGapMaxMs = 30;
var dropGapSpanMs = 700;
var fallMinMs = 280;
var fallMaxMs = 420;
var dropFadeMs = 120;         // reduced motion
var settleMs = 150;
var boxDegrees = 8;
var rattleTickMs = 125;       // at most 8 a second

// mulberry32: a small, fast, seedable generator.
function random(seed) {
    var a = (seed | 0) ^ 0x5DEECE6;
    return function () {
        a = (a + 0x6D2B79F5) | 0;
        var t = Math.imul(a ^ (a >>> 15), 1 | a);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

function smooth(u) {
    var x = Math.max(0, Math.min(1, u));
    return x * x * (3 - 2 * x);
}

// ---- The cells of an atlas -------------------------------------------------
// The cells a board's atlas holds: for each die, the six faces of the die that
// is on it, one cell each, laid out as the board is (three cells across and two
// down for each die). A cell with letters is drawn as a glyph; a cell with none
// is the jewel's. FaceAtlas.qml makes an item for each of these; the scene's
// work budget counts them (apps/app/tests/unit/shake_script_test.cpp), which is why the
// layout is here and not in the item: it is the scene's, and it is testable
// without a window.
//
// Returns { cells, letters, jewels }: cells is six times the dice, and letters
// plus jewels is cells.
function atlasCells(columns, faces, cell, blockColumns, blockRows) {
    var letters = [];
    var jewels = [];
    for (var die = 0; die < faces.length; ++die) {
        var sides = faces[die];
        for (var side = 0; side < blockColumns * blockRows; ++side) {
            var text = sides && sides.length > 0 ? String(sides[side % sides.length]).toUpperCase() : "";
            var x = ((die % columns) * blockColumns + side % blockColumns) * cell;
            var y = (Math.floor(die / columns) * blockRows + Math.floor(side / blockColumns)) * cell;
            if (text === "") {
                jewels.push({ x: x, y: y });
            } else {
                // About 55% of the tile for one letter; a two-letter tile (the
                // Qu face) at 80% of that, a three-letter tile smaller still.
                letters.push({ x: x, y: y, letters: text,
                               pixelSize: text.length > 2 ? 48 : (text.length === 2 ? 68 : 88) });
            }
        }
    }
    return { cells: letters.length + jewels.length, letters: letters, jewels: jewels };
}

// ---- The faces of a die ------------------------------------------------------
// The six faces of a die, numbered by the local axis they face as the simulation
// has them (sim/dice_world.hpp): +x, -x, +y, -y, +z, -z. Each has its outward
// normal n and the directions r (right) and u (up) its letter is read in when it
// is seen from outside, right-handed (r x u = n). Face k of the library's die is
// face k here, and the atlas has its cell: the library says which six letters a
// die has and not which are neighbours, so which are opposite is for show.
var sides = [
    { n: [1, 0, 0], r: [0, 1, 0], u: [0, 0, 1] },
    { n: [-1, 0, 0], r: [0, 0, 1], u: [0, 1, 0] },
    { n: [0, 1, 0], r: [0, 0, 1], u: [1, 0, 0] },
    { n: [0, -1, 0], r: [1, 0, 0], u: [0, 0, 1] },
    { n: [0, 0, 1], r: [1, 0, 0], u: [0, 1, 0] },
    { n: [0, 0, -1], r: [0, 1, 0], u: [1, 0, 0] }
];

// Quaternions are [w, x, y, z].
function quatMultiply(a, b) {
    return [a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
            a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
            a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
            a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0]];
}

function quatAxis(x, y, z, degrees) {
    var half = degrees * Math.PI / 360;
    var s = Math.sin(half);
    return [Math.cos(half), x * s, y * s, z * s];
}

// The rotation that takes r to x, u to y and n to z: the matrix whose rows are
// r, u and n.
function quatFromBasis(r, u, n) {
    var trace = r[0] + u[1] + n[2];
    var s;
    if (trace > 0) {
        s = Math.sqrt(trace + 1) * 2;
        return [s / 4, (n[1] - u[2]) / s, (r[2] - n[0]) / s, (u[0] - r[1]) / s];
    }
    if (r[0] > u[1] && r[0] > n[2]) {
        s = Math.sqrt(1 + r[0] - u[1] - n[2]) * 2;
        return [(n[1] - u[2]) / s, s / 4, (r[1] + u[0]) / s, (r[2] + n[0]) / s];
    }
    if (u[1] > n[2]) {
        s = Math.sqrt(1 + u[1] - r[0] - n[2]) * 2;
        return [(r[2] - n[0]) / s, (r[1] + u[0]) / s, s / 4, (u[2] + n[1]) / s];
    }
    s = Math.sqrt(1 + n[2] - r[0] - u[1]) * 2;
    return [(u[0] - r[1]) / s, (r[2] + n[0]) / s, (u[2] + n[1]) / s, s / 4];
}

// The way round the shorter arc from a to b, t from 0 to 1.
function quatSlerp(a, b, t) {
    var dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    var sign = dot < 0 ? -1 : 1;
    dot = Math.abs(dot);
    var wa;
    var wb;
    if (dot > 0.9995) {
        wa = 1 - t;
        wb = t * sign;
    } else {
        var angle = Math.acos(Math.min(1, dot));
        var sine = Math.sin(angle);
        wa = Math.sin((1 - t) * angle) / sine;
        wb = Math.sin(t * angle) / sine * sign;
    }
    var w = wa * a[0] + wb * b[0];
    var x = wa * a[1] + wb * b[1];
    var y = wa * a[2] + wb * b[2];
    var z = wa * a[3] + wb * b[3];
    var length = Math.sqrt(w * w + x * x + y * y + z * z);
    return [w / length, x / length, y / length, z / length];
}

// How a die lies with face k up and its letter upright, turned about the
// vertical by `quarters` quarter turns. (The staged script's dice; the
// simulated ones are turned by the simulation.)
function faceUp(k, quarters) {
    var side = sides[k];
    var turn = quatFromBasis(side.r, side.u, side.n);
    return quarters ? quatMultiply(quatAxis(0, 0, 1, 90 * quarters), turn) : turn;
}

// The vector v turned by the quaternion q.
function quatRotate(q, v) {
    var t = quatMultiply(quatMultiply(q, [0, v[0], v[1], v[2]]), [q[0], -q[1], -q[2], -q[3]]);
    return [t[1], t[2], t[3]];
}

// Which face of a die turned by q is uppermost, and how high it is (1 when the
// die is flat).
function topSide(q) {
    var best = 0;
    var height = -2;
    for (var k = 0; k < sides.length; ++k) {
        var z = quatRotate(q, sides[k].n)[2];
        if (z > height) {
            height = z;
            best = k;
        }
    }
    var right = quatRotate(q, sides[best].r);
    return { side: best, height: height, tilt: Math.abs(Math.atan2(right[1], right[0]) * 180 / Math.PI) };
}

// board is { rows, columns, faces, up } (and tiles, which the script does not
// read); layout is { tile, gap, pitch, reduced,
// weighted, lidTiles, slackTiles, mock, mockBurstMs }, in the pixels the scene
// is drawn in (lidTiles is the height of the lid's middle above the floor, and
// slackTiles how far the box's walls are past the board, both in tiles).
function create(board, seed, layout) {
    var rowCount = board.rows;
    var columnCount = board.columns;
    var count = rowCount * columnCount;
    var rand = random(seed);
    var faceRand = random((seed | 0) * 40503 + 2654435);
    var state = {
        count: count,
        rows: rowCount,
        columns: columnCount,
        tile: layout.tile,
        gap: layout.gap,
        pitch: layout.pitch,
        reduced: layout.reduced,
        stage: "shake",
        clock: 0,
        stageStart: 0,
        smoothAgitation: 0,
        tickAccumulator: 0,
        tiltX: 0,
        tiltY: 0,
        events: [],
        dice: [],
        dropMs: 0,
        px: new Array(count), py: new Array(count), pz: new Array(count),
        rx: new Array(count), ry: new Array(count), rz: new Array(count),
        tw: new Array(count), tx: new Array(count), ty: new Array(count), tz: new Array(count),
        opacity: new Array(count), shadowRadius: new Array(count), shadowOpacity: new Array(count),
        boxRx: 0, boxRy: 0, lidAngle: 0, lidLift: 0, lidOpacity: 1, leanFraction: 1
    };
    for (var i = 0; i < count; ++i) {
        var r = Math.floor(i / columnCount);
        var c = i % columnCount;
        state.dice.push({
            cx: (c - (columnCount - 1) / 2) * layout.pitch,
            cy: ((rowCount - 1) / 2 - r) * layout.pitch,
            // How it sits in the box before the lid goes: a little turned and
            // tipped, a little off its cell.
            baseX: (rand() - 0.5) * layout.gap * 0.6,
            baseY: (rand() - 0.5) * layout.gap * 0.6,
            baseYaw: (rand() - 0.5) * 16,
            baseTipX: (rand() - 0.5) * 8,
            baseTipY: (rand() - 0.5) * 8,
            // How it rattles: a rate (Hz) and a phase for each motion.
            rate: [3.5 + rand() * 3.5, 3.5 + rand() * 3.5, 5 + rand() * 4, 3 + rand() * 4, 3 + rand() * 4, 3 + rand() * 4],
            phase: [rand() * 6.2832, rand() * 6.2832, rand() * 6.2832, rand() * 6.2832, rand() * 6.2832, rand() * 6.2832],
            // How it falls: the spin it carries down, its time and its click.
            spinX: (rand() < 0.5 ? -1 : 1) * (40 + rand() * 70),
            spinY: (rand() < 0.5 ? -1 : 1) * (40 + rand() * 70),
            fallMs: fallMinMs + rand() * (fallMaxMs - fallMinMs),
            startMs: 0,
            pitchValue: 0.9 + rand() * 0.25,
            landed: false,
            // The staged dice: which face is up at the end (the board's), and how
            // the die lies in the box: some face up, turned a quarter turn this
            // way or that.
            target: faceUp(board.up && board.up[i] >= 0 && board.up[i] < sides.length ? board.up[i] : 4, 0),
            start: faceUp(Math.floor(faceRand() * sides.length), Math.floor(faceRand() * 4))
        });
    }
    // The order they fall in: a seeded shuffle, the same for the same seed.
    var order = [];
    for (var n = 0; n < count; ++n) {
        order.push(n);
    }
    for (var k = count - 1; k > 0; --k) {
        var j = Math.floor(rand() * (k + 1));
        var swap = order[k];
        order[k] = order[j];
        order[j] = swap;
    }
    var startGap = Math.min(dropGapMaxMs, dropGapSpanMs / Math.max(1, count));
    var end = 0;
    for (var m = 0; m < count; ++m) {
        var die = state.dice[order[m]];
        die.startMs = m * startGap;
        end = Math.max(end, die.startMs + die.fallMs);
    }
    state.dropMs = state.reduced ? dropFadeMs : end;
    state.seed = seed;
    state.useMock = layout.mock !== false;      // no device: the seeded stand-in
    state.mockBurstMs = layout.mockBurstMs || 0;
    state.mode = 'staged';
    if (layout.weighted && !layout.reduced) {
        initWeighted(state, board, seed, layout);
    }
    evaluate(state);
    return state;
}

// ---- The device's motion -----------------------------------------------------
// Samples are the accelerometer's specific force in g (what it reads: gravity
// included, so a device lying face up reads 0, 0, 1; x to the right, y up the
// screen, z out of the screen) and the gyroscope's rate in rad/s.
var sampleMs = 10;                // the nominal interval, 100 samples a second
var shakeStillLevel = 0.3;        // g of "hand" (gravity taken out) below which it is not being shaken
var shakeStartLevel = 0.8;        // and above which a shake has started

function newTracker() {
    // Two queues, swapped each frame: the samples that arrived go to `queue`,
    // the frame takes it and leaves the other empty for the next, so a frame
    // makes no new array (an allocation a frame is the frame's jank).
    return { queue: [], spare: [], f: [0, 0, 1], w: [0, 0, 0], wPrev: [0, 0, 0], g: [0, 0, 1], energy: 0, level: 0, count: 0 };
}

function feed(tracker, ax, ay, az, wx, wy, wz) {
    tracker.queue.push({ f: [ax, ay, az], w: [wx, wy, wz] });
    if (tracker.queue.length > 256) {
        tracker.queue.shift();
    }
    tracker.f = [ax, ay, az];
    tracker.wPrev = tracker.w;
    tracker.w = [wx, wy, wz];
    // Gravity is what the reading settles to; the rest is the hand.
    for (var i = 0; i < 3; ++i) {
        tracker.g[i] += (tracker.f[i] - tracker.g[i]) * 0.02;
    }
    var ux = ax - tracker.g[0];
    var uy = ay - tracker.g[1];
    var uz = az - tracker.g[2];
    tracker.energy += (ux * ux + uy * uy + uz * uz - tracker.energy) * 0.15;
    tracker.level = Math.sqrt(tracker.energy);
    tracker.count += 1;
}

function resetTracker(tracker) {
    tracker.queue = [];
    tracker.spare = [];
    tracker.f = [0, 0, 1];
    tracker.w = [0, 0, 0];
    tracker.wPrev = [0, 0, 0];
    tracker.g = [0, 0, 1];
    tracker.energy = 0;
    tracker.level = 0;
}

// A uniform number in [0, 1) from a seed, a sample number and a channel: the
// stand-in's noise, so that it is a function of the seed and the time.
function hash(seed, k, channel) {
    var h = Math.imul((seed | 0) ^ 0x9E3779B9, 0x85EBCA6B) ^ Math.imul(k | 0, 0xC2B2AE35)
        ^ Math.imul(channel + 1, 0x27D4EB2F);
    h ^= h >>> 15;
    h = Math.imul(h, 0x2C1B3C6D);
    h ^= h >>> 12;
    h = Math.imul(h, 0x297A2A67);
    h ^= h >>> 15;
    return (h >>> 0) / 4294967296;
}

// The stand-in for a hand shaking a device: sample number k of the stream for a
// seed. A burst of strokes: back and forth along an axis that drifts, at three
// to five hertz that wanders, rising to about 1.2 g at the ends of a stroke
// (where the hand turns round), with the up and down of the arm, a little
// ripple from the box and the dice in it, now and then a small knock, noise on
// every sample, a slow tip of the device, and a twist on the gyroscope that goes
// with the strokes. burstMs of 0 is a burst of 0.6 to 0.9 s from the seed.
function mockSampleAt(seed, k, burstMs) {
    var t = k * sampleMs / 1000;
    var burst = (burstMs > 0 ? burstMs : 800 + 400 * hash(seed, 0, 90)) / 1000;
    var two = 2 * Math.PI;
    var env = 0;
    if (t < burst) {
        env = smooth(t / 0.12) * (1 - smooth((t - (burst - 0.15)) / 0.15));
    }
    var p = [];
    for (var i = 0; i < 9; ++i) {
        p.push(two * hash(seed, 0, 100 + i));
    }
    var f0 = 3.2 + 1.4 * hash(seed, 0, 91);
    // The phase is the integral of a frequency that wanders around f0.
    var phase = two * f0 * t - 0.6 / 0.9 * Math.cos(two * 0.9 * t + p[1]);
    var stroke = Math.sin(phase) + 0.12 * Math.sin(2 * phase + p[2]);
    var amount = 2.4 * env * (0.85 + 0.15 * Math.sin(two * 1.3 * t + p[3]));
    var axis = p[0] + 0.9 * Math.sin(two * 0.7 * t + p[4]);
    var ax = amount * stroke * Math.cos(axis);
    var ay = amount * stroke * Math.sin(axis);
    var az = 1.0 * amount * Math.sin(phase + 1.2);
    var ripple = 0.08 * env * Math.sin(two * 23 * t + p[5]);
    ax += ripple; ay -= ripple;
    // A small knock, one or two samples.
    for (var back = 0; back < 2; ++back) {
        var kk = k - back;
        if (kk >= 0 && hash(seed, kk, 3) < 0.012 * env) {
            var knock = (0.25 + 0.25 * hash(seed, kk, 4)) * (hash(seed, kk, 5) < 0.5 ? -1 : 1) * (back === 0 ? 1 : 0.5);
            var which = Math.floor(hash(seed, kk, 6) * 3);
            if (which === 0) { ax += knock; } else if (which === 1) { ay += knock; } else { az += knock; }
        }
    }
    // The device tips a little as it is shaken.
    var roll = 0.05 * Math.sin(two * 1.1 * t + p[6]);
    var pitch = 0.04 * Math.sin(two * 0.9 * t + p[7]);
    var gx = -Math.sin(roll);
    var gy = Math.sin(pitch);
    var gz = Math.sqrt(Math.max(0, 1 - gx * gx - gy * gy));
    var noise = 0.02;
    var f = [gx + ax + noise * (hash(seed, k, 7) * 2 - 1),
             gy + ay + noise * (hash(seed, k, 8) * 2 - 1),
             gz + az + noise * (hash(seed, k, 9) * 2 - 1)];
    var twist = 1.6 * env * Math.sin(phase + p[8]);
    var w = [twist * Math.sin(axis) + 0.02 * (hash(seed, k, 10) * 2 - 1),
             -twist * Math.cos(axis) + 0.02 * (hash(seed, k, 11) * 2 - 1),
             0.3 * twist + 0.02 * (hash(seed, k, 12) * 2 - 1)];
    return { f: f, w: w };
}

// The samples of the stand-in stream that fall in (fromMs, toMs], fed in order.
function mockFill(tracker, seed, burstMs, fromMs, toMs) {
    var first = Math.floor(fromMs / sampleMs) + 1;
    var last = Math.floor(toMs / sampleMs);
    for (var k = first; k <= last; ++k) {
        var sample = mockSampleAt(seed, k, burstMs);
        feed(tracker, sample.f[0], sample.f[1], sample.f[2], sample.w[0], sample.w[1], sample.w[2]);
    }
}

// What the staged script takes from the stream: how hard (0 to 1) and which
// way the device is tipped (each -1 to 1).
function agitationOf(tracker) {
    return Math.min(1, tracker.level / 1.2);
}

function tiltXOf(tracker) {
    return Math.max(-1, Math.min(1, -tracker.g[0] / 0.14));
}

function tiltYOf(tracker) {
    return Math.max(-1, Math.min(1, tracker.g[1] / 0.14));
}

// ---- Weighted motion: dice in a box --------------------------------------------
// The dice are simulated by DiceSim (apps/app/src/scene/dice_sim.hpp, over
// sim/dice_world.hpp): sixteen rigid cubes in a box with a floor, four walls and
// a lid, in the box's own frame. This script holds the clock and the stages,
// feeds the device's samples to the simulation, asks it for the poses, and
// decides what the box, the lid and the camera are doing. Lengths in the
// simulation are die edges.
var weightedMinMs = 400;          // the least a shake lasts: dice have no use for a longer one, they stop when the hand does
var hopDelayMs = 120;            // after the lid starts to go: the lid is clear before the first die hops
var settleGraceMs = 1800;         // after the last die has started for its cell, then everything is put there
var lidReleaseU = 0.4;            // how far through the lid stage the dice may rise past where the lid was

function initWeighted(state, board, seed, layout) {
    state.mode = 'weighted';
    state.sim = layout.sim;
    state.sim.reset(state.rows, state.columns, layout.pitch / layout.tile, layout.slackTiles, layout.lidTiles - 0.03, seed);
    state.sim.setLid(true);
    // Which face of each die is to be up when it is home.
    state.hasFaces = !!board.up;
    state.sim.setTargets(state.hasFaces ? board.up : []);
    state.sim.setReading(0, 0, 1, 0, 0, 0, 0, 0, 0);
    state.useQuaternion = true;
    state.qw = new Array(state.count);
    state.qx = new Array(state.count);
    state.qy = new Array(state.count);
    state.qz = new Array(state.count);
    state.poses = [];
    // The cell of the atlas each die shows, and the face of it that is up with
    // the quarter turns of its letter (BoardScene gives them to the dice).
    state.cell = [];
    state.shown = [];
    state.turnChanges = [];
    state.landed = [];
    state.lastStartMs = 0;
    state.lidReleased = false;
    state.f = [0, 0, 1];
    state.w = [0, 0, 0];
    state.alpha = [0, 0, 0];
    state.zoom = 0;
    state.doneShare = 0;
    state.vibe = 0;
    state.tipX = 0;
    state.tipY = 0;
    for (var i = 0; i < state.count; ++i) {
        state.cell.push(i);
        state.shown.push(-1);
        state.landed.push(false);
        state.lastStartMs = Math.max(state.lastStartMs, state.dice[i].startMs);
    }
    readPoses(state);
}

function readPoses(state) {
    var p = state.sim.poses();
    var n = state.count;
    // The array is kept across frames: one pose array a play, not one a frame.
    var poses = state.poses;
    if (poses.length !== 7 * n) {
        poses = new Array(7 * n);
        state.poses = poses;
    }
    for (var k = 0; k < 7 * n; ++k) {
        poses[k] = p[k];
    }
    for (var i = 0; i < n; ++i) {
        state.qw[i] = poses[7 * i + 3];
        state.qx[i] = poses[7 * i + 4];
        state.qy[i] = poses[7 * i + 5];
        state.qz[i] = poses[7 * i + 6];
    }
}

// Which face of each die is up, and how its letter has to be turned to read
// upright: noted when it changes, so that the scene can turn it. `faces` is
// the simulation's one array for the frame (3 numbers a die), read once by the
// frame and shared with landedEvents: the simulation makes a list per call.
function noteFaces(state, faces) {
    for (var i = 0; i < state.count; ++i) {
        var code = faces[3 * i] * 4 + faces[3 * i + 1];
        if (state.shown[i] !== code) {
            state.shown[i] = code;
            state.turnChanges.push({ die: i, face: faces[3 * i], turns: faces[3 * i + 1] });
        }
    }
}

function landedEvents(state, faces) {
    for (var i = 0; i < state.count; ++i) {
        if (faces[3 * i + 2] === 1 && !state.landed[i]) {
            state.landed[i] = true;
            state.events.push({ kind: 'landed', index: state.cell[i], pitch: state.dice[i].pitchValue });
        }
    }
}

// What the simulation is told the device reads, for a stretch of time.
function feedSim(state, follow, seconds) {
    var f = state.f;
    var w = state.w;
    var a = state.alpha;
    state.sim.setReading(f[0] * follow, f[1] * follow, 1 + (f[2] - 1) * follow,
                         w[0] * follow, w[1] * follow, w[2] * follow,
                         a[0] * follow, a[1] * follow, a[2] * follow);
    state.sim.advance(seconds);
}

function advanceWeighted(state, dtMs, tracker) {
    state.clock += dtMs;
    var inStage = state.clock - state.stageStart;
    var sim = state.sim;
    if (state.stage === 'shake') {
        state.tickAccumulator += dtMs;
        if (state.tickAccumulator >= rattleTickMs) {
            state.tickAccumulator -= rattleTickMs;
            state.events.push({ kind: 'rattled' });
        }
        if (inStage >= weightedMinMs && (tracker.level < shakeStillLevel || inStage >= shakeMaxMs)) {
            enter(state, 'lid');
            // Which die is to be on which cell (its own, if it has faces; by the
            // least travel if not); then each starts for its cell in the seeded
            // order, once the lid is clear.
            // (With faces a die is a particular die and goes to its own cell.)
            var cells = [];
            if (state.hasFaces) {
                for (var c = 0; c < state.count; ++c) {
                    cells.push(c);
                }
            } else {
                cells = sim.assignCells();
            }
            var delays = [];
            for (var i = 0; i < state.count; ++i) {
                state.cell[i] = cells[i];
                delays.push((hopDelayMs + 0.5 * state.dice[i].startMs) / 1000);
            }
            sim.guide(delays);
        }
    } else if (state.stage === 'lid') {
        if (!state.lidReleased && inStage >= lidMs * lidReleaseU) {
            state.lidReleased = true;
            sim.setLid(false);
        }
        if (inStage >= lidMs) {
            enter(state, 'settle');
        }
    }
    inStage = state.clock - state.stageStart;

    // The samples that arrived this frame, each for its share of it. Once the lid
    // is going the device is put down: the reading fades to flat. The two queues
    // are swapped, not thrown away: `samples` keeps its length while the other
    // becomes the empty queue for the next frame's feeds (no array a frame).
    var follow = state.stage === 'shake' ? 1 : (state.stage === 'lid' ? 1 - smooth(inStage / lidMs) : 0);
    var samples = tracker.queue;
    tracker.queue = tracker.spare;
    tracker.spare = samples;
    tracker.queue.length = 0;
    if (samples.length === 0) {
        feedSim(state, follow, dtMs / 1000);
    } else {
        var share = dtMs / 1000 / samples.length;
        for (var s = 0; s < samples.length; ++s) {
            var sample = samples[s];
            var previousW = state.w;
            state.f = sample.f;
            state.w = sample.w;
            state.alpha = [Math.max(-100, Math.min(100, (sample.w[0] - previousW[0]) / (sampleMs / 1000))),
                           Math.max(-100, Math.min(100, (sample.w[1] - previousW[1]) / (sampleMs / 1000))),
                           Math.max(-100, Math.min(100, (sample.w[2] - previousW[2]) / (sampleMs / 1000)))];
            feedSim(state, follow, share);
        }
    }
    readPoses(state);
    // The camera stands back for the shake and comes in as the dice go home, so
    // that it is square by the time the last of them is down.
    state.doneShare = sim.doneCount() / state.count;
    var zoomTarget = state.stage === 'settle' ? Math.max(0, 1 - state.doneShare / 0.6) : 1;
    if (state.stage === 'idle') {
        zoomTarget = 0;
    }
    state.zoom += (zoomTarget - state.zoom) * Math.min(1, dtMs / (state.stage === 'settle' ? 70 : 160));
    if (state.stage === 'settle' || state.stage === 'lid') {
        var faces = state.sim.faces();
        noteFaces(state, faces);
        landedEvents(state, faces);
    }

    // What the box shows of the device: how it is held, which is gravity as the
    // reading settles to it, not each stroke (the dice answer to the strokes).
    var gv = tracker.g;
    var norm = Math.max(0.2, Math.sqrt(gv[0] * gv[0] + gv[1] * gv[1] + gv[2] * gv[2]));
    state.tipX = Math.asin(Math.max(-1, Math.min(1, gv[1] / norm))) * 180 / Math.PI;
    state.tipY = Math.asin(Math.max(-1, Math.min(1, -gv[0] / norm))) * 180 / Math.PI;
    state.vibe = tracker.level;

    if (state.stage === 'settle' || state.stage === 'lid') {
        if (sim.allDone()) {
            enter(state, 'idle');
            state.events.push({ kind: 'settled' });
        } else if (state.stage === 'settle' && state.clock - state.stageStart > state.lastStartMs + settleGraceMs) {
            sim.finish();
            readPoses(state);
            var finalFaces = state.sim.faces();
            noteFaces(state, finalFaces);
            landedEvents(state, finalFaces);
            enter(state, 'idle');
            state.events.push({ kind: 'settled' });
        }
    }
}

function evaluateWeighted(state) {
    var tile = state.tile;
    var inStage = state.clock - state.stageStart;
    var p = state.poses;
    for (var i = 0; i < state.count; ++i) {
        var o = 7 * i;
        var height = Math.max(0, p[o + 2] - 0.5);
        var airborne = Math.min(2, height);
        state.px[i] = p[o] * tile;
        state.py[i] = p[o + 1] * tile;
        state.pz[i] = p[o + 2] * tile;
        state.tw[i] = 1;
        state.tx[i] = 0;
        state.ty[i] = 0;
        state.tz[i] = 0;
        state.opacity[i] = 1;
        state.shadowRadius[i] = tile * 0.47 * (1 - 0.1 * Math.min(1.5, airborne));
        state.shadowOpacity[i] = 0.34 / (1 + 1.2 * airborne);
    }
    // The box is shown tipped the way the device is held, up to about eight
    // degrees, and otherwise still: a box that shivers with every stroke reads
    // as judder, and the dice already answer to the strokes.
    var limit = 8;
    state.boxRx = Math.max(-limit, Math.min(limit, state.tipX));
    state.boxRy = Math.max(-limit, Math.min(limit, state.tipY));
    // The lid.
    var lidU = state.stage === 'shake' ? 0 : (state.stage === 'lid' ? inStage / lidMs : 1);
    var lift = smooth(lidU);
    state.lidAngle = -72 * lift;
    state.lidLift = tile * 0.9 * lift;
    state.lidOpacity = state.stage === 'shake' ? 1
        : (state.stage === 'lid' ? 1 - smooth((lidU - 0.55) / 0.45) : 0);
    // The camera stands back for the shake and comes in as the dice go home.
    state.leanFraction = state.stage === 'idle' ? 0 : state.zoom;
}

function enter(state, name) {
    state.stage = name;
    state.stageStart = state.clock;
}

// A play that has already ended: the dice down, nothing moving.
function finish(state) {
    if (state.mode === 'weighted') {
        state.sim.finish();
        readPoses(state);
        var faces = state.sim.faces();
        noteFaces(state, faces);
        for (var i = 0; i < state.count; ++i) {
            state.landed[i] = true;
        }
    }
    enter(state, "idle");
    evaluate(state);
}

// Moves the script on by dtMs of the device's motion.
// Moves the script on by dtMs. The tracker is the device's motion: samples fed
// to it are what the dice answer to. With no device (layout.mock) the seeded
// stand-in feeds it.
function advance(state, dtMs, tracker) {
    if (state.useMock) {
        mockFill(tracker, state.seed, state.mockBurstMs, state.clock, state.clock + dtMs);
    }
    if (state.mode === 'weighted') {
        advanceWeighted(state, dtMs, tracker);
        return;
    }
    // Staged: how hard and which way the device is tipped, from the stream.
    // The queue is dropped, not re-made: the array is kept and emptied.
    tracker.queue.length = 0;
    advanceStaged(state, dtMs, agitationOf(tracker), tiltXOf(tracker), tiltYOf(tracker));
}

// Moves the staged script on by dtMs of the device's motion.
function advanceStaged(state, dtMs, agitation, tiltX, tiltY) {
    state.clock += dtMs;
    state.smoothAgitation += (agitation - state.smoothAgitation) * Math.min(1, dtMs / 90);
    var inStage = state.clock - state.stageStart;
    if (state.stage === "shake") {
        state.tickAccumulator += dtMs;
        if (!state.reduced && state.tickAccumulator >= rattleTickMs) {
            state.tickAccumulator -= rattleTickMs;
            state.events.push({ kind: "rattled" });
        }
        var limit = state.reduced ? shakeStillMs : shakeMaxMs;
        var least = state.reduced ? shakeStillMs : shakeMinMs;
        if (inStage >= least && (state.reduced || agitation < 0.15 || inStage >= limit)) {
            enter(state, "lid");
        }
    } else if (state.stage === "lid") {
        if (inStage >= (state.reduced ? lidFadeMs : lidMs)) {
            enter(state, "drop");
        }
    } else if (state.stage === "drop") {
        if (!state.reduced) {
            for (var i = 0; i < state.count; ++i) {
                var die = state.dice[i];
                if (!die.landed && inStage >= die.startMs + die.fallMs * 0.78) {
                    die.landed = true;
                    state.events.push({ kind: "landed", index: i, pitch: die.pitchValue });
                }
            }
        }
        if (inStage >= state.dropMs) {
            enter(state, state.reduced ? "idle" : "settle");
            if (state.reduced) {
                state.events.push({ kind: "settled" });
            }
        }
    } else if (state.stage === "settle") {
        if (inStage >= settleMs) {
            enter(state, "idle");
            state.events.push({ kind: "settled" });
        }
    }
    state.tiltX = tiltX;
    state.tiltY = tiltY;
}

// Writes the pose of everything at the script's present time.
function evaluate(state) {
    if (state.mode === 'weighted') {
        evaluateWeighted(state);
        return;
    }
    var tile = state.tile;
    var rest = tile / 2;
    var seconds = state.clock / 1000;
    var inStage = state.clock - state.stageStart;
    var energy = 0.55 + 0.45 * state.smoothAgitation;
    var tiltX = state.tiltX;
    var tiltY = state.tiltY;

    // Where each part of the sequence is: how much rattle there still is
    // (calm), how far up the dice have been lifted into the box (lifted), how
    // far through the lid, the drop and the settle.
    var calm = 0;
    var lifted = 0;
    var lidU = 1;
    var dropMs = -1;
    var settleU = 1;
    var resting = false;
    switch (state.stage) {
    case "shake":
        calm = state.reduced ? 0 : Math.min(1, inStage / 120);
        lifted = state.reduced ? 0 : smooth(inStage / 150);
        lidU = 0;
        break;
    case "lid":
        calm = state.reduced ? 0 : 1 - smooth(inStage / lidMs);
        lifted = state.reduced ? 0 : 1;
        lidU = inStage / (state.reduced ? lidFadeMs : lidMs);
        break;
    case "drop":
        lifted = state.reduced ? 0 : 1;
        dropMs = inStage;
        break;
    case "settle":
        lifted = 1;
        dropMs = Infinity;
        settleU = inStage / settleMs;
        break;
    default:
        resting = true;
        dropMs = Infinity;
        break;
    }

    var amplitude = calm * energy;
    for (var i = 0; i < state.count; ++i) {
        var die = state.dice[i];
        var x = die.cx;
        var y = die.cy;
        var z = rest;
        var rx = 0;
        var ry = 0;
        var rz = 0;
        var alpha = 1;
        var airborne = 0;     // 0 on the floor, 1 at the height of the box
        var turn = die.target;   // which face is up: the board's, but in flight

        if (state.reduced) {
            // Nothing moves: the dice are on their cells and fade in once the
            // lid has gone.
            alpha = state.stage === "drop" ? smooth(inStage / dropFadeMs)
                : (state.stage === "settle" || resting ? 1 : 0);
        } else if (dropMs < 0 || (dropMs !== Infinity && dropMs < die.startMs)) {
            // In the box, on whichever face it was left.
            turn = die.start;
            var rate = die.rate;
            var phase = die.phase;
            x += die.baseX + amplitude * state.gap * 0.35 * Math.sin(6.2832 * rate[0] * seconds + phase[0]) + tiltX * tile * 0.1;
            y += die.baseY + amplitude * state.gap * 0.35 * Math.sin(6.2832 * rate[1] * seconds + phase[1]) - tiltY * tile * 0.1;
            z = rest + lifted * (tile + amplitude * tile * 0.09 * Math.sin(6.2832 * rate[2] * seconds + phase[2]));
            rz = die.baseYaw + amplitude * 7 * Math.sin(6.2832 * rate[3] * seconds + phase[3]);
            rx = die.baseTipX + amplitude * 4 * Math.sin(6.2832 * rate[4] * seconds + phase[4]);
            ry = die.baseTipY + amplitude * 4 * Math.sin(6.2832 * rate[5] * seconds + phase[5]);
            airborne = lifted;
        } else if (dropMs !== Infinity && dropMs < die.startMs + die.fallMs) {
            // Falling.
            var u = (dropMs - die.startMs) / die.fallMs;
            var contact = 0.78;
            var w = Math.min(1, u / contact);
            var hoverZ = rest + tile;
            x = die.cx + (die.baseX + tiltX * tile * 0.1) * (1 - w);
            y = die.cy + (die.baseY - tiltY * tile * 0.1) * (1 - w);
            if (u < contact) {
                z = rest + tile * (1 - w * w);
            } else {
                // A small overshoot: a rebound of an eighth of the fall.
                var v = (u - contact) / (1 - contact);
                z = rest + tile * 0.125 * Math.sin(Math.PI * v) * (1 - v);
            }
            rz = die.baseYaw * (1 - 0.65 * Math.min(1, u));
            rx = die.baseTipX * (1 - w) + die.spinX * Math.sin(Math.PI * w) * (1 - 0.3 * w);
            ry = die.baseTipY * (1 - w) + die.spinY * Math.sin(Math.PI * w) * (1 - 0.3 * w);
            airborne = Math.max(0, (z - rest) / (hoverZ - rest));
            // It turns over as it falls, and has its face up when it lands.
            turn = quatSlerp(die.start, die.target, smooth(w));
        } else {
            // Down. The letters straighten through the settle.
            rz = die.baseYaw * 0.35 * (1 - smooth(settleU));
        }
        state.px[i] = x;
        state.py[i] = y;
        state.pz[i] = z;
        state.rx[i] = rx;
        state.ry[i] = ry;
        state.rz[i] = rz;
        state.tw[i] = turn[0];
        state.tx[i] = turn[1];
        state.ty[i] = turn[2];
        state.tz[i] = turn[3];
        state.opacity[i] = alpha;
        // A shadow that is smaller and fainter the higher the die is.
        state.shadowRadius[i] = tile * 0.47 * (1 - 0.12 * airborne);
        state.shadowOpacity[i] = alpha * 0.34 / (1 + 1.2 * airborne);
    }

    // The box: a rattle of about eight degrees that dies away as the lid goes,
    // plus the lean the device is tilted to.
    var rattleX = amplitude * boxDegrees * 0.8 * Math.sin(6.2832 * 5.3 * seconds);
    var rattleY = amplitude * boxDegrees * 0.8 * Math.sin(6.2832 * 4.1 * seconds + 1.3);
    var softX = state.reduced && state.stage === "shake"
        ? 2 * Math.sin(6.2832 * inStage / shakeStillMs) * (1 - inStage / shakeStillMs) : 0;
    state.boxRx = Math.max(-boxDegrees, Math.min(boxDegrees, rattleX - tiltY * 5 + softX));
    state.boxRy = Math.max(-boxDegrees, Math.min(boxDegrees, rattleY + tiltX * 5));

    // The lid lifts and tips away (hinged at the far edge); reduced motion fades
    // it instead.
    if (state.reduced) {
        state.lidAngle = 0;
        state.lidLift = 0;
        state.lidOpacity = state.stage === "shake" ? 1
            : (state.stage === "lid" ? 1 - smooth(lidU) : 0);
    } else {
        var lift = smooth(lidU);
        state.lidAngle = -72 * lift;
        state.lidLift = tile * 0.9 * lift;
        state.lidOpacity = state.stage === "shake" ? 1
            : (state.stage === "lid" ? 1 - smooth((lidU - 0.55) / 0.45) : 0);
    }

    // The camera comes upright over the drop and the settle. With reduced motion
    // it is upright throughout: nothing moves.
    if (state.reduced) {
        state.leanFraction = 0;
    } else if (state.stage === "shake" || state.stage === "lid") {
        state.leanFraction = 1;
    } else if (state.stage === "drop") {
        state.leanFraction = state.dropMs > 0 ? 1 - smooth(inStage / (state.dropMs + settleMs)) : 1;
    } else if (state.stage === "settle") {
        state.leanFraction = 1 - smooth((state.dropMs + inStage) / (state.dropMs + settleMs));
    } else {
        state.leanFraction = 0;
    }
}

// The script at `ms` into a play, the device still.
function at(board, seed, layout, ms) {
    var state = create(board, seed, layout);
    var tracker = newTracker();
    var frameMs = 1000 / 60;
    for (var t = 0; t < ms; t += frameMs) {
        advance(state, Math.min(frameMs, ms - t), tracker);
    }
    evaluate(state);
    return state;
}

// Every die's pose at every frame of a play at 60 Hz with the stand-in's shake,
// one line a frame: the stage, then for each die x y z (pixels) and the
// quaternion w x y z. For looking at the motion, not for the scene.
function trace(board, seed, layout) {
    var state = create(board, seed, layout);
    var tracker = newTracker();
    var rows = [];
    for (var frame = 0; state.stage !== "idle" && frame < 1000; ++frame) {
        advance(state, 1000 / 60, tracker);
        evaluate(state);
        var row = [state.stage];
        for (var i = 0; i < state.count; ++i) {
            row.push(state.px[i].toFixed(3), state.py[i].toFixed(3), state.pz[i].toFixed(3),
                     state.useQuaternion ? state.qw[i].toFixed(5) : "1", state.useQuaternion ? state.qx[i].toFixed(5) : "0",
                     state.useQuaternion ? state.qy[i].toFixed(5) : "0", state.useQuaternion ? state.qz[i].toFixed(5) : "0");
        }
        rows.push(row.join(" "));
    }
    return rows.join("\n");
}

// What a play of a board and seed does to the faces, frame by frame at 60 Hz
// with the stand-in's shake: how many dice end with the board's face on top
// (and the die flat), and how many different faces each showed uppermost on the
// way (the tips of a die in flight, which show side faces too, are not counted,
// so it is the least a viewer saw).
function topOf(state, i) {
    var q = state.useQuaternion ? [state.qw[i], state.qx[i], state.qy[i], state.qz[i]]
                                : [state.tw[i], state.tx[i], state.ty[i], state.tz[i]];
    return topSide(q);
}

function faceReport(board, seed, layout) {
    var state = create(board, seed, layout);
    var tracker = newTracker();
    var seen = [];
    for (var i = 0; i < state.count; ++i) {
        seen.push({});
    }
    var frames = 0;
    for (; state.stage !== "idle" && frames < 1000; ++frames) {
        advance(state, 1000 / 60, tracker);
        evaluate(state);
        for (var k = 0; k < state.count; ++k) {
            seen[k][topOf(state, k).side] = true;
        }
    }
    evaluate(state);
    var landed = 0;
    var total = 0;
    var most = 0;
    for (var d = 0; d < state.count; ++d) {
        var top = topOf(state, d);
        // On the board's face, the die flat; and, for the staged dice, whose
        // letters are not turned by the cube's geometry, upright.
        if (top.side === board.up[d] && top.height > 0.9999 && (state.useQuaternion || top.tilt < 0.01)) {
            landed += 1;
        }
        var distinct = Object.keys(seen[d]).length;
        total += distinct;
        most = Math.max(most, distinct);
    }
    return { dice: state.count, landed: landed, facesSeenMean: total / state.count, facesSeenMost: most,
             frames: frames, finished: state.stage === "idle" };
}

// A checksum of every pose a board and seed produce, frame by frame at 60 Hz
// with the device still. The same board, seed and layout give the same string.
function digest(board, seed, layout) {
    var state = create(board, seed, layout);
    var tracker = newTracker();
    var sum = 0x811C9DC5;
    for (var frame = 0; state.stage !== "idle" && frame < 1000; ++frame) {
        advance(state, 1000 / 60, tracker);
        evaluate(state);
        for (var i = 0; i < state.count; ++i) {
            var values = state.useQuaternion
                ? [state.px[i], state.py[i], state.pz[i], state.qw[i] * 100, state.qx[i] * 100, state.qy[i] * 100, state.qz[i] * 100]
                : [state.px[i], state.py[i], state.pz[i], state.rx[i], state.ry[i], state.rz[i],
                   state.tw[i], state.tx[i], state.ty[i], state.tz[i]];
            for (var v = 0; v < values.length; ++v) {
                sum = Math.imul(sum ^ (Math.round(values[v] * 100) | 0), 0x01000193);
            }
        }
    }
    return (sum >>> 0).toString(16);
}
