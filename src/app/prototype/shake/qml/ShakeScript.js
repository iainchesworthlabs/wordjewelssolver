.pragma library

// The shake, the lid and the drop as a script: what every die, the box, the lid
// and the camera are doing at each moment of a play. It is plain JavaScript with
// no QML in it and no renderer, so the Qt Quick 3D scene (BoardScene.qml) is one
// way to draw it and a 2D scene would be another, and the two replay the same
// board and seed the same way.
//
// There is no physics engine. Everything is computed from the board, the seed
// and the time. The one live input is the device's motion: `agitation` and the
// tilt change how hard the box rattles and for how long, and never where a die
// lands.
//
//   state = create(board, seed, layout)   the script for a play, at time zero
//   advance(state, dtMs, agitation, tiltX, tiltY)   moves it on; collects events
//   evaluate(state)                       writes the pose of everything now
//
// The pose is in state.px, py, pz (the die's centre, in the layout's pixels
// from the centre of the board, y up, z up from the floor), rx, ry, rz
// (degrees), opacity, shadowRadius, shadowOpacity, and state.boxRx, boxRy,
// lidAngle, lidLift, lidOpacity, leanFraction. The events (state.events) are
// { kind: "rattled" }, { kind: "landed", index, pitch } and { kind: "settled" }.

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

// board is { rows, columns, tiles }; layout is { tile, gap, pitch, reduced }, in
// the pixels the scene is drawn in.
function create(board, seed, layout) {
    var rowCount = board.rows;
    var columnCount = board.columns;
    var count = rowCount * columnCount;
    var rand = random(seed);
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
            landed: false
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
    evaluate(state);
    return state;
}

function enter(state, name) {
    state.stage = name;
    state.stageStart = state.clock;
}

// A play that has already ended: the dice down, nothing moving.
function finish(state) {
    enter(state, "idle");
    evaluate(state);
}

// Moves the script on by dtMs of the device's motion.
function advance(state, dtMs, agitation, tiltX, tiltY) {
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

        if (state.reduced) {
            // Nothing moves: the dice are on their cells and fade in once the
            // lid has gone.
            alpha = state.stage === "drop" ? smooth(inStage / dropFadeMs)
                : (state.stage === "settle" || resting ? 1 : 0);
        } else if (dropMs < 0 || (dropMs !== Infinity && dropMs < die.startMs)) {
            // In the box.
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
    var frameMs = 1000 / 60;
    for (var t = 0; t < ms; t += frameMs) {
        advance(state, Math.min(frameMs, ms - t), 0, 0, 0);
    }
    evaluate(state);
    return state;
}

// A checksum of every pose a board and seed produce, frame by frame at 60 Hz
// with the device still. The same board, seed and layout give the same string.
function digest(board, seed, layout) {
    var state = create(board, seed, layout);
    var sum = 0x811C9DC5;
    for (var frame = 0; state.stage !== "idle" && frame < 1000; ++frame) {
        advance(state, 1000 / 60, 0, 0, 0);
        evaluate(state);
        for (var i = 0; i < state.count; ++i) {
            var values = [state.px[i], state.py[i], state.pz[i], state.rx[i], state.ry[i], state.rz[i]];
            for (var v = 0; v < values.length; ++v) {
                sum = Math.imul(sum ^ (Math.round(values[v] * 100) | 0), 0x01000193);
            }
        }
    }
    return (sum >>> 0).toString(16);
}
