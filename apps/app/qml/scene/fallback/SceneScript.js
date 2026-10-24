// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// The shake, the lid, the drop and the settle of the 2D placeholder scene
// (BoardScene.qml), as pure functions of the board's size, the seed and the time:
// no QML and no renderer in here. The stages and their times are the starting
// values of docs/design/board.md ("The stages"); the 3D scene being built
// separately keeps the same stages and the same seeded order, so a replay is the
// same in either.
//
//   shake   the box rattles through about 8 degrees, at least 0.7 s, 3 s at most
//   lid     lifts and fades, 250 ms
//   drop    each die falls from one tile above, 280 to 420 ms, started in a seeded
//           order; the gap between starts is the smaller of 30 ms and 700 ms over
//           the dice; a rebound of an eighth of the height on landing
//   settle  the letters straighten, 150 ms
//
// Reduced motion: the box is still and rattles once, 300 ms; the lid cross-fades,
// 120 ms; the dice fade in together, 120 ms. 0.54 s in all.

var SHAKE_MIN = 700;
var SHAKE_MAX = 3000;
var LID = 250;
var SETTLE = 150;
var GAP_CAP = 30;
var GAP_SPREAD = 700;
var DROP_MIN = 280;
var DROP_MAX = 420;
var REDUCED_SHAKE = 300;
var REDUCED_LID = 120;
var REDUCED_DROP = 120;
var RATTLE_GAP = 125;   // ms: at most 8 rattles a second
var HIT = 0.8;          // the share of a fall at which the die meets the floor
var REBOUND = 0.125;    // of the fall's height

function mulberry32(seed) {
    var a = seed >>> 0;
    return function () {
        a = (a + 0x6D2B79F5) >>> 0;
        var t = a;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

function clamp01(x) {
    return x < 0 ? 0 : (x > 1 ? 1 : x);
}

// Everything that varies between plays of different seeds, drawn once.
function create(count, seed, reduced) {
    var rnd = mulberry32(seed);
    var order = [];
    var i;
    for (i = 0; i < count; ++i) {
        order.push(i);
    }
    for (i = count - 1; i > 0; --i) {
        var j = Math.floor(rnd() * (i + 1));
        var swap = order[i];
        order[i] = order[j];
        order[j] = swap;
    }

    var gap = Math.min(GAP_CAP, GAP_SPREAD / Math.max(1, count));
    var script = {
        count: count,
        reduced: reduced,
        order: order,
        start: [],
        duration: [],
        phase1: [],
        phase2: [],
        phase3: [],
        restTilt: [],
        pitch: [],
        dropLength: 0
    };
    for (i = 0; i < count; ++i) {
        script.start.push(0);
        script.duration.push(0);
        script.phase1.push(rnd() * 2 * Math.PI);
        script.phase2.push(rnd() * 2 * Math.PI);
        script.phase3.push(rnd() * 2 * Math.PI);
        script.restTilt.push((rnd() - 0.5) * 12);
        script.pitch.push(0.9 + rnd() * 0.2);
    }
    for (var k = 0; k < count; ++k) {
        var die = order[k];
        script.start[die] = k * gap;
        script.duration[die] = DROP_MIN + Math.floor(rnd() * (DROP_MAX - DROP_MIN + 1));
        script.dropLength = Math.max(script.dropLength, script.start[die] + script.duration[die]);
    }
    return script;
}

// How long a stage lasts, apart from the shake, whose end depends on the device.
function stageLength(script, stage) {
    if (script.reduced) {
        return stage === "shake" ? REDUCED_SHAKE : stage === "lid" ? REDUCED_LID
             : stage === "drop" ? REDUCED_DROP : 0;
    }
    return stage === "lid" ? LID : stage === "drop" ? script.dropLength
         : stage === "settle" ? SETTLE : 0;
}

// The time at which die `i` meets the floor, in the drop stage.
function landingTime(script, i) {
    if (script.reduced) {
        return REDUCED_DROP;
    }
    return script.start[i] + script.duration[i] * HIT;
}

// Where a die is, away from its tile: x and y in pixels, tilt in degrees, scale,
// opacity, and how dark its shadow is (0 to 1).
function pose(script, stage, t, i, tile) {
    var p = { x: 0, y: 0, tilt: 0, scale: 1, opacity: 1, shadow: 0 };
    if (stage === "idle") {
        return p;
    }
    if (script.reduced) {
        if (stage === "shake" || stage === "lid") {
            p.opacity = 0;
        } else if (stage === "drop") {
            p.opacity = clamp01(t / REDUCED_DROP);
        }
        return p;
    }

    var swing = tile * 0.07;
    if (stage === "shake" || stage === "lid") {
        var calm = stage === "lid" ? 1 - clamp01(t / LID) : 1;
        p.x = swing * Math.sin(0.031 * t + script.phase1[i]) * calm;
        p.y = swing * Math.sin(0.043 * t + script.phase2[i]) * calm;
        p.tilt = 12 * Math.sin(0.024 * t + script.phase3[i]) * calm + script.restTilt[i] * (1 - calm);
        p.scale = 0.9;
        p.shadow = 0.5;
    } else if (stage === "drop") {
        var local = t - script.start[i];
        var rest = script.restTilt[i];
        if (local < 0) {
            p.tilt = rest;
            p.scale = 0.9;
            p.shadow = 0.5;
        } else if (local < script.duration[i]) {
            var u = local / script.duration[i];
            if (u < HIT) {
                var f = u / HIT;
                p.y = -tile * (1 - f * f);
                p.shadow = f * f;
            } else {
                p.y = -tile * REBOUND * Math.sin(Math.PI * (u - HIT) / (1 - HIT));
                p.shadow = 1;
            }
            p.tilt = rest * (1 - 0.5 * u);
            p.scale = 1 + 0.12 * (1 - u);
        } else {
            p.tilt = rest * 0.5;
            p.shadow = 1;
        }
    } else if (stage === "settle") {
        p.tilt = script.restTilt[i] * 0.5 * (1 - clamp01(t / SETTLE));
        p.shadow = 1 - clamp01(t / SETTLE);
    }
    return p;
}

// The box's lean while it rattles, in degrees. `agitation` (0 to 1) is how hard
// the device is being shaken now, `lean` (-1 to 1) a tilt to one side. They change
// how the box moves and never where a die lands.
function boxTilt(script, stage, t, agitation, lean) {
    if (script.reduced || stage !== "shake") {
        return 0;
    }
    var strength = 0.6 + 0.4 * Math.max(0, Math.min(1, agitation));
    return 8 * strength * Math.sin(0.028 * t) + 4 * Math.max(-1, Math.min(1, lean));
}

// The lid: how far it has lifted (0 closed, 1 gone).
function lidOpen(script, stage, t) {
    if (stage === "shake") {
        return 0;
    }
    if (stage === "lid") {
        return clamp01(t / stageLength(script, "lid"));
    }
    return 1;
}
