// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import WordJewelsSolver

// The replica of the real board in `assist.playing` (docs/design/game.md, "The
// MVP: one capture"): the tiles as they were confirmed, the path being traced,
// and the start tile the help lights. It is 2D, not the 3D scene: Mode A's board
// is a picture of the family's real one, and a child reads it better upright
// (docs/design/game.md, "The check screen").
//
// It is the same object as Solo Play's flat board, drawn from the same parts in
// the same order: the kit's tray with its gold rim (BoardTray), the cream die
// faces (TileFace), the path as a ribbon over the dice and under the letters,
// then the letters in the learning face, the keyboard ring, the start ring and
// the pointer. The old replica was flat outlined squares on bare paper, a
// different toy from the one the child plays on the tablet.
//
// The touch logic is the model's (PathTracer, the same as Solo Play): this item
// forwards the first pointer and draws what the model says. A path that is not a
// word fades where it was; the "real, not today's" tick fades the same way and
// the model chooses what cue to send (no word is ever shown for it).
//
// The board's origin is this item's top left. `tile` and `gap` are the layout
// rule's, from SoloLayout, so the replica lands exactly where Solo Play's board
// would; `inset` is the board area's inset, which the tray's outset must fit in.
Item {
    id: root

    required property AssistModel assist
    property real tile: 1
    property real gap: 0
    // The board area's inset (SoloLayout.inset): the tray is as far outside the
    // tiles as that leaves room for.
    property real inset: 12

    readonly property int rows: root.assist.rows
    readonly property int columns: root.assist.columns
    readonly property real pitch: root.tile + root.gap
    readonly property real side: root.columns > 0
                                ? root.columns * root.tile + (root.columns - 1) * root.gap
                                : 0
    readonly property real trayOutset: Theme.trayOutset(root.tile, root.inset)
    // The slack around the board in which a finger still counts as on it.
    readonly property real slack: Math.max(Theme.targetChild * 0.35, root.tile * 0.35)
    // The path's width grows with the tile, so on a big die it reads as a line a
    // finger drew and not a hair across the letters.
    readonly property real coreWidth: Math.max(Theme.pathCoreWidth, root.tile * Theme.pathCoreShare)
    readonly property real casingWidth: Math.max(Theme.pathCasingWidth, root.tile * Theme.pathCasingShare)

    property int focusIndex: 0
    property bool keyboardUsed: false

    activeFocusOnTab: true
    Accessible.role: Accessible.Table
    Accessible.name: ScanWording.board

    function tileX(index) {
        return (index % Math.max(root.columns, 1)) * root.pitch;
    }
    function tileY(index) {
        return Math.floor(index / Math.max(root.columns, 1)) * root.pitch;
    }
    function centre(index) {
        return Qt.point(root.tileX(index) + root.tile / 2, root.tileY(index) + root.tile / 2);
    }
    function points(indices) {
        return indices.map(i => root.centre(i));
    }
    function isLit(index) {
        return root.assist.path.indexOf(index) >= 0;
    }
    // The line to draw: the path being made, one array for both stroked paths.
    readonly property var linePoints: root.points(root.assist.path)
    readonly property var ghostPoints: root.points(root.ghostPath)

    // A path that was not a word (or a word that is not today's) fades out where
    // it was. The model says which; the fade is the same for both (docs/design/
    // game.md, "Words and what each does").
    property var ghostPath: []
    property string ghostKind: ""

    Connections {
        target: root.assist

        function onTraced(kind, path) {
            if (kind === "tick" || kind === "fade") {
                root.ghostPath = path;
                root.ghostKind = kind;
                ghostFade.restart();
            }
        }
        function onBoardChanged() {
            root.focusIndex = 0;
        }
    }

    NumberAnimation {
        id: ghostFade
        target: ghost
        property: "opacity"
        from: 1
        to: 0
        duration: root.ghostKind === "tick" ? Theme.quick * 2 : Theme.pathFade
    }

    // --- the tray ----------------------------------------------------------------
    BoardTray {
        visible: root.rows > 0
        x: -root.trayOutset
        y: -root.trayOutset
        width: root.side + 2 * root.trayOutset
        height: root.side + 2 * root.trayOutset
    }

    // --- the die faces -----------------------------------------------------------
    Repeater {
        model: root.assist.tiles

        Item {
            id: cell
            required property int index
            required property string modelData

            x: root.tileX(cell.index)
            y: root.tileY(cell.index)
            width: root.tile
            height: root.tile

            // The letter is drawn again above the path, so the face is only the
            // die itself here.
            TileFace {
                anchors.fill: parent
                letters: ""
                lit: root.isLit(cell.index)
            }

            // "A, row 2, column 3": named by its letter and its place.
            Accessible.role: Accessible.Button
            Accessible.name: ScanWording.tileName(
                cell.modelData, Math.floor(cell.index / Math.max(root.columns, 1)) + 1,
                cell.index % Math.max(root.columns, 1) + 1)
            Accessible.checked: root.isLit(cell.index)
            Accessible.onPressAction: root.assist.tapTile(cell.index)
        }
    }

    // --- the path: a pale casing over a dark core, over the dice, under the letters
    Shape {
        anchors.fill: parent
        visible: root.assist.path.length > 1
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.pathCasing
            strokeWidth: root.casingWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.linePoints }
        }
        ShapePath {
            strokeColor: Theme.pathCore
            strokeWidth: root.coreWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.linePoints }
        }
    }

    // --- a path that fades ---------------------------------------------------------
    Shape {
        id: ghost
        anchors.fill: parent
        opacity: 0
        visible: opacity > 0 && root.ghostPath.length > 1
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.pathCasing
            strokeWidth: root.casingWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.ghostPoints }
        }
        ShapePath {
            strokeColor: Theme.pathCore
            strokeWidth: root.coreWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.ghostPoints }
        }
    }

    // --- the letters ---------------------------------------------------------------
    // The same Andika face as TileFace, above the ribbon. The lit tile's ink is
    // unchanged: the gold edge and the line say "selected" without recolouring
    // the letter a child is reading.
    Repeater {
        model: root.assist.tiles

        Label {
            required property int index
            required property string modelData
            visible: modelData !== ""
            x: root.tileX(index)
            y: root.tileY(index)
            width: root.tile
            height: root.tile
            decorative: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: modelData === "QU" ? "Qu" : modelData
            font.family: Theme.fontFamily
            font.weight: Font.Bold
            size: root.tile * Theme.letterShare * (modelData === "QU" ? Theme.quShare : 1)
            lineRatio: 1.0
            color: Theme.dieInk
        }
    }

    // The tile the keyboard is on.
    Rectangle {
        visible: root.activeFocus && root.keyboardUsed && root.focusIndex >= 0
                 && root.focusIndex < root.assist.tiles.length
        x: root.focusIndex >= 0 ? root.tileX(root.focusIndex) - Theme.litEdge : 0
        y: root.focusIndex >= 0 ? root.tileY(root.focusIndex) - Theme.litEdge : 0
        width: root.tile + 2 * Theme.litEdge
        height: width
        radius: Theme.radiusSmall + Theme.litEdge
        color: "transparent"
        border.width: Theme.heavy
        border.color: Theme.gold
    }

    // --- the start tile: a ring, shape and not colour alone -----------------------
    Rectangle {
        id: startRing
        objectName: "startRing"
        visible: root.assist.litTile >= 0
        x: root.assist.litTile >= 0 ? root.tileX(root.assist.litTile) - Theme.startRing : 0
        y: root.assist.litTile >= 0 ? root.tileY(root.assist.litTile) - Theme.startRing : 0
        width: root.tile + 2 * Theme.startRing
        height: width
        radius: Theme.radiusSmall + Theme.startRing
        color: "transparent"
        border.width: Theme.litEdge
        border.color: Theme.gold

        Accessible.role: Accessible.Graphic
        Accessible.name: ScanWording.startHere

        // Never for more than five seconds (WCAG 2.2.2): five pulses, then it
        // holds. Reduced motion keeps it still.
        SequentialAnimation on scale {
            running: startRing.visible && !Theme.reducedMotion
            loops: 5
            NumberAnimation { from: 1.0; to: 1.06; duration: Theme.ringPulse; easing.type: Easing.InOutQuad }
            NumberAnimation { from: 1.06; to: 1.0; duration: Theme.ringPulse; easing.type: Easing.InOutQuad }
        }
    }

    // --- the pointer ---------------------------------------------------------------
    // Only the first touch draws: a MouseArea receives the events synthesised
    // from the first touch point, so a second finger or a resting palm is
    // ignored (docs/design/application.md, "Input").
    MouseArea {
        id: pointer
        x: -root.slack
        y: -root.slack
        width: root.side + 2 * root.slack
        height: root.side + 2 * root.slack
        enabled: root.rows > 0
        preventStealing: true

        function local(mouseX, mouseY) {
            return Qt.point(mouseX + pointer.x, mouseY + pointer.y);
        }
        function over(mouseX, mouseY) {
            const p = local(mouseX, mouseY);
            const s = root.slack;
            return p.x >= -s && p.y >= -s && p.x <= root.side + s && p.y <= root.side + s;
        }

        onPressed: mouse => {
            root.keyboardUsed = false;
            root.forceActiveFocus();
            const p = pointer.local(mouse.x, mouse.y);
            root.assist.pressAt(p.x, p.y);
        }
        onPositionChanged: mouse => {
            const p = pointer.local(mouse.x, mouse.y);
            root.assist.moveTo(p.x, p.y);
        }
        onReleased: mouse => {
            const p = pointer.local(mouse.x, mouse.y);
            root.assist.releaseAt(p.x, p.y, pointer.over(mouse.x, mouse.y));
        }
        onCanceled: root.assist.clearPath()
    }

    // --- the keyboard and the switch -------------------------------------------------
    Keys.onPressed: event => {
        if (root.rows === 0) {
            return;
        }
        root.keyboardUsed = true;
        const row = Math.floor(root.focusIndex / Math.max(root.columns, 1));
        const column = root.focusIndex % Math.max(root.columns, 1);
        switch (event.key) {
        case Qt.Key_Left:
            root.focusIndex = row * root.columns + Math.max(0, column - 1);
            event.accepted = true;
            break;
        case Qt.Key_Right:
            root.focusIndex = row * root.columns + Math.min(root.columns - 1, column + 1);
            event.accepted = true;
            break;
        case Qt.Key_Up:
            root.focusIndex = Math.max(0, row - 1) * root.columns + column;
            event.accepted = true;
            break;
        case Qt.Key_Down:
            root.focusIndex = Math.min(root.rows - 1, row + 1) * root.columns + column;
            event.accepted = true;
            break;
        case Qt.Key_Space:
            root.assist.tapTile(root.focusIndex);
            event.accepted = true;
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.assist.done();
            event.accepted = true;
            break;
        case Qt.Key_Backspace:
            root.assist.removeLast();
            event.accepted = true;
            break;
        case Qt.Key_Escape:
            if (root.assist.path.length > 0) {
                root.assist.clearPath();
                event.accepted = true;
            }
            break;
        default:
            break;
        }
    }
}
