// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// Every sentence and screen-reader name of the child's collection. One file,
// like SoloWording, so the words the child sees can be reviewed together.
QtObject {
    id: root

    readonly property string title: qsTr("My jewels")
    readonly property string back: qsTr("Back")

    function jewels(count) {
        return count === 1 ? qsTr("1 jewel collected") : qsTr("%1 jewels collected").arg(count);
    }
    function points(count) {
        return count === 1 ? qsTr("1 point") : qsTr("%1 points").arg(count);
    }
    function words(count) {
        return count === 1 ? qsTr("1 word") : qsTr("%1 words").arg(count);
    }

    readonly property string bestTitle: qsTr("My best boards")
    // The three cells of a best board's row: its size, its stars and its words.
    function sizeLabel(size) {
        return qsTr("%1 × %2").arg(size).arg(size);
    }
    function stars(count) {
        return count === 1 ? qsTr("1 star") : qsTr("%1 stars").arg(count);
    }

    readonly property string weekTitle: qsTr("This week")
    function weekLine(stars, newWords) {
        const starText = stars === 1 ? qsTr("1 star") : qsTr("%1 stars").arg(stars);
        const wordText = newWords === 1 ? qsTr("1 new word") : qsTr("%1 new words").arg(newWords);
        return starText + "  ·  " + wordText;
    }
    function longest(word) {
        return qsTr("Longest word: %1").arg(word.toUpperCase());
    }

    readonly property string shelfTitle: qsTr("My words")
    readonly property string shelfEmpty: qsTr("Words you find will land here.")
    readonly property string boardsTitle: qsTr("Boards I played")
    readonly property string boardsEmpty: qsTr("Boards you finish will appear here.")
    function boardLine(stars, words) {
        const starText = stars === 1 ? qsTr("1 star") : qsTr("%1 stars").arg(stars);
        const wordText = words === 1 ? qsTr("1 word") : qsTr("%1 words").arg(words);
        return starText + "  ·  " + wordText;
    }

    readonly property string emptyTitle: qsTr("Your jewel box is waiting")
    readonly property string emptyBody: qsTr("Play a board and the words you find will fill this shelf.")
    readonly property string play: qsTr("Play on my own")
    readonly property string hear: qsTr("Hear the word")
}
