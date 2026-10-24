// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// Every sentence the Family Assistant's screens show, and every name they give
// to a screen reader, in one file: the i18n pass changed this file and no
// screen (docs/design/application.md, "Languages, right-to-left and fonts";
// game.md, "Mode A"). A screen says nothing of its own.
//
// The words are the design's. A message never blames a child for a camera or a
// reading: the reader's failures are "scan again", and the camera's absence is a
// fact with the typed board beside it (docs/design/game.md, "No camera";
// docs/design/scanner.md, "A doubt is data, not a message").
QtObject {
    id: root

    // --- the way in and out --------------------------------------------------
    readonly property string back: qsTr("Back")
    readonly property string parent: qsTr("For grown-ups")

    // --- scan.ready ----------------------------------------------------------
    readonly property string readyTitle: qsTr("Put the board in the frame")
    readonly property string capture: qsTr("Capture")
    readonly property string typeBoard: qsTr("Type in your board")
    readonly property string reading: qsTr("Reading the board…")
    //: A camera that was not found may still be there: covered, busy or plugged
    //: in after the screen opened. The retry button is the way to look again.
    readonly property string cameraAbsent: qsTr("No camera was found. If this device has one, it may be covered or in use by another app. You can type in your board.")
    readonly property string cameraRefused: qsTr("The camera is not allowed. Allow it in your device settings, then try again. You can type in your board.")
    readonly property string cameraBroken: qsTr("The camera did not start. Close other apps that use it and try again, or type in your board.")
    //: Look for the camera again without leaving the ready screen.
    readonly property string tryCameraAgain: qsTr("Try camera again")
    readonly property string readAgainMessage: qsTr("I could not read the board. Try again, or type it in.")
    //: The headline of the picture shown in place of the camera's view when there is no camera.
    readonly property string noCamera: qsTr("No camera")

    // --- the three steps -----------------------------------------------------
    readonly property string stepsName: qsTr("Steps")
    readonly property string stepPlace: qsTr("Place")
    readonly property string stepCheck: qsTr("Check")
    readonly property string stepPlay: qsTr("Play")

    //: A step of three, for a screen reader: "Step 2 of 3: Check, you are here".
    function stepName(number, label, status) {
        switch (status) {
        case "done":
            return qsTr("Step %1 of 3: %2, done").arg(number).arg(label);
        case "current":
            return qsTr("Step %1 of 3: %2, you are here").arg(number).arg(label);
        default:
            return qsTr("Step %1 of 3: %2").arg(number).arg(label);
        }
    }

    function cameraProblem(code) {
        switch (code) {
        case "no_camera":
        case "unavailable":
        case "no_such_device":
            return root.cameraAbsent;
        case "permission":
        case "permission_denied":
            return root.cameraRefused;
        case "already_running":
        case "failed":
            return root.cameraBroken;
        default:
            return "";
        }
    }

    function readProblem(code) {
        switch (code) {
        case "no_camera":
            return root.cameraAbsent;
        case "":
            return "";
        default:
            return root.readAgainMessage;
        }
    }

    // --- scan.check ----------------------------------------------------------
    readonly property string checkTitle: qsTr("Is this your board?")
    //: Rotate the replica of the real board a quarter turn.
    readonly property string turn: qsTr("Turn the board")
    readonly property string boardSize: qsTr("Board size")
    readonly property string doubtfulHint: qsTr("A ringed tile may be wrong. Tap it to change the letter.")
    readonly property string incomplete: qsTr("Every tile needs a letter before the board can play.")
    readonly property string noGame: qsTr("The board could not be started. Try again.")
    //: A typed board's secondary action: clear it and start typing a fresh one.
    readonly property string startOver: qsTr("Start over")
    //: A camera-read board's secondary action: go back to the frame and read it
    //: again.
    readonly property string scanAgain: qsTr("Scan again")
    //: The grown-up confirms the board and starts the round.
    readonly property string startPlaying: qsTr("Start playing")
    readonly property string doubtfulMark: "?"
    //: Shown over a blank typed board, so the empty grid says what to do.
    readonly property string typedHint: qsTr("Tap a square to choose its letter. Start playing opens when every square has one.")
    //: Under the typed board once every square has a letter.
    readonly property string typedReady: qsTr("Every square has a letter. Is it the board in front of you?")

    // --- ringed tiles left when the grown-up presses Start playing -------------
    readonly property string ringedTitle: qsTr("Check the ringed tiles?")
    readonly property string ringedBody: qsTr("The reader was not sure about the tiles with a ring. Check them first, or play with the board as it is.")
    readonly property string checkThem: qsTr("Check them")
    readonly property string playAnyway: qsTr("Play anyway")

    function sizeChoice(side) {
        return qsTr("%1 by %1 board").arg(side);
    }

    function sizeLabel(side) {
        return qsTr("%1 x %1").arg(side);
    }

    // --- the letter picker ---------------------------------------------------
    readonly property string pickerTitle: qsTr("Which letter is it?")
    readonly property string pickerSuggestions: qsTr("The reader's other thoughts")
    //: The two-letter tile as the dice print it: Q and U on one face.
    readonly property string pickerQu: qsTr("Qu")
    readonly property string pickerOther: qsTr("Other letters")
    readonly property string useLetters: qsTr("Use these")
    readonly property string closePicker: qsTr("Close")

    // --- assist.playing ------------------------------------------------------
    readonly property string wordsFound: qsTr("Words I found")
    readonly property string noWordsYet: qsTr("No words yet")
    readonly property string showStart: qsTr("Show me a start")
    readonly property string newBoard: qsTr("New board")
    //: The screen-reader name of the ring that lights the start tile.
    readonly property string startHere: qsTr("Start here")
    readonly property string earlierBoards: qsTr("Earlier boards")
    //: Puts the path being made down.
    readonly property string clear: qsTr("Clear")
    //: Hands the path being made in as a word.
    readonly property string done: qsTr("Done")

    function tileName(letters, row, column) {
        return qsTr("%1, row %2, column %3").arg(letters).arg(row).arg(column);
    }

    //: A tile the reader doubted, for a screen reader: the ring is a picture.
    function doubtfulTileName(letters, row, column) {
        return qsTr("%1, row %2, column %3, may be wrong").arg(letters).arg(row).arg(column);
    }

    readonly property string noWordsHint: qsTr("Trace a word on the board and it lands here.")
    readonly property string historyEmpty: qsTr("No earlier boards yet")
    readonly property string historyEmptyHint: qsTr("A board you leave is kept here.")

    //: One earlier board's caption: its size and how many words were found.
    function historyCaption(size, found) {
        return qsTr("%1 x %1, %2 found").arg(size).arg(found);
    }

    function foundName(word) {
        return qsTr("%1, found").arg(word);
    }

    //: The screen-reader name of the replica of the board.
    readonly property string board: qsTr("Board")
}
