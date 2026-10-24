// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// The size matrix: the window sizes and text scales a screen of the application
// is held to, and the audit that says what is wrong with a laid-out item tree.
// SizeMatrixCase.qml runs a screen through the matrix; this file is the part
// that reads the tree, so a suite can call it on its own as well.
//
// The sizes are the design's own: the 900 x 620 minimum window, a tall tablet
// held upright (900 x 1200), a 4:3 tablet (1024 x 768), a big monitor
// (2400 x 1200) and 150% text on the two smaller ones.

var sizes = [
    {w: 900, h: 620, scale: 1.0},
    {w: 900, h: 1200, scale: 1.0},
    {w: 1024, h: 768, scale: 1.0},
    {w: 2400, h: 1200, scale: 1.0},
    {w: 900, h: 620, scale: 1.5},
    {w: 1024, h: 768, scale: 1.5}
];

var epsilon = 0.6;

function typeName(item) {
    return item.toString().replace(/\(0x[0-9a-f]+\)$/i, "").replace(/_QMLTYPE_\d+$/, "")
                          .replace(/_QML_\d+$/, "");
}

function nameOf(item) {
    const named = item.accessibleName || item.label || item.text || item.title || item.objectName || "";
    return typeName(item) + (named ? " \"" + named + "\"" : "");
}

function isMouseArea(item) {
    return typeName(item).indexOf("MouseArea") >= 0;
}

// An item a person can press: focusable on Tab (buttons, cards, rows made
// focusable) or a MouseArea of its own that no interactive parent already is.
function isInteractive(item) {
    if (item.activeFocusOnTab === true) {
        return true;
    }
    return false;
}

function isScrollContent(parent, child) {
    return parent.contentItem !== undefined && parent.contentItem === child;
}

function walk(item, visit) {
    visit(item);
    for (let i = 0; i < item.children.length; ++i) {
        walk(item.children[i], visit);
    }
}

function truncatedDescendants(item) {
    const found = [];
    walk(item, function (it) {
        if (it !== item && it.truncated === true && it.visible) {
            found.push(it);
        }
    });
    return found;
}

// The problems with the interactive items under `root`, as readable strings:
//   - smaller than `min` (default 48) in either direction;
//   - not wholly inside the viewport `root`, or inside an ancestor that clips
//     (a Flickable's content may run off the end of its scrolling direction);
//   - holding a label that is truncated ("Cl...");
//   - overlapping another interactive item that is not its ancestor or child.
function audit(root, min) {
    const floor = min === undefined ? 48 : min;
    const found = [];
    const items = [];

    walk(root, function (item) {
        if (item === root || !item.visible || item.opacity <= 0 || item.width <= 0 || item.height <= 0) {
            return;
        }
        if (isInteractive(item)) {
            items.push(item);
        }
    });

    // Small MouseAreas that are not the face of an interactive item and not a
    // backdrop behind some (a sheet's click-eater is a MouseArea over its buttons).
    const focusables = items.slice();
    walk(root, function (item) {
        if (item === root || !isMouseArea(item) || !item.visible || !item.enabled
                || item.width <= 0 || item.height <= 0) {
            return;
        }
        for (let at = item.parent; at; at = at.parent) {
            if (focusables.indexOf(at) >= 0) {
                return;
            }
        }
        const here = item.mapToItem(root, 0, 0);
        for (const other of focusables) {
            const there = other.mapToItem(root, 0, 0);
            if (there.x >= here.x && there.y >= here.y
                    && there.x + other.width <= here.x + item.width + epsilon
                    && there.y + other.height <= here.y + item.height + epsilon) {
                return;
            }
        }
        if (item.width < root.width * 0.5 && item.height < root.height * 0.5) {
            items.push(item);
        }
    });

    const rects = [];
    for (const item of items) {
        const at = item.mapToItem(root, 0, 0);
        const rect = {item: item, x: at.x, y: at.y, w: item.width, h: item.height};
        rects.push(rect);
        const who = nameOf(item) + " at " + Math.round(at.x) + "," + Math.round(at.y)
                  + " " + Math.round(item.width) + "x" + Math.round(item.height);

        if (item.width < floor - epsilon || item.height < floor - epsilon) {
            found.push(who + ": below " + floor + " dp");
        }

        // Walk up: every clipping ancestor, and the viewport, must contain it.
        let scrolls = false;
        let child = item;
        for (let at2 = item.parent; at2; child = at2, at2 = at2.parent) {
            if (isScrollContent(at2, child)) {
                scrolls = true;
            }
            const clips = at2 === root || at2.clip === true || at2.contentItem !== undefined;
            if (!clips) {
                continue;
            }
            const p = item.mapToItem(at2, 0, 0);
            const bound = at2 === root ? at2 : at2;
            const outRight = p.x + item.width > bound.width + epsilon;
            const outLeft = p.x < -epsilon;
            const outBottom = p.y + item.height > bound.height + epsilon;
            const outTop = p.y < -epsilon;
            if (scrolls && at2.contentItem === undefined && at2 !== root) {
                // Above a Flickable the content is wherever it is scrolled to.
            }
            if (outRight || outLeft || (!scrolls && (outBottom || outTop))) {
                found.push(who + ": clipped by " + typeName(at2));
                break;
            }
        }

        for (const label of truncatedDescendants(item)) {
            found.push(who + ": label truncated (\"" + label.text + "\")");
        }
    }

    for (let i = 0; i < rects.length; ++i) {
        for (let j = i + 1; j < rects.length; ++j) {
            const a = rects[i];
            const b = rects[j];
            let related = false;
            for (let at = a.item.parent; at; at = at.parent) {
                if (at === b.item) {
                    related = true;
                }
            }
            for (let at = b.item.parent; at; at = at.parent) {
                if (at === a.item) {
                    related = true;
                }
            }
            if (related) {
                continue;
            }
            const overlapX = Math.min(a.x + a.w, b.x + b.w) - Math.max(a.x, b.x);
            const overlapY = Math.min(a.y + a.h, b.y + b.h) - Math.max(a.y, b.y);
            if (overlapX > 4 && overlapY > 4) {
                found.push(nameOf(a.item) + " overlaps " + nameOf(b.item) + " by "
                           + Math.round(overlapX) + "x" + Math.round(overlapY));
            }
        }
    }
    return found;
}
