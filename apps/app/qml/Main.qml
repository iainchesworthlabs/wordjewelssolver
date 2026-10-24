// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import WordJewelsSolver
import WordJewelsSolver.Parent
import WordJewelsSolver.Scan
import WordJewelsSolver.Scene

// The window: a thin shell that shows the screen Navigation names. Add a screen as
// one `case` and one `Component` line, and flip its availability on Navigation in
// the same change. Back goes where the person came from (Navigation), Esc and the
// system back do what the visible back control does, and on home they do nothing.
//
// The parent area (gate, dashboard, settings, about) is one screen of it, reached
// from home and from the box by the small button in the corner. It is the parent
// area's own gate that stands in front of it; nothing here opens it.
ApplicationWindow {
    id: window

    // The parent area's controller, made by main.cpp over the program's one store,
    // which the game also works on (so a change made in the settings is what the
    // next game is made from).
    required property var parentController
    // The interface's language, made by main.cpp and applied before this window
    // is parsed: its catalogue, its layout direction and its retranslations all
    // go through it (apps/app/src/language_manager.hpp).
    required property var languageManager
    // The feedback engines, made by main.cpp: the sound, the platform's voice
    // and the haptics, behind one cue() (feedback/feedback_engine.hpp), and the
    // device's motion, which only Solo Play reads (feedback/motion_source.hpp).
    required property var feedback
    required property var motion

    width: 1160
    height: 740
    minimumWidth: 900
    minimumHeight: 620
    visible: true
    title: qsTr("Word Jewels Solver")
    color: Theme.page

    // The Solo Play game lives for the whole run, so a board in the box survives a
    // trip to home and back.
    SoloModel {
        id: soloGame
        onCue: (kind, detail) => Feedback.cue(kind, detail)
    }

    // Mode A, the Family Companion. The check screen's draft and the camera's
    // little lifecycle are ScanModel's; the confirmed board and the tray are the
    // assistant's. Both live for the run, so the draft survives a trip to the
    // parent area or home; the assistant's round is recorded when its screen is
    // left (assist.leave()).
    //
    // The camera is made here and handed to the model, which starts it only while
    // scan.ready is shown and stops it as soon as a frame is in hand. A kit
    // without Qt Multimedia has the same object with a backend that answers
    // "no_camera", so the screen is unchanged and the typed board is the way on.
    CameraSource {
        id: scanCamera
    }
    ScanModel {
        id: scanGame
        cameraSource: scanCamera
    }
    AssistModel {
        id: assistGame
        onCue: (kind, detail) => Feedback.cue(kind, detail)
    }

    // ---- The smoke run (--smoke): the workload a capture attaches to ------------
    // main.cpp sets this before the window is completed; the frames are counted
    // by FrameProfiler in C++, which prints the report and quits. This gives the
    // counter work: Solo Play is shown, the box is shaken whenever it is ready,
    // and a board that has settled is put away again, so a capture sees the
    // scene's whole cycle. The seed is fixed for a smoke run (main.cpp), so the
    // boards are the same on every run.
    property bool smoke: false

    function startSmoke() {
        Navigation.go("solo");
        smokeTimer.running = true;
    }

    Component.onCompleted: {
        // main.cpp applied this before the engine loaded; doing it again is the
        // same language and changes nothing. Kept here so the window is right
        // even when its owner did not (the stub shells).
        languageManager.apply(parentController.language);
        // The one seam every cue goes through (kit/Feedback.qml), and the
        // parent's settings that decide what a cue plays and says.
        Feedback.engine = feedback;
        feedback.settings = parentController.settings;
        // Centre the first window on the screen it opens on. Use the window's
        // own screen, not Screen.desktopAvailableWidth: that is the union of
        // every monitor, so on a multi-monitor desk it can place the window in
        // the gap between two screens (or off every one of them). The parent's
        // window geometry is not persisted yet; the default is a comfortable
        // desktop size rather than a phone-sized minimum.
        const screenX = typeof Screen.virtualX === "number" ? Screen.virtualX : 0;
        const screenY = typeof Screen.virtualY === "number" ? Screen.virtualY : 0;
        const screenWidth = Screen.width > 0 ? Screen.width : width;
        const screenHeight = Screen.height > 0 ? Screen.height : height;
        if (width > screenWidth - 32) {
            width = Math.max(minimumWidth, screenWidth - 32);
        }
        if (height > screenHeight - 32) {
            height = Math.max(minimumHeight, screenHeight - 32);
        }
        x = screenX + Math.max(0, Math.round((screenWidth - width) / 2));
        y = screenY + Math.max(0, Math.round((screenHeight - height) / 2));
        if (smoke) {
            startSmoke();
        }
    }

    Timer {
        id: smokeTimer
        interval: 250
        repeat: true
        running: false
        onTriggered: {
            if (soloGame.state === "box") {
                soloGame.shake();
            } else if (soloGame.state === "playing") {
                soloGame.lidOn();
            }
        }
    }

    // What the parent chooses for the whole program: the Motion setting and the Text
    // size. Reduced forces reduced motion on; System follows what the operating
    // system asks for (SystemMotion), and the system's text size is not read.
    Binding {
        target: Theme
        property: "reducedMotion"
        value: window.parentController.reducedMotion
               || (window.parentController.motion === "system" && SystemMotion.reduced)
    }
    Binding {
        target: Theme
        property: "textScale"
        value: window.parentController.textScale
    }

    // The parent's Colour scheme setting: System follows the operating system,
    // Light and Dark are the person's choice for every screen's chrome.
    Binding {
        target: Theme
        property: "colourScheme"
        value: window.parentController.colourScheme
    }

    // The parent's clock for solo boards: off, a count-up, or a count-down of
    // the minutes chosen. Solo Play reads it from the model the board is made
    // from; a board in play keeps the clock it began with.
    Binding {
        target: soloGame
        property: "clockMode"
        value: window.parentController.soloClock
    }
    Binding {
        target: soloGame
        property: "clockMinutes"
        value: window.parentController.soloClockMinutes
    }

    // The parent's help level for Solo Play: off, the start tile alone, or the
    // start tile and its first two letters. Applied at once, as the parent's
    // other switches are.
    Binding {
        target: soloGame
        property: "helpLevel"
        value: window.parentController.helpLevel
    }

    // The interface's direction: the screens' positioners, icons and the split
    // read this; the window's anchors follow the same choice through
    // LayoutMirroring on the shell.
    Binding {
        target: Theme
        property: "rightToLeft"
        value: window.languageManager.rightToLeft
    }

    // A setting the parent changed, or a reset, is read by the next game.
    Connections {
        target: window.parentController
        function onStoreEdited() {
            soloGame.storeEdited();
        }
        // The Language setting is applied at once: the catalogue is installed and
        // this window's direction and every qsTr binding follow. The board and its
        // letters do not move: they are the same English board in every language.
        // The sound, speech and feedback settings are read by the next cue.
        function onSettingsChanged() {
            window.feedback.settings = window.parentController.settings;
            window.languageManager.apply(window.parentController.language);
        }
    }

    // Esc, and the system's back key, go back. A screen that has a deeper state
    // (a path being made, a sheet) takes the key first and lets it through when it
    // has nothing to undo.
    FocusScope {
        id: shell
        anchors.fill: parent
        focus: true

        // Hebrew and Yiddish read right to left: the window's layout follows, and
        // its children inherit the mirroring (anchors flip, RTL layouts order their
        // children from the right). The board containers switch it off again where
        // a mirrored anchor would move a tile (qml/solo/FlatBoard.qml).
        LayoutMirroring.enabled: window.languageManager.rightToLeft
        LayoutMirroring.childrenInherit: true

        // Solo Play's screen is made once, at start-up, and kept for the run: its
        // board scene is the 3D one, whose View3D is created once (docs/design/
        // shake-prototype.md, "What was built"), behind home and not on the
        // Shake press. Hidden, it is not drawn; its dice and script stay.
        SoloScreen {
            id: soloScreen

            anchors.fill: parent
            solo: soloGame
            // The device's motion (Qt Sensors on a tablet; unavailable on these
            // desktops, where the drag stands in). It is started only while the
            // box or the falling dice need it and the parent has Shake on.
            motionSource: window.motion
            // The parent's switch for shaking the device: it gates the drag that
            // stands in for the accelerometer, never the Shake button.
            shakeEnabled: window.parentController.settings.shake === true
            opacity: Navigation.screen === "solo" ? 1 : 0
            visible: opacity > 0
            enabled: Navigation.screen === "solo"
            onHomeRequested: Navigation.home()
            onParentRequested: Navigation.go("parent")

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.screen
                    easing.type: Easing.OutCubic
                }
            }
        }

        // The screen on show, untyped: a screen with a deeper state has a back().
        readonly property var current: Navigation.screen === "solo" ? soloScreen : screens.item

        function goBack() {
            if (current && typeof current.back === "function") {
                current.back();
            } else {
                Navigation.back();
            }
        }

        Keys.onEscapePressed: event => {
            if (Navigation.screen !== "home") {
                goBack();
                event.accepted = true;
            }
        }
        Keys.onBackPressed: event => {
            if (Navigation.screen !== "home") {
                goBack();
                event.accepted = true;
            }
        }

        Loader {
            id: screens
            anchors.fill: parent
            focus: true
            asynchronous: true
            visible: Navigation.screen !== "solo"
            // Each screen fades in rather than replacing the last in one frame, and
            // takes the keyboard (Space on the box, the arrow keys on the board).
            onLoaded: {
                Profiler.begin("ui.screen");
                if (Navigation.screen !== "solo") {
                    screenFade.restart();
                    (item as Item).forceActiveFocus();
                }
                Profiler.end();
            }
            sourceComponent: {
                switch (Navigation.screen) {
                case "solo":
                    return null;
                case "scan":
                    return scanScreen;
                case "assist":
                    return assistScreen;
                case "parent":
                    return parentScreen;
                case "jewels":
                    return jewelsScreen;
                default:
                    return homeScreen;
                }
            }
        }

        // The boards played, over whichever screen asked (Home's "My boards"). It
        // fills the window, so its scrim covers the app bar too.
        HistorySheet {
            id: boardsSheet
            anchors.fill: parent
            solo: soloGame
        }

        // Solo Play takes the keyboard when it is shown (its own screen keeps the
        // focus while it is not).
        Connections {
            target: Navigation

            function onScreenChanged() {
                if (Navigation.screen === "solo") {
                    soloScreen.forceActiveFocus();
                }
            }
        }

        NumberAnimation {
            id: screenFade
            target: screens
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.screen
            easing.type: Easing.OutCubic
        }
    }

    Component {
        id: homeScreen
        Home {
            controller: window.parentController
            onSoloRequested: Navigation.go("solo")
            onParentRequested: Navigation.go("parent")
            onCollectionRequested: Navigation.go("jewels")
            // The boards played: a sheet over Home itself, so the child is not
            // taken into the box to look at a list. Back, Escape and Close
            // return to Home.
            onBoardsRequested: boardsSheet.open = true
        }
    }
    Component {
        id: jewelsScreen
        JewelsScreen {
            controller: window.parentController
            onClosed: Navigation.back()
            onPlayRequested: Navigation.go("solo")
        }
    }
    Component {
        id: scanScreen
        // Mode A: the camera's ready screen and the check screen are one screen
        // (ScanScreen); the check screen's "Yes, play" hands the confirmed board
        // to the assistant and shows it.
        ScanScreen {
            scan: scanGame
            assist: assistGame
            rightToLeft: window.languageManager.rightToLeft
            // Back from scan.ready goes home; from the check screen it is "Read
            // again", which the screen handles itself.
            onClosed: Navigation.back()
            onPlayRequested: Navigation.go("assist")
        }
    }
    Component {
        id: assistScreen
        AssistScreen {
            assist: assistGame
            rightToLeft: window.languageManager.rightToLeft
            // The check screen is where the person came from, with the board it
            // confirmed still in the draft; the assistant's board is recorded.
            onClosed: Navigation.back()
            // A new round is a new scan, but it is still entered from the board
            // the child was on: Back returns to that board rather than jumping
            // out to the parent area. The board that was left is already saved.
            onNewBoardRequested: {
                scanGame.readAgain();
                Navigation.go("scan");
            }
        }
    }
    Component {
        id: parentScreen
        // Back goes where the person came from, home or the box.
        ParentArea {
            controller: window.parentController
            rightToLeft: window.languageManager.rightToLeft
            onClosed: Navigation.back()
            // The scanner is an adult workflow behind the gate. The parent
            // area keeps the gate open while the scanner is shown, so Back
            // returns here rather than to the gate.
            onScanRequested: Navigation.go("scan")
        }
    }
}
