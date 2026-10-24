// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import WordJewelsSolver
import WordJewelsSolver.Scan

// A stub for the application's screen shell for the Family Assistant's screens:
// the scan screen and the assistant's screen, with the two models the program
// makes, and no 3D scene and no other screen. It is test support, not part of the
// application: the real shell shows ScanScreen and AssistScreen as two screens of
// its navigation (Main.qml).
//
// The Loader replaces one screen with the other, as the real shell does, so the
// scan screen's destruction stops the camera and the assistant's records its
// board, and the check screen keeps its draft (the model's).
Window {
    id: host

    required property var scan
    required property var assist
    // The interface's language: the real shell makes it required too, and the
    // mirroring below is bound to it from the first binding evaluation.
    required property var languageManager

    // "scan" or "assist", as Main.qml's Navigation would say.
    property string page: "scan"

    // What the language check reads: the window's mirroring reaches the screen,
    // the bar's way back is on the leading edge, and the panel is on the trailing
    // side of the split.
    readonly property bool mirrored: loader.item && loader.item.mirrored !== undefined
                                      ? loader.item.mirrored
                                      : host.languageManager.rightToLeft
    readonly property real backButtonX: loader.item && loader.item.backButtonX !== undefined
                                        ? loader.item.backButtonX : -1
    readonly property real checkYesPlayX: loader.item && loader.item.checkYesPlayX !== undefined
                                          ? loader.item.checkYesPlayX : -1
    readonly property real panelX: loader.item && loader.item.panelX !== undefined
                                   ? loader.item.panelX : -1
    // Whether the scan screen is showing the camera's live view. The no-camera
    // check expects false; the camera check hands the model a fake source and
    // expects true.
    readonly property bool previewShown: loader.item && loader.item.previewShown !== undefined
                                          ? loader.item.previewShown : false

    width: 1100
    height: 700
    visible: true
    title: "Family game (stub shell)"
    color: Theme.page

    // The real shell does this in Main.qml: the kit's direction is the language's.
    Binding {
        target: Theme
        property: "rightToLeft"
        value: host.languageManager.rightToLeft
    }

    // The two settings the capture mode (--shots) varies: the colour scheme and
    // the text size, as the parent's settings would set them.
    property string scheme: "light"
    property real textScale: 1.0
    Binding {
        target: Theme
        property: "colourScheme"
        value: host.scheme
    }
    Binding {
        target: Theme
        property: "textScale"
        value: host.textScale
    }

    // The check screen's "Yes, play", from a test: the confirmation, the board
    // handed to the assistant, and the screen shown.
    function play() {
        if (!host.scan.confirm()) {
            return false;
        }
        if (!host.assist.begin(host.scan.rows, host.scan.columns, host.scan.letters)) {
            return false;
        }
        host.page = "assist";
        return true;
    }

    // "New board" from a test, as the assist screen's button asks for: the scan
    // screen is ready and the assistant's board is recorded.
    function newBoard() {
        host.assist.leave();
        host.scan.readAgain();
        host.page = "scan";
    }

    Loader {
        id: loader
        anchors.fill: parent
        sourceComponent: host.page === "assist" ? assistScreen : scanScreen
    }

    Component {
        id: scanScreen
        ScanScreen {
            scan: host.scan
            assist: host.assist
            onPlayRequested: host.page = "assist"
            onClosed: host.page = "scan"
            rightToLeft: host.languageManager.rightToLeft
        }
    }

    Component {
        id: assistScreen
        AssistScreen {
            assist: host.assist
            onClosed: host.page = "scan"
            onNewBoardRequested: host.newBoard()
            rightToLeft: host.languageManager.rightToLeft
        }
    }
}
