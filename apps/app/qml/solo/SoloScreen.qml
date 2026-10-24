// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver
import WordJewelsSolver.Scene

// Solo Play, Mode B. The closed jewel box and the flat board are 2D; the 3D
// scene is used only for the shake and the drop. A persistent app bar holds the
// way back and the grown-up entry, so no control lies over the board. The
// panel is one full-height column: the words, the hint, the board's clock and
// the Clear and Done actions all live in it, and nothing of the board moves
// when a path starts. The end of a board - cleared, finished or timed out - is
// one Sheet over the whole window; there is no separate resting screen.
FocusScope {
    id: root

    required property var solo
    property bool shakeEnabled: true
    property var motionSource: null

    signal homeRequested
    signal parentRequested

    readonly property string soloState: solo.state
    readonly property bool boxShown: soloState === "box"
    readonly property bool shaking: soloState === "shaking" || soloState === "dropping"
    // A board that was cleared and put away ("resting", which a game brought back
    // from the store can open on) is shown as the cleared board is: settled, under
    // its summary.
    readonly property bool boardShown: soloState === "playing" || soloState === "word-found"
                                       || soloState === "cleared" || soloState === "resting"
    // A board can be ended by the child before every dictionary word is found.
    // The summary is the same card, worded for a finish rather than a clear.
    property bool finishSummary: false
    // The round ended because the count-down reached zero (not the child's
    // Finish board): the summary says so.
    property bool finishedByClock: false
    readonly property bool summaryShown: soloState === "cleared" || soloState === "resting" || root.finishSummary
    readonly property bool settling: settlingNow
    property bool settlingNow: false
    readonly property bool motionLive: motionSource !== null && motionSource.available
    readonly property bool motionNeeded: motionSource !== null && root.enabled && root.visible
                                        && root.shakeEnabled
                                        && (root.boxShown || root.shaking)
    readonly property bool mirrored: Theme.rightToLeft

    LayoutMirroring.enabled: root.mirrored
    LayoutMirroring.childrenInherit: true

    function syncMotion() {
        if (motionSource === null) {
            return;
        }
        motionSource.shakeEnabled = root.shakeEnabled;
        motionSource.active = root.motionNeeded;
    }
    onMotionSourceChanged: syncMotion()
    onMotionNeededChanged: syncMotion()
    onShakeEnabledChanged: syncMotion()

    function boardForScene() {
        return {
            rows: root.solo.rows,
            columns: root.solo.columns,
            tiles: root.solo.tiles,
            faces: root.solo.faces,
            up: root.solo.up
        };
    }
    function closedBox() {
        return { rows: root.solo.boardSize, columns: root.solo.boardSize, tiles: [] };
    }

    // The board layout: the board in the leading two thirds and the panel
    // column in the trailing third of the whole stage, to the window's bottom
    // edge. The panel keeps a place for its own actions, so the board and the
    // panel never move when a path starts.
    SoloLayout {
        id: rule
        width: root.width
        height: root.height - Theme.appBarHeight
        boardSize: root.solo.boardSize
        rightToLeft: root.mirrored
    }

    // The stage already sits below the app bar, so the layout's rectangles are
    // used as they are.
    readonly property rect boardArea: Qt.rect(rule.boardArea.x, rule.boardArea.y,
                                              rule.boardArea.width, rule.boardArea.height)
    readonly property rect panelArea: Qt.rect(rule.panelArea.x, rule.panelArea.y,
                                              rule.panelArea.width, rule.panelArea.height)

    // How far the tray stands off the tiles, in the flat board and in the 3D scene
    // alike: the token's outset, held inside the board area's own inset with a
    // little air (4 dp) before the area's edge, so the rim is never clipped or
    // flush against the window.
    readonly property real trayInset: Theme.trayOutset(rule.tile, rule.inset - Theme.space1)

    function relayout() {
        if (solo.rows > 0) {
            root.solo.setGeometry(rule.tile, rule.gap);
        }
    }
    Component.onCompleted: {
        root.relayout();
        syncMotion();
        // The scene is made lazily (the sceneLoader below) and is given the
        // closed box when it is ready: the 2D box is what the child sees.
    }
    onWidthChanged: resizeTimer.restart()
    onHeightChanged: resizeTimer.restart()
    Timer {
        id: resizeTimer
        interval: 50
        onTriggered: root.relayout()
    }

    property var pendingBoard: null
    property int pendingSeed: 0
    property string feedbackText: ""
    property string feedbackDetail: ""
    property bool feedbackWord: false
    // The scene, typed for the rest of the screen; null while the loader is
    // still making it. Once made it is kept for the run (sceneMade).
    readonly property BoardScene scene: sceneLoader.item
    property bool sceneMade: false

    function startPendingPlay() {
        if (pendingBoard === null || scene === null) {
            // The board stays waiting: either nothing shook yet, or the scene
            // is still being made and its onLoaded takes over.
            return;
        }
        const board = pendingBoard;
        pendingBoard = null;
        if (root.soloState === "shaking") {
            scene.play(board, pendingSeed);
        }
    }

    Timer {
        id: playStart
        interval: 16
        onTriggered: root.startPendingPlay()
    }

    Timer {
        id: feedbackTimer
        interval: 1800
        onTriggered: {
            root.feedbackText = "";
            root.feedbackDetail = "";
        }
    }

    // The 3D dice and the flat dice are one object seen twice: when the scene
    // says it has settled, let the 3D board fade out under the flat one instead
    // of cutting between two pictures.
    Timer {
        id: settleFade
        interval: Theme.screen
        onTriggered: root.settlingNow = false
    }

    function currentWord() {
        var letters = [];
        for (var i = 0; i < solo.path.length; ++i) {
            const tile = solo.tiles[solo.path[i]];
            letters.push(tile === "QU" ? "Qu" : tile);
        }
        return letters.join(" · ");
    }

    function finishBoard(byClock) {
        if (root.soloState !== "playing" && root.soloState !== "word-found") {
            return;
        }
        root.finishedByClock = byClock === true;
        root.finishSummary = true;
    }

    function leaveFinishSummary() {
        root.finishSummary = false;
        root.finishedByClock = false;
        if (root.soloState === "word-found") {
            root.solo.wordShown();
        }
        root.solo.lidOn();
    }

    // The summary's Close, and Escape: the board is over and the lid goes back on,
    // as Play again does without the shake.
    function dismissSummary() {
        if (root.finishSummary) {
            root.leaveFinishSummary();
        } else {
            root.solo.celebrated();
            root.solo.again();
        }
    }

    function back() {
        if (root.finishSummary) {
            root.leaveFinishSummary();
            root.homeRequested();
            return;
        }
        switch (solo.state) {
        case "box":
            root.homeRequested();
            break;
        case "resting":
            root.solo.again();
            root.homeRequested();
            break;
        case "cleared":
            root.solo.celebrated();
            root.solo.again();
            root.homeRequested();
            break;
        default:
            // Playing, a word being shown, the shake or the drop: the round is
            // left in progress. Home, and the next visit, finds it as it was -
            // the session is saved on every find, so nothing is lost.
            root.homeRequested();
            break;
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space && root.boxShown) {
            root.solo.shake();
            event.accepted = true;
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.page
    }

    AppBar {
        id: appBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        showBack: true
        showWordmark: false
        showParent: true
        title: root.boxShown ? qsTr("Solo Play")
               : root.finishSummary ? qsTr("Board finished")
               : (root.soloState === "cleared" || root.soloState === "resting"
                  ? qsTr("All words found") : qsTr("Find a word"))
        onBack: root.back()
        onParentRequested: root.parentRequested()
    }

    // --- the stage ------------------------------------------------------------------
    Item {
        id: stage
        anchors.top: appBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        Rectangle {
            x: root.boardArea.x
            y: root.boardArea.y
            width: root.boardArea.width
            height: root.boardArea.height
            color: Theme.boardArea
        }

        // The closed box, before the shake. Its compartments change with the
        // board size, so the picture on the left answers the choice on the
        // right. The caption is aligned to the box, not to the whole window.
        JewelBox {
            id: boxArt
            readonly property real boxSide: Math.min(root.boardArea.width * 0.66,
                                                     root.boardArea.height * 0.64, 560)
            x: root.boardArea.x + (root.boardArea.width - width) / 2
            y: root.boardArea.y + (root.boardArea.height - height) / 2 - 28
            width: boxSide
            height: boxSide * 0.80
            closed: true
            showDice: false
            boardSize: root.solo.boardSize
            visible: root.boxShown
            opacity: root.boxShown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.base } }

            DragHandler {
                id: boxDrag
                target: null
                enabled: root.boxShown && !root.motionLive
                property real startX: 0
                onActiveChanged: {
                    if (active) {
                        startX = centroid.position.x;
                    }
                }
                onCentroidChanged: {
                    if (active && Math.abs(centroid.position.x - startX) > 42) {
                        startX = centroid.position.x;
                        root.solo.shake();
                    }
                }
            }
        }

        Label {
            id: boxCaption
            visible: root.boxShown
            width: Math.min(boxArt.width * 1.2, root.boardArea.width)
            x: boxArt.x + (boxArt.width - width) / 2
            y: boxArt.y + boxArt.height + Theme.space4
            horizontalAlignment: Text.AlignHCenter
            role: "word"
            text: SoloWording.shakeToOpen
            color: Theme.inkSecondary
            wrapMode: Text.WordWrap
        }

        // The 3D theatre: the shake and the drop only. It is made by a Loader,
        // asynchronously and only once Solo Play is entered (the closed box),
        // so its first creation - the scene graph, the light probe and the
        // materials, whose shaders are made and cached on first use - happens
        // while the child reads the box instead of blocking the first shake.
        // It then lives for the rest of the run, so later shakes pay nothing.
        Loader {
            id: sceneLoader
            objectName: "sceneLoader"
            x: root.boardArea.x
            y: root.boardArea.y
            width: root.boardArea.width
            height: root.boardArea.height
            // The full board rectangle is what aligns the settle; clipping keeps
            // the scene's overdraw out of the app bar above and the panel beside.
            clip: true
            asynchronous: true
            // The scene is made once Solo Play is shown - the closed box, not
            // the first shake - and kept for the rest of the run, so later
            // shakes pay nothing. It is not gated on the derived state
            // booleans: their re-evaluation after a state change is not
            // atomic, and the loader once saw a false in between, destroying
            // the scene mid-shake (measured in the box suites).
            active: root.sceneMade || (root.enabled && root.visible)
            visible: root.shaking || root.settling
            opacity: root.shaking ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.base } }

            sourceComponent: boardScene

            onLoaded: {
                root.sceneMade = true;
                if (root.shaking) {
                    // The shake began while the scene was still being made:
                    // hand it the board that has been waiting for it.
                    if (root.pendingBoard !== null) {
                        root.scene.prepare(root.pendingBoard);
                        root.startPendingPlay();
                    }
                } else if (root.boxShown) {
                    // The closed box: the scene keeps its seam and its own
                    // state, so the shake can replace the box without
                    // re-creating anything.
                    root.scene.show(root.closedBox());
                }
            }
        }
        Component {
            id: boardScene
            BoardScene {
                // Exactly the flat board's rectangle, with no inset: the settled
                // 3D dice and the flat dice have to occupy the same pixels for
                // the cross-fade to read as one object changing surface.
                reducedMotion: Theme.reducedMotion
                LayoutMirroring.enabled: false
                // The flat board's own colours, so the cross-fade between the
                // two does not change colour.
                trayColor: Theme.tray
                rimColor: Theme.trayEdge
                dieColor: Theme.die
                dieEdgeColor: Theme.dieEdge
                dieInkColor: Theme.dieInk
                jewelColor: Theme.jewelDead
                lidColor: Theme.lid
                trayInset: root.trayInset

                onSettled: root.solo.sceneSettled()
                onLanded: (index, pitch) => Feedback.cue("tile-landed", String(pitch))
                onRattled: Feedback.cue("rattle", "")
            }
        }

        // The flat board the child reads and traces.
        FlatBoard {
            id: flatBoard
            x: root.boardArea.x
            y: root.boardArea.y
            width: root.boardArea.width
            height: root.boardArea.height
            solo: root.solo
            boardRect: rule.boardRect
            tileSize: rule.tile
            tileGap: rule.gap
            trayInset: root.trayInset
            visible: root.boardShown || root.settling
            opacity: root.boardShown ? 1 : 0
            scale: root.boardShown ? 1 : 0.992
            transformOrigin: Item.Center
            Behavior on opacity { NumberAnimation { duration: Theme.screen; easing.type: Easing.OutCubic } }
            Behavior on scale { NumberAnimation { duration: Theme.turn; easing.type: Easing.OutCubic } }
            // While the finish summary covers the board, the board neither
            // takes the keyboard nor speaks to a screen reader: the summary's
            // own focus stays in its card, where the way on is.
            focus: root.boardShown && !root.summaryShown
            activeFocusOnTab: !root.summaryShown
            Accessible.ignored: root.summaryShown
            onVisibleChanged: if (root.boardShown && !root.summaryShown) forceActiveFocus()
        }

        // --- the panel ------------------------------------------------------------
        // One column from the app bar to the window's bottom edge. Its own
        // actions (Clear, Done) and the board's clock live at its foot, so the
        // bottom right of the window is the actions' home and the column fills
        // the full height.
        Item {
            id: panelSlot
            x: root.panelArea.x
            y: root.panelArea.y
            width: root.panelArea.width
            height: root.panelArea.height
            clip: true

            SoloBox {
                id: boxControls
                width: parent.width
                height: parent.height
                solo: root.solo
                layoutRule: rule
                wide: rule.wide
                // The panel stays while the box shakes (quiet, its Shake button
                // saying so) instead of fading to blank paper.
                busy: root.shaking
                opacity: root.boxShown || root.shaking ? 1 : 0
                visible: opacity > 0
                enabled: root.boxShown
                onShakeRequested: root.solo.shake()
                Behavior on opacity { NumberAnimation { duration: Theme.turn } }
            }

            PlayingPanel {
                id: panel
                solo: root.solo
                wide: rule.wide
                width: parent.width
                height: parent.height
                statusText: root.feedbackText
                statusDetail: root.feedbackDetail
                statusWord: root.feedbackWord
                // Under the summary nothing of the panel takes a key or a touch.
                enabled: !root.summaryShown
                property real shown: root.boardShown ? 1 : 0
                Behavior on shown { NumberAnimation { duration: Theme.turn; easing.type: Easing.OutCubic } }
                opacity: shown > 0 ? 1 : 0
                visible: shown > 0
                x: rule.wide && !Theme.reducedMotion ? (1 - shown) * width : 0
                y: !rule.wide && !Theme.reducedMotion ? (1 - shown) * height : 0
                onFinishRequested: root.finishBoard()
            }
        }
    }

    // --- the end of a board -----------------------------------------------------
    // The board is over, one way or the other: the words are safe, and the sheet
    // says what was earned and offers the way on. It is the one modal of the
    // application (kit/Sheet): it dims the whole window, the bar too, and takes
    // the focus; Play again leads, Home is as big beside it, and Close puts the
    // lid back on. The words scroll in their own card, so the buttons never move.
    Sheet {
        id: summary
        anchors.fill: parent
        open: root.summaryShown
        title: root.finishSummary
               ? SoloWording.finishTitle
               : root.solo.found.length === 1
                 ? qsTr("You found the word!")
                 : qsTr("You found all %1 words!").arg(root.solo.found.length)
        maxWidth: Theme.modalMax
        maxHeight: Theme.modalMax
        dismissOnScrim: false
        onClose: root.dismissSummary()

        // The reward is spoken as the sheet opens, and Play again takes the focus.
        onOpenChanged: {
            if (summary.open) {
                Feedback.cue("speak", summary.title + ". "
                                      + SoloWording.summaryStars(root.solo.boardStars)
                                      + " and " + SoloWording.summaryPoints(root.solo.boardPoints));
                Qt.callLater(() => playAgain.forceActiveFocus());
            }
        }

        // The reward, front and centre: the stars (which feed the collection) and
        // the points, large enough to read at arm's length.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2

            Jewel {
                Layout.preferredWidth: Theme.jewelMd
                Layout.preferredHeight: Theme.jewelMd
                tier: "power"
                color: Theme.gold
                Accessible.ignored: true
            }
            Label {
                role: "child"
                color: Theme.goldInk
                text: SoloWording.summaryStars(root.solo.boardStars)
            }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
            }
            Label {
                role: "child"
                font.family: Theme.monoFont
                text: SoloWording.summaryPoints(root.solo.boardPoints)
            }
        }

        Label {
            Layout.fillWidth: true
            role: "body"
            learning: true
            color: Theme.inkSecondary
            text: root.finishSummary ? SoloWording.finishBody : qsTr("Look at the jewels you collected.")
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: root.solo.clockMode === "up"
            role: "caption"
            text: SoloWording.playedFor(root.solo.clockText)
        }

        Label {
            Layout.fillWidth: true
            visible: root.finishedByClock
            role: "body"
            learning: true
            color: Theme.inkSecondary
            text: SoloWording.timeRanOut
            wrapMode: Text.WordWrap
        }

        // The words, one jewel each, in their own scroll: the sheet stays one
        // height however many were found, and the buttons below stay put.
        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: SoloWording.yourWordsCount(root.solo.found.length)
            empty: root.solo.found.length === 0
            emptyTitle: SoloWording.summaryNoWords

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: Theme.rowHeight
                Layout.preferredHeight: Math.min(summaryWords.contentHeight, 4 * Theme.rowHeight)

                ListView {
                    id: summaryWords
                    anchors.fill: parent
                    anchors.rightMargin: Theme.space3
                    clip: true
                    model: root.solo.found
                    boundsBehavior: Flickable.StopAtBounds

                    Accessible.role: Accessible.List
                    Accessible.name: SoloWording.yourWordsCount(root.solo.found.length)

                    delegate: Item {
                        id: summaryRow
                        required property int index
                        required property var modelData
                        width: ListView.view.width
                        height: Theme.rowHeight

                        Accessible.role: Accessible.ListItem
                        Accessible.name: SoloWording.found(summaryRow.modelData.word)

                        Jewel {
                            id: summaryJewel
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.jewelMd
                            height: Theme.jewelMd
                            tier: summaryRow.modelData.tier
                            colorIndex: summaryRow.index
                            Accessible.ignored: true
                        }
                        Label {
                            anchors.left: summaryJewel.right
                            anchors.leftMargin: Theme.space3
                            anchors.right: summaryPoints.left
                            anchors.rightMargin: Theme.space2
                            anchors.verticalCenter: parent.verticalCenter
                            role: "word"
                            decorative: true
                            text: summaryRow.modelData.word
                            elide: Text.ElideRight
                        }
                        Label {
                            id: summaryPoints
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            role: "mono"
                            decorative: true
                            font.capitalization: Font.MixedCase
                            color: Theme.goldInk
                            text: qsTr("+%1").arg(summaryRow.modelData.points)
                        }
                    }
                }

                ScrollCue {
                    view: summaryWords
                    anchors.top: summaryWords.top
                    anchors.bottom: summaryWords.bottom
                    anchors.right: parent.right
                }
            }
        }

        // The way on: Play again leads, Home is as big beside it. The two are
        // the same height and share the row equally; the colour says which leads.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3

            AppButton {
                id: playAgain
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                size: "primary"
                variant: "primary"
                wrap: true
                label: SoloWording.playAgain
                iconName: "again"
                onClicked: {
                    if (root.finishSummary) {
                        root.leaveFinishSummary();
                        root.solo.shake();
                    } else {
                        root.solo.celebrated();
                        root.solo.again();
                    }
                }
            }
            AppButton {
                id: homeButton
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                size: "primary"
                variant: "secondary"
                wrap: true
                label: SoloWording.home
                onClicked: {
                    root.dismissSummary();
                    root.homeRequested();
                }
            }
        }
    }

    // The board's clock ticks only while this screen is the one on show and a
    // round is in play. The model owns the seconds; the screen owns the "now".
    Binding {
        target: root.solo
        property: "clockActive"
        value: root.enabled && root.visible
               && (root.soloState === "playing" || root.soloState === "word-found")
               && !root.finishSummary
    }

    // --- the model's moments ---------------------------------------------------------
    Connections {
        target: root.solo

        function onStateChanged() {
            switch (root.solo.state) {
            case "shaking": {
                settleFade.stop();
                root.settlingNow = false;
                const board = root.boardForScene();
                if (root.scene !== null) {
                    root.scene.prepare(board);
                }
                root.pendingBoard = board;
                root.pendingSeed = root.solo.seed;
                playStart.restart();
                root.feedbackText = "";
                root.feedbackDetail = "";
                break;
            }
            case "box":
                playStart.stop();
                settleFade.stop();
                root.settlingNow = false;
                root.pendingBoard = null;
                root.finishSummary = false;
                root.finishedByClock = false;
                root.feedbackText = "";
                root.feedbackDetail = "";
                break;
            case "playing":
                // The scene has settled. Keep it for one cross-fade, under the
                // flat board that the child will trace.
                root.relayout();
                root.settlingNow = true;
                settleFade.restart();
                break;
            case "cleared":
                settleFade.stop();
                root.settlingNow = false;
                root.feedbackText = "";
                root.feedbackDetail = "";
                break;
            }
        }

        function onClockExpired() {
            // A count-down board's time ran out: the round ends as Finish
            // board ends it, with everything found kept, and the summary
            // says the time ran out.
            root.finishBoard(true);
        }

        function onTraced(kind, path, word, tier, stars, again, newBest, cleared) {
            if (kind === "word") {
                root.feedbackWord = true;
                root.feedbackText = word.toUpperCase() + "!";
                root.feedbackDetail = again ? qsTr("You found this one already.")
                                            : qsTr("You found a word. +%1 points").arg(
                                                  root.solo.found.length > 0
                                                  ? root.solo.found[root.solo.found.length - 1].points : 0);
            } else if (kind === "tick") {
                root.feedbackWord = false;
                root.feedbackText = qsTr("That's a real word.");
                root.feedbackDetail = qsTr("It is not on this board.");
            } else {
                root.feedbackWord = false;
                root.feedbackText = qsTr("Not this time.");
                root.feedbackDetail = qsTr("Try another word.");
            }
            feedbackTimer.restart();
        }
    }

    Connections {
        target: root.motionSource
        function onSample(ax, ay, az, wx, wy, wz) {
            if (root.motionLive && (root.motionNeeded || root.shaking)
                    && root.scene !== null) {
                root.scene.pushMotion(ax, ay, az, wx, wy, wz);
            }
        }
        function onShakeDetected() {
            if (root.shakeEnabled && root.boxShown) {
                root.solo.shake();
            }
        }
    }
}
