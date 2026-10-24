// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// Mode A, the Family Companion's first screen (docs/design/game.md, "The MVP:
// one capture"): `scan.ready`, `scan.reading` and `scan.check` are one screen,
// because the camera, the frame and the check belong to one act. The three steps
// are shown throughout: Place, Check, Play (ScanStepper.qml).
//
//   scan.ready    the frame to put the board in, Capture (96 dp), and Type in
//                 your board. The camera is on only here: the screen starts it
//                 when it opens and stops it when it leaves (the screens' and the
//                 model's rule; docs/design/game.md, "Mode A"). Where there is a
//                 camera, its live picture is shown in the frame; where there is
//                 none, the frame says so with a picture and the words under it.
//   scan.reading  one frame is read on the device; nothing is kept.
//   scan.check    the replica, the doubts, the letter picker and "Start playing"
//                 (CheckScreen.qml). "Start playing" hands the confirmed board to
//                 the assistant and asks the shell to show it. The secondary
//                 action says what it does: "Scan again" for a camera board,
//                 "Start over" for a typed one.
//
// The frame takes the room the screen has: it is the largest 4:3 picture that
// fits between the prompt and the buttons, so a tall window shows a big frame and
// a short one a small frame and nothing is left empty below it. When the window
// is too short even for the smallest frame the stage scrolls instead of clipping.
//
// With no camera (a desktop with no webcam, or a kit without Qt Multimedia) the
// camera's absence is said in words, "Try camera again" looks for it afresh, and
// "Type in your board" is the way on: the check screen with an empty 4x4
// (docs/design/game.md, "No camera").
//
// The two modals of Mode A, the letter picker and the question about ringed
// tiles, are Sheets of this screen and not of the check screen inside it: their
// scrim covers the bar and the stepper as well, one rule for every modal.
FocusScope {
    id: root

    required property ScanModel scan
    required property AssistModel assist

    // Back from `scan.ready`: out of Mode A, to wherever the person came from.
    signal closed
    // "Start playing" has confirmed a board and the assistant has it.
    signal playRequested

    readonly property string scanState: root.scan.state
    readonly property bool cameraShown: root.scan.cameraAvailable
    // The live view is shown only while the camera is running: ScanModel starts
    // it on scan.ready and stops it as soon as a frame is in hand.
    readonly property bool cameraRunning: root.scan.cameraCapturing
    readonly property bool previewShown: cameraPreview.active
    readonly property string cameraMessage: ScanWording.cameraProblem(root.scan.cameraProblem)
    // The camera's absence is already in cameraMessage; a capture pressed without
    // one must not say it a second time.
    readonly property string readMessage: root.scan.readProblem === "no_camera"
                                              ? "" : ScanWording.readProblem(root.scan.readProblem)
    // The step the person is on: Place while the frame is shown, Check once the
    // draft is in front of them. Play is the assistant's screen.
    readonly property int step: root.scanState === "check" ? 2 : 1
    // The interface's direction, set by the shell: Hebrew and Yiddish are right
    // to left. The frame, the bar and the check screen's rows follow it; the
    // replica of the board does not.
    property bool rightToLeft: Theme.rightToLeft
    // For the RTL check in the stub shell: the bar's way back follows the
    // leading edge, and the window's mirroring reaches this screen.
    readonly property real backButtonX: Theme.rightToLeft ? width - Theme.space4 - Theme.targetMin
                                                          : Theme.space4
    readonly property bool mirrored: LayoutMirroring.enabled
    // The check screen's confirm button, for the RTL check.
    readonly property real checkYesPlayX: check.yesPlayX

    LayoutMirroring.enabled: root.rightToLeft
    LayoutMirroring.childrenInherit: true

    activeFocusOnTab: true

    Component.onCompleted: {
        // A screen that opens on `check` is one the person came back to; the
        // camera stays off. A screen that opens on `ready` is a new scan.
        if (root.scan.state === "ready") {
            root.scan.open();
        }
    }

    Component.onDestruction: root.scan.close()

    // The way back: from the check screen it is the step back (to the frame and a
    // camera that is on again, which is also a camera board's "Scan again"); from
    // the ready screen it leaves Mode A. A typed board's "Start over" is the
    // check screen's own secondary action, not the bar: it keeps the person on
    // the typing task.
    function back() {
        if (root.scanState === "check" || root.scanState === "reading") {
            root.scan.readAgain();
        } else {
            root.scan.close();
            root.closed();
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.page
    }

    // --- the bar ---------------------------------------------------------------
    AppBar {
        id: appBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        showBack: true
        showWordmark: false
        showParent: false
        title: root.scanState === "check" ? ScanWording.checkTitle
             : (root.scanState === "reading" ? ScanWording.reading : qsTr("Set up a board"))
        onBack: root.back()
    }

    // --- the steps -------------------------------------------------------------
    // Place, Check, Play: the grown-up's three steps, the one they are on filled.
    // The check screen inside this screen is step 2; Play is the assistant's
    // screen, which the shell shows after "Start playing".
    ScanStepper {
        id: stepper
        anchors.top: appBar.bottom
        anchors.topMargin: Theme.space3
        anchors.horizontalCenter: parent.horizontalCenter
        step: root.step
    }

    // --- scan.ready and scan.reading ------------------------------------------------
    Flickable {
        id: readyStage
        anchors.top: stepper.bottom
        anchors.topMargin: Theme.space3
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.scanState !== "check"
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        contentWidth: width
        // The column fills the stage; only when the stage is shorter than the
        // column's least height does it scroll.
        contentHeight: Math.max(height, stage.leastHeight)

        ColumnLayout {
            id: stage
            x: Theme.space4
            width: readyStage.width - 2 * Theme.space4
            height: readyStage.contentHeight - Theme.space4
            spacing: Theme.space3

            // What the frame cannot be smaller than, and what it likes to be.
            readonly property real frameLeast: Theme.targetPrimary * 1.5
            readonly property real frameLikes: Theme.pictureMax
            readonly property real leastHeight: stage.implicitHeight - frameHolder.Layout.preferredHeight
                                                + frameHolder.Layout.minimumHeight + Theme.space4

            Label {
                id: prompt
                Layout.fillWidth: true
                role: "heading"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.scanState === "reading" ? ScanWording.reading : ScanWording.readyTitle
            }

            // The live view: the camera's own picture while it is on, with the
            // corners of the board's place over it. With no camera the frame
            // shows a camera with a stroke through it; when the camera is off for
            // another reason (the check screen) the place keeps its picture of a
            // board, so the screen still says what it is for without a sentence.
            Item {
                id: frameHolder
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: stage.frameLeast
                Layout.preferredHeight: stage.frameLikes * 0.75

                BoardTray {
                    id: viewfinder
                    // 4:3 where there is room for it, squarer where the window is tall.
                    readonly property real aspect: Math.max(1, Math.min(4 / 3, parent.width / Math.max(1, parent.height)))

                    anchors.centerIn: parent
                    width: Math.max(1, Math.min(parent.width, parent.height * viewfinder.aspect))
                    height: viewfinder.width / viewfinder.aspect
                    showWell: false

                    Item {
                        id: view
                        anchors.fill: parent
                        anchors.margins: Theme.trayRim
                        clip: true

                        // The camera's picture. The view is a CameraPreview of the
                        // build's camera variant: a VideoOutput where the kit has
                        // Qt Multimedia, and an empty Item where it does not. The
                        // source hands its sink over in Component.onCompleted, and
                        // nothing is recorded.
                        Loader {
                            id: cameraPreview
                            anchors.fill: parent
                            active: root.cameraRunning && root.scan.cameraSource !== null
                            sourceComponent: Component {
                                CameraPreview {
                                    camera: root.scan.cameraSource
                                }
                            }
                        }

                        // The four corners of a board's place, as brackets.
                        Repeater {
                            model: 4

                            Item {
                                id: corner
                                objectName: "frameCorner"
                                required property int index
                                readonly property bool onRight: index % 2 === 1
                                readonly property bool onBottom: index >= 2
                                x: corner.onRight ? view.width - width - Theme.space4 : Theme.space4
                                y: corner.onBottom ? view.height - height - Theme.space4 : Theme.space4
                                width: Math.min(view.width, view.height) * 0.16
                                height: width

                                // The two strokes meet at the frame's corner: the
                                // horizontal one at the top for a top corner and
                                // at the bottom for a bottom one, the vertical one
                                // on the outer side in both.
                                Rectangle {
                                    objectName: "cornerHorizontal"
                                    width: parent.width
                                    height: Theme.heavy
                                    y: corner.onBottom ? parent.height - height : 0
                                    color: Theme.die
                                }
                                Rectangle {
                                    objectName: "cornerVertical"
                                    width: Theme.heavy
                                    height: parent.height
                                    x: corner.onRight ? parent.width - width : 0
                                    color: Theme.die
                                }
                            }
                        }

                        // A small board of dice, so the place says what goes in
                        // it without a sentence: a picture, not text. It is
                        // behind the camera's view while the camera is on, and
                        // gives way to the no-camera picture.
                        Grid {
                            anchors.centerIn: parent
                            columns: 4
                            spacing: Math.max(Theme.space1, view.width * 0.012)
                            opacity: 0.5
                            visible: !root.cameraRunning && root.cameraMessage === ""

                            Repeater {
                                model: 16

                                Rectangle {
                                    width: (Math.min(view.width, view.height) * 0.4
                                            - 3 * Math.max(Theme.space1, view.width * 0.012)) / 4
                                    height: width
                                    color: Theme.die
                                }
                            }
                        }

                        // No camera: the picture, and its one-word headline. The
                        // sentence that says what to do is under the frame.
                        ColumnLayout {
                            id: noCamera
                            objectName: "noCameraPicture"
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 2 * Theme.space6, Theme.pictureMax)
                            spacing: Theme.space2
                            visible: !root.cameraRunning && root.cameraMessage !== ""

                            CameraOffArt {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Math.max(Theme.space16,
                                                                Math.min(view.width * 0.3, view.height * 0.4 * 4 / 3))
                                Layout.preferredHeight: Layout.preferredWidth * 0.75
                            }
                            Label {
                                Layout.fillWidth: true
                                role: "heading"
                                horizontalAlignment: Text.AlignHCenter
                                color: Theme.die
                                text: ScanWording.noCamera
                            }
                        }

                        // While reading, a quiet band across the picture.
                        Rectangle {
                            visible: root.scanState === "reading"
                            anchors.centerIn: parent
                            width: parent.width
                            height: Theme.targetChild
                            color: Theme.scrim
                        }
                    }
                }
            }

            // The camera's absence, and a read that failed: two facts, no blame.
            Label {
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.contentWidth
                Layout.alignment: Qt.AlignHCenter
                visible: text !== ""
                role: "body"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.inkSecondary
                text: root.cameraMessage
            }
            Label {
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.contentWidth
                Layout.alignment: Qt.AlignHCenter
                visible: text !== ""
                role: "body"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.inkSecondary
                text: root.readMessage
            }

            // The camera's absence is retried here, without leaving the screen:
            // open() enumerates the devices again, so a camera that was covered,
            // busy or plugged in since the screen opened is found. When it is
            // found, the message and this button go and Capture appears; when it
            // is not, the camera says so again and the typed board stays primary.
            AppButton {
                id: tryCamera
                visible: root.scanState === "ready" && root.cameraMessage !== ""
                Layout.alignment: Qt.AlignHCenter
                size: "min"
                variant: "secondary"
                iconName: "again"
                label: ScanWording.tryCameraAgain
                accessibleName: ScanWording.tryCameraAgain
                onClicked: root.scan.retryCamera()
            }

            // Capture, and the typed board beside it: the grown-up chooses. With
            // a camera Capture is the one big action and the typed board its
            // quieter neighbour; with none the typed board is the way on and takes
            // the primary place.
            ActionBar {
                id: actions
                readonly property bool capturing: root.cameraShown && root.scanState === "ready"

                Layout.fillWidth: true
                Layout.maximumWidth: Theme.modalMax
                Layout.alignment: Qt.AlignHCenter
                tier: "child"
                primaryLabel: root.scanState !== "ready" ? ""
                              : (actions.capturing ? ScanWording.capture : ScanWording.typeBoard)
                secondaryLabel: actions.capturing ? ScanWording.typeBoard : ""
                onPrimaryClicked: {
                    if (actions.capturing) {
                        root.scan.capture();
                    } else {
                        root.scan.typeBoard();
                    }
                }
                onSecondaryClicked: root.scan.typeBoard()
            }
        }
    }

    // --- scan.check --------------------------------------------------------------------
    CheckScreen {
        id: check
        anchors.top: stepper.bottom
        anchors.topMargin: Theme.space3
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.scanState === "check"
        scan: root.scan
        assist: root.assist
        onPlayRequested: root.playRequested()
    }

    // --- the modals ---------------------------------------------------------------
    // Both cover the whole screen, bar and stepper included, and both are the
    // kit's Sheet: a heading, a 20 px radius, Escape, a focus trap.
    LetterPicker {
        id: picker
        anchors.fill: parent
        scan: root.scan
        picked: check.picked
        onChosen: check.picked = -1
        onDismissed: check.picked = -1
    }

    RingedQuestion {
        id: ringed
        anchors.fill: parent
        open: check.askingRinged
        onCheckThem: check.askingRinged = false
        onPlayAnyway: {
            check.askingRinged = false;
            check.play();
        }
    }
}
