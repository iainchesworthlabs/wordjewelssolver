// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// Every sentence and every screen-reader name of Solo Play's chrome, in one
// file: the bar's way back and corner buttons, the box's size choice and Shake,
// the tray, the hint, the history and the summary. A screen says nothing of its
// own.
//
// The game itself is English: the dice letters, the words in the tray and the
// word list are not translated (docs/design/application.md, "Languages").
// These are the words around them.
QtObject {
    id: root

    // --- the bar -------------------------------------------------------------
    readonly property string back: qsTr("Back")
    readonly property string earlierBoards: qsTr("Earlier boards")
    readonly property string forGrownUps: qsTr("For grown-ups")

    // --- the box -------------------------------------------------------------
    //: Shake the box to roll the dice; the button and Space do the same.
    readonly property string shake: qsTr("Shake")
    //: The line under the closed box, before the first roll.
    readonly property string shakeToOpen: qsTr("Shake the box to start")

    //: The size of the board: "3 by 3 board".
    function sizeBoard(side) {
        return qsTr("%1 by %1 board").arg(side);
    }

    //: The box panel's title over the row of board sizes.
    readonly property string chooseBoard: qsTr("Choose a board")
    readonly property string chooseBoardHint: qsTr("3 × 3 is a good start. Bigger boards hold more words.")
    //: The box panel's card of three steps.
    readonly property string howItWorks: qsTr("How it works")
    readonly property string howShake: qsTr("Shake the box.")
    readonly property string howFind: qsTr("Find a word.")
    readonly property string howCollect: qsTr("Collect the jewels.")
    //: The line under the box while it shakes.
    readonly property string shaking: qsTr("Shaking the box…")

    // --- the panel -----------------------------------------------------------
    readonly property string wordsFound: qsTr("Words I found")
    //: Light the first tile of a word that has not been found yet.
    readonly property string showStart: qsTr("Show me a start")
    //: Hand in the word that has been traced or tapped.
    readonly property string done: qsTr("Done")

    function found(word) {
        return qsTr("%1, found").arg(word);
    }

    // --- the history sheet -----------------------------------------------------
    readonly property string close: qsTr("Close")
    //: The history sheet's empty state: nothing has been played yet.
    readonly property string noBoardsTitle: qsTr("No boards yet")
    readonly property string noBoardsText: qsTr("Boards you play will be kept here.")

    //: A board in the history sheet: its size and how many words were found.
    function boardCaption(side, words) {
        return words === 1 ? qsTr("%1 × %1, 1 word").arg(side)
                           : qsTr("%1 × %1, %2 words").arg(side).arg(words);
    }

    // --- the summary ------------------------------------------------------------
    readonly property string playAgain: qsTr("Play again")
    readonly property string home: qsTr("Home")

    // --- the board's names for a screen reader -----------------------------------
    readonly property string board: qsTr("Board")
    //: The screen-reader name of the ring that lights the start tile.
    readonly property string startHere: qsTr("Start here")
    //: The screen-reader name of the second ring, when the child asks for more.
    readonly property string nextLetter: qsTr("The next letter")
    //: The second rung of the help: two tiles are lit.
    readonly property string twoLetters: qsTr("Show me two letters")
    //: Put the help away and quiet the board again.
    readonly property string hideHint: qsTr("Hide the hint")
    //: End this board early: the words are saved, and the child sees what was found.
    readonly property string finishBoard: qsTr("Finish board")
    readonly property string finishTitle: qsTr("Board finished!")
    readonly property string finishBody: qsTr("Your words are safe in your collection.")
    //: The board's clock for a screen reader: what it is, then what it shows.
    function clockName(mode, text) {
        if (mode === "down") {
            return qsTr("Time left: %1").arg(text);
        }
        return qsTr("Time on this board: %1").arg(text);
    }

    //: On the finish summary, after a count-up board.
    function playedFor(text) {
        return qsTr("You played for %1.").arg(text);
    }

    //: The finish summary's reward line: how many stars the board earned.
    function summaryStars(n) {
        return n === 1 ? qsTr("1 star") : qsTr("%1 stars").arg(n);
    }

    //: The finish summary's reward line: the board's points.
    function summaryPoints(n) {
        return n === 1 ? qsTr("1 point") : qsTr("%1 points").arg(n);
    }

    //: The finish summary's list of the words found, with their count: how many
    //: words it holds, so a finished board says its total without a count in
    //: the title.
    function yourWordsCount(n) {
        return n === 1 ? qsTr("Your word") : qsTr("Your words (%1)").arg(n);
    }
    //: The finish summary of a count-down board whose time ran out.
    readonly property string timeRanOut: qsTr("The time ran out. Everything you found is kept.")
    //: The finish summary's words card when no word was found.
    readonly property string summaryNoWords: qsTr("No words this time. Every board is a new chance.")
    //: Shown under the button at the first rung.
    readonly property string startHintShown: qsTr("The first letter is lit. Trace a word from it.")
    //: Shown under the button at the second rung.
    readonly property string twoLettersShown: qsTr("Two letters are lit. Find the rest of the word.")

    //: After a find made from the hint's start: more words begin with those tiles.
    function moreWordsWithStart(n) {
        return n === 1 ? qsTr("1 more word starts with these letters.")
                       : qsTr("%1 more words start with these letters.").arg(n);
    }

    //: After a find made from the hint's start: nothing else begins with those tiles.
    readonly property string noMoreWordsWithStart: qsTr("No more words start with these letters.")

    function tileName(letters, row, column) {
        return qsTr("%1, row %2, column %3").arg(letters).arg(row).arg(column);
    }

    function jewelName(row, column) {
        return qsTr("Jewel, row %1, column %2").arg(row).arg(column);
    }
}
