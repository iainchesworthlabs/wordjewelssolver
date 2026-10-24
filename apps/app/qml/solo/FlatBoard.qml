// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import WordJewelsSolver
import WordJewelsSolver.Scene

// The flat board the child reads and traces. It is 2D on purpose: the 3D scene
// is the shake and the drop, and this is what settles afterwards. The tray is
// the physical object; the dice keep their shape; the path is a gold ribbon
// over the dice and under the letters, so the child sees the line their finger
// draws without the line cutting through the letter.
//
// The layers matter: tray, die faces, path, letters, start ring, pointer. The
// old board drew the path under opaque dice, which left only the connectors in
// the gaps and replaced the line with 1-2-3 badges on each die.
Item {
    id: root

    required property var solo
    property rect boardRect: Qt.rect(0, 0, 0, 0)
    property real tileSize: 1
    property real tileGap: 0

    readonly property int rows: solo.rows
    readonly property int columns: solo.columns
    readonly property real pitch: tileSize + tileGap
    readonly property bool accepting: solo.state === "playing"
    readonly property bool presenting: solo.state === "word-found"
    readonly property real slack: tileSize * 0.38
    // How far the tray stands off the tiles. The screen passes the outset that
    // fits inside the board area's own inset (Theme.trayOutset with the layout's
    // inset), so the rim never leaves the area; alone it is the token's wish.
    property real trayInset: Theme.trayOutset(tileSize)
    readonly property real ribbonCore: Theme.ribbonCoreWidth(tileSize)
    readonly property real ribbonEdge: Theme.ribbonEdgeWidth(tileSize)
    readonly property real ribbonLine: Theme.ribbonLineWidth(tileSize)

    property int focusIndex: 0
    property bool keyboardUsed: false

    activeFocusOnTab: true
    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true
    Accessible.role: Accessible.Table
    Accessible.name: SoloWording.board

    function tileX(index) {
        return boardRect.x + (index % columns) * pitch;
    }
    function tileY(index) {
        return boardRect.y + Math.floor(index / columns) * pitch;
    }
    function centre(index) {
        return Qt.point(tileX(index) + tileSize / 2, tileY(index) + tileSize / 2);
    }
    function points(indices) {
        return indices.map(i => centre(i));
    }
    // The path being made, copied from the model once per change: the tiles'
    // lit tests read this one JS array instead of asking the model for a fresh
    // list each (the model builds its list per read, and a 6x6 board reads it
    // once a tile per move event: the pointer's jank is those rebuilds).
    property var pathList: []
    function isLit(index) {
        if (root.pathList.indexOf(index) >= 0) {
            return true;
        }
        return root.presenting && solo.litPath.indexOf(index) >= 0;
    }
    // The line to draw: the path being made, or the path of the word being
    // shown. One array, so the shape does not flicker at the hand-off.
    readonly property var linePath: solo.path.length > 1 ? solo.path
                                    : (root.presenting && solo.litPath.length > 1 ? solo.litPath : [])
    // The line's points, computed once per change for both stroked paths.
    readonly property var linePoints: root.points(root.linePath)
    // The fading path's points, computed once per fade for both lines.
    readonly property var ghostPoints: root.points(root.ghostPath)

    Connections {
        target: root.solo

        function onPathChanged() {
            // One copy per change: the tiles and the path lines read this.
            root.pathList = root.solo.path;
        }
        function onStateChanged() {
            if (root.solo.state === "word-found") {
                wordTimer.restart();
            }
        }
        function onBoardChanged() {
            root.focusIndex = 0;
        }
    }

    Timer {
        id: wordTimer
        interval: Theme.wordHold
        onTriggered: root.solo.wordShown()
    }

    // --- the tray ----------------------------------------------------------------
    BoardTray {
        x: root.boardRect.x - root.trayInset
        y: root.boardRect.y - root.trayInset
        width: root.boardRect.width + 2 * root.trayInset
        height: root.boardRect.height + 2 * root.trayInset
    }

    // --- the die faces ------------------------------------------------------------
    Repeater {
        model: root.solo.tiles

        Item {
            id: cell
            required property int index
            required property string modelData

            x: root.tileX(cell.index)
            y: root.tileY(cell.index)
            width: root.tileSize
            height: root.tileSize

            readonly property bool dead: modelData === ""

            TileFace {
                anchors.fill: parent
                // The letter is drawn again above the path, so the face is only
                // the die itself here.
                letters: ""
                lit: root.isLit(cell.index)
            }

            // A dead tile is a die of six jewels in the scene; on the flat board
            // it keeps the same cream die and shows the same cut stone.
            Jewel {
                visible: cell.dead
                anchors.centerIn: parent
                width: parent.width * 0.58
                height: width
                tier: "power"
                color: Theme.jewelDead
            }

            Accessible.role: cell.dead ? Accessible.Graphic : Accessible.Button
            Accessible.name: cell.dead
                ? SoloWording.jewelName(Math.floor(cell.index / root.columns) + 1,
                                        cell.index % root.columns + 1)
                : SoloWording.tileName(cell.modelData === "QU" ? "Qu" : cell.modelData,
                                       Math.floor(cell.index / root.columns) + 1,
                                       cell.index % root.columns + 1)
            Accessible.checked: root.isLit(cell.index)
            Accessible.onPressAction: root.solo.tapTile(cell.index)
        }
    }

    // --- the path -----------------------------------------------------------------
    // A ribbon through the dice's middles, under the letters: a gold edge round a
    // pale core, as wide as the die allows (14 and 22 px on a 200 px die), with a
    // thin dark line outside the gold.
    Shape {
        anchors.fill: parent
        visible: root.linePath.length > 1
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.ribbonLine
            strokeWidth: root.ribbonLine
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.linePoints }
        }
        ShapePath {
            strokeColor: Theme.ribbonEdge
            strokeWidth: root.ribbonEdge
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.linePoints }
        }
        ShapePath {
            strokeColor: Theme.ribbonCore
            strokeWidth: root.ribbonCore
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.linePoints }
        }
    }

    // A path that was not a word fades where it was.
    property var ghostPath: []
    property string ghostKind: ""
    Connections {
        target: root.solo
        function onTraced(kind, path) {
            if (kind === "tick" || kind === "fade") {
                root.ghostPath = path;
                root.ghostKind = kind;
                ghostFade.restart();
            }
        }
    }
    Shape {
        id: ghost
        anchors.fill: parent
        opacity: 0
        visible: opacity > 0 && root.ghostPath.length > 1
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.ribbonLine
            strokeWidth: root.ribbonLine
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.ghostPoints }
        }
        ShapePath {
            strokeColor: Theme.ribbonEdge
            strokeWidth: root.ribbonEdge
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.ghostPoints }
        }
        ShapePath {
            strokeColor: Theme.ribbonCore
            strokeWidth: root.ribbonCore
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.ghostPoints }
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

    // --- the letters --------------------------------------------------------------
    // The same Andika face as TileFace, above the ribbon. The lit tile's ink is
    // unchanged: the gold edge and the line say "selected" without recolouring
    // the letter a child is reading.
    Repeater {
        model: root.solo.tiles

        Text {
            required property int index
            required property string modelData
            visible: modelData !== ""
            x: root.tileX(index)
            y: root.tileY(index)
            width: root.tileSize
            height: root.tileSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: modelData === "QU" ? "Qu" : modelData
            font.family: Theme.fontFamily
            font.weight: Font.Bold
            font.pixelSize: root.tileSize * Theme.letterShare
                            * (modelData === "QU" ? Theme.quShare : 1)
            color: Theme.dieInk
            Accessible.ignored: true
        }
    }

    // The tile the keyboard is on.
    Rectangle {
        visible: root.accepting && root.activeFocus && root.keyboardUsed
                 && root.focusIndex >= 0 && root.focusIndex < root.solo.tiles.length
        x: root.focusIndex >= 0 ? root.tileX(root.focusIndex) - Theme.focusGap : 0
        y: root.focusIndex >= 0 ? root.tileY(root.focusIndex) - Theme.focusGap : 0
        width: root.tileSize + 2 * Theme.focusGap
        height: root.tileSize + 2 * Theme.focusGap
        radius: Theme.radiusSmall + Theme.focusGap
        color: "transparent"
        // Cream on the dark tray: the emerald focus colour does not show on it
        // and gold is the hint's.
        border.width: Theme.litEdge
        border.color: Theme.die
    }

    // The start tile, and the second letter when the child asks for more. One
    // ring for the first rung; two for the second, the second a size smaller so
    // the two never merge. Each is a gold band with a thin dark line outside it,
    // which holds 3:1 on the cream dice and on the tray. The word itself is never
    // shown or spoken here.
    Repeater {
        model: root.solo.helpTiles

        Rectangle {
            id: ring
            required property int index
            required property var modelData
            readonly property real reach: ring.index === 0 ? Theme.startRing : Theme.startRing / 2
            visible: root.accepting && modelData >= 0
            x: modelData >= 0 ? root.tileX(modelData) - ring.reach : 0
            y: modelData >= 0 ? root.tileY(modelData) - ring.reach : 0
            width: root.tileSize + 2 * ring.reach
            height: width
            radius: Theme.radiusSmall + ring.reach
            color: "transparent"
            border.width: Theme.hair
            border.color: Theme.dieEdge
            Accessible.role: Accessible.Graphic
            Accessible.name: ring.index === 0 ? SoloWording.startHere : SoloWording.nextLetter

            Rectangle {
                anchors.fill: parent
                anchors.margins: Theme.hair
                radius: parent.radius - Theme.hair
                color: "transparent"
                border.width: ring.index === 0 ? Theme.ringWidth : Theme.ringWidthSecond
                border.color: Theme.gold
                Accessible.ignored: true
            }

            SequentialAnimation on scale {
                running: ring.visible && !Theme.reducedMotion
                loops: 5
                NumberAnimation { from: 1.0; to: 1.06; duration: Theme.ringPulse; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 1.06; to: 1.0; duration: Theme.ringPulse; easing.type: Easing.InOutQuad }
            }
        }
    }

    // --- the pointer ---------------------------------------------------------------
    // Only the first pointer draws; the rest is ignored. The forgiving band
    // (a little beyond the board's edges) stays inside this item's bounds, so
    // it never reaches over the app bar above or the panel beside.
    readonly property real pointerLeft: Math.max(0, root.boardRect.x - root.slack)
    readonly property real pointerTop: Math.max(0, root.boardRect.y - root.slack)
    readonly property real pointerRight: Math.min(root.width,
                                                  root.boardRect.x + root.boardRect.width + root.slack)
    readonly property real pointerBottom: Math.min(root.height,
                                                   root.boardRect.y + root.boardRect.height + root.slack)
    MouseArea {
        id: pointer
        x: root.pointerLeft
        y: root.pointerTop
        width: Math.max(0, root.pointerRight - root.pointerLeft)
        height: Math.max(0, root.pointerBottom - root.pointerTop)
        enabled: root.accepting
        preventStealing: true

        function local(mouseX, mouseY) {
            return Qt.point(mouseX - root.slack, mouseY - root.slack);
        }
        function over(mouseX, mouseY) {
            const p = local(mouseX, mouseY);
            const s = root.slack;
            return p.x >= -s && p.y >= -s && p.x <= root.boardRect.width + s && p.y <= root.boardRect.height + s;
        }

        onPressed: mouse => {
            root.keyboardUsed = false;
            root.forceActiveFocus();
            // The path work of a touch: the model's hit test, the path it
            // changes and every binding that follows it, until the frame.
            Profiler.begin("ui.trace");
            root.solo.pressAt(pointer.local(mouse.x, mouse.y).x, pointer.local(mouse.x, mouse.y).y);
            Profiler.end();
        }
        onPositionChanged: mouse => {
            const p = pointer.local(mouse.x, mouse.y);
            Profiler.begin("ui.trace");
            root.solo.moveTo(p.x, p.y);
            Profiler.end();
        }
        onReleased: mouse => {
            const p = pointer.local(mouse.x, mouse.y);
            Profiler.begin("ui.trace");
            root.solo.releaseAt(p.x, p.y, pointer.over(mouse.x, mouse.y));
            Profiler.end();
        }
        onCanceled: {
            Profiler.begin("ui.trace");
            root.solo.clearPath();
            Profiler.end();
        }
    }

    // --- the keyboard ---------------------------------------------------------------
    Keys.onPressed: event => {
        if (!root.accepting || root.columns === 0) {
            return;
        }
        root.keyboardUsed = true;
        const row = Math.floor(root.focusIndex / root.columns);
        const column = root.focusIndex % root.columns;
        Profiler.begin("ui.trace");
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
            root.solo.tapTile(root.focusIndex);
            event.accepted = true;
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.solo.done();
            event.accepted = true;
            break;
        case Qt.Key_Backspace:
            root.solo.removeLast();
            event.accepted = true;
            break;
        case Qt.Key_Escape:
            if (root.solo.path.length > 0) {
                root.solo.clearPath();
                event.accepted = true;
            }
            break;
        default:
            break;
        }
        Profiler.end();
    }
}
