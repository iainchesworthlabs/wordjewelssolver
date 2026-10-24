// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// Every sentence a parent reads on the parent area's screens, in one place.
//
// The wording is a **proposal for the literacy review** (gate G7,
// docs/design/dashboard.md, "Wording"; docs/product-requirements.md, 12.6): it
// is observations and never assessments, and never about a gap. The sentences
// the design quotes are here as it quotes them:
//
//   "Words with ai have been on 6 boards. 4 were found."
//   "Found without a start tile: 3."
//   "This week: 3 boards cleared, 14 stars, 2 new words."
//   "not met yet"
//   "few words use this sound yet"
//   "on 6 boards; found on 1"
//
// A sound that has not been met says "not met yet", never "missed". Nothing is
// said about a week in which the child did not play: a gap in play is not a
// deficit. The test parent.wording holds the file to the design's sentences and
// to a list of words that must not be in it ("weak", "mastered", "missed"...).
//
// A number is never put in a sentence by pasting: each sentence that counts has
// its singular ("1 board") and its plural ("6 boards"), because the language of
// the sentence is this file's and not the code's.
QtObject {
    // --- the screens ------------------------------------------------------
    readonly property string wordmarkTitle: qsTr("Word Jewels")
    readonly property string appName: qsTr("Word Jewels Solver")
    readonly property string tagline: qsTr("A word-finding game for young readers, played with a grown-up.")
    readonly property string parentArea: qsTr("Parent area")
    readonly property string back: qsTr("Back")
    readonly property string privacyLine: qsTr("Kept on this device. No recording, no network.")
    readonly property string tabDashboard: qsTr("Dashboard")
    readonly property string tabSettings: qsTr("Settings")
    //: The About tab of the parent area, and the heading of the page it shows.
    readonly property string tabAbout: qsTr("About")
    //: The Licences tab of the parent area, and the heading of the page it shows.
    readonly property string tabLicences: qsTr("Licences")
    readonly property string nothingYet: qsTr("Nothing to show yet.")

    // --- the store's notes ---------------------------------------------------
    readonly property string noteUnreadable: qsTr("The saved progress could not be read, so a new one was started. The old file was kept on this device.")
    readonly property string noteNewer: qsTr("The saved progress was made by a newer version of this app. It has been left as it is, and nothing will be saved this time.")
    readonly property string noteUnsaved: qsTr("Progress could not be saved on this device. Check that the app can write to your user folder, then choose Retry.")
    readonly property string retry: qsTr("Retry")
    readonly property string saved: qsTr("Saved on this device.")
    readonly property string saveStillNotSaved: qsTr("Still not saved. The board can still be played; progress may not last after the app closes.")

    // --- the gate -------------------------------------------------------------
    readonly property string gateTitle: qsTr("For grown-ups")
    readonly property string gateInstruction: qsTr("Press and hold for two seconds.")
    readonly property string gateWhy: qsTr("This keeps little fingers out of the settings and the progress.")
    readonly property string gateKeys: qsTr("With a keyboard, hold Space or Enter for two seconds.")
    readonly property string gateHoldButton: qsTr("Hold to enter")
    readonly property string gateHoldHint: qsTr("Keep holding until the bar is full")
    readonly property string gateSpot: qsTr("Open the parent area")

    // --- the dashboard ---------------------------------------------------------
    //: The graphemes of the phonics phase, and how often each has come up.
    readonly property string welcomeTitle: qsTr("Play with a real board")
    readonly property string welcomeBody: qsTr("Set the tablet beside the board. A grown-up checks the letters before the child plays.")
    readonly property string setUpBoard: qsTr("Set up a real board")
    readonly property string backToPlay: qsTr("Back to play")
    readonly property string soundsHeading: qsTr("Sounds")
    readonly property string vaultHeading: qsTr("The vault")
    //: The best board found at each board size.
    readonly property string bestHeading: qsTr("Best")
    //: The heading over the dashboard's records (the week, the sounds, the
    //: vault, the best), marking them off from the action above them.
    readonly property string recordsHeading: qsTr("How the play is going")
    readonly property string notMetYet: qsTr("not met yet")
    readonly property string oftenFound: qsTr("Often found")
    readonly property string fewWords: qsTr("few words use this sound yet")

    function boards(n) {
        return n === 1 ? qsTr("1 board") : qsTr("%1 boards").arg(n);
    }

    function stars(n) {
        return n === 1 ? qsTr("1 star") : qsTr("%1 stars").arg(n);
    }

    // "Words with ai have been on 6 boards. 4 were found."  (letters in italics)
    function soundSentence(letters, met, found) {
        const foundPart = found === 1 ? qsTr("1 was found.") : qsTr("%1 were found.").arg(found);
        return qsTr("Words with <i>%1</i> have been on %2. %3").arg(letters).arg(boards(met)).arg(foundPart);
    }

    // "Found without a start tile: 3."
    function foundWithoutStartTile(n) {
        return qsTr("Found without a start tile: %1.").arg(n);
    }

    // "on 6 boards; found on 1": the same two counts, shorter.
    function onBoardsFoundOn(met, found) {
        return qsTr("on %1; found on %2").arg(boards(met)).arg(found);
    }

    // "This week: 3 boards cleared, 14 stars, 2 new words." A part that is
    // nothing is left out, and a week with nothing in it is not said at all
    // (weekPlayed is the caller's check): a gap in play is not a deficit.
    function weekSentence(cleared, starCount, newWords) {
        const parts = [];
        if (cleared > 0) {
            parts.push(cleared === 1 ? qsTr("1 board cleared") : qsTr("%1 boards cleared").arg(cleared));
        }
        if (starCount > 0) {
            parts.push(stars(starCount));
        }
        if (newWords > 0) {
            parts.push(newWords === 1 ? qsTr("1 new word") : qsTr("%1 new words").arg(newWords));
        }
        return qsTr("This week: %1.").arg(parts.join(", "));
    }

    function longestWord(word) {
        return qsTr("The longest word: %1.").arg(word);
    }

    // "3 x 3": a board's size, as the game writes it.
    function boardSizeLabel(size) {
        return qsTr("%1 x %1").arg(size);
    }

    // "3 x 3: 12 stars"
    function bestLine(size, starCount) {
        return qsTr("%1: %2").arg(boardSizeLabel(size)).arg(stars(starCount));
    }

    // A count-down's length, in whole minutes: "5 min".
    function timerLengthLabel(n) {
        return qsTr("%1 min").arg(n);
    }

    // The Text size choices: "125%".
    function textSizeLabel(percent) {
        return qsTr("%1%").arg(percent);
    }

    // --- the settings ----------------------------------------------------------
    readonly property string settingsTitle: qsTr("Settings")
    readonly property string settingsSubtitle: qsTr("How the game behaves for your child.")
    //: The settings' groups: headings, written in the language's own case.
    readonly property string groupPlay: qsTr("Play")
    readonly property string groupSoundTouch: qsTr("Sound and touch")
    readonly property string groupAppearance: qsTr("Appearance")
    readonly property string groupLanguage: qsTr("Language")
    readonly property string settingsHelp: qsTr("Help")
    readonly property string settingsHelpHint: qsTr("The start tile glows to show where a word begins. Two letters is the most the game ever shows.")
    //: The help's middle level: one tile glows, and a second press hides it.
    readonly property string valueHelpStart: qsTr("A start tile")
    //: The help's fullest level: the start tile, then its first two letters.
    readonly property string valueHelpTwo: qsTr("Two letters")
    //: The board setting (standard dice or the phonics words), not its size.
    readonly property string settingsBoard: qsTr("Board")
    readonly property string settingsBoardHint: qsTr("Standard: dice and any word. Phonics: the words of a phase, with their sounds.")
    //: A phase of the phonics scheme; the repository's are named A and B.
    readonly property string settingsPhase: qsTr("Phase")
    //: The graphemes the school is working on this week.
    readonly property string settingsFocus: qsTr("Focus sounds")
    readonly property string settingsFocusHint: qsTr("The sounds the school is on this week. The phonics board prefers words with them.")
    //: The phonics board's words, only where the build carries them.
    readonly property string settingsBoardNoPhonics: qsTr("Phonics needs the phonics data, which is not in this build.")
    readonly property string settingsBoardSize: qsTr("Board size")
    readonly property string settingsBoardSizeHint: qsTr("The size the box offers first.")
    readonly property string settingsTimer: qsTr("Timer")
    readonly property string settingsTimerHint: qsTr("Count up shows how long a board has been open. Count down ends the board when it reaches zero.")
    //: How long a count-down board has, chosen in whole minutes.
    readonly property string settingsTimerLength: qsTr("Time on the board")
    readonly property string settingsSoundItOut: qsTr("Sound it out")
    readonly property string settingsCelebration: qsTr("Celebration sounds")
    //: The sound-effects switch, not the sounds of the letters.
    readonly property string settingsSound: qsTr("Sound")
    readonly property string settingsVibration: qsTr("Vibration")
    readonly property string settingsShake: qsTr("Shake the device")
    readonly property string settingsMotion: qsTr("Motion")
    readonly property string settingsMotionHint: qsTr("System follows the device's own setting.")
    readonly property string settingsColour: qsTr("Colour scheme")
    readonly property string settingsColourHint: qsTr("System follows the device's own light or dark setting.")
    readonly property string settingsTextSize: qsTr("Text size")
    readonly property string settingsSchoolFont: qsTr("School font")
    //: The interface's language; the board and its letters stay English.
    readonly property string settingsLanguage: qsTr("Language")
    readonly property string settingsLanguageHint: qsTr("The words on the screens. The board and its letters stay English.")

    // The languages on offer, each named in its own script, in the order the
    // author gave them (docs/design/application.md, "Languages"): English
    // first, since it is the language of the board. The names are not
    // translated: a parent reading Hebrew sees "עברית" whatever the screens are
    // in. `""` is the device's own language.
    readonly property var languageChoices: [
        {code: "en", label: "English"},
        {code: "fr", label: "Français"},
        {code: "es", label: "Español"},
        {code: "de", label: "Deutsch"},
        {code: "yi", label: "ייִדיש"},
        {code: "he", label: "עברית"},
        {code: "zh_CN", label: "中文"},
        {code: "ja", label: "日本語"}
    ]

    readonly property string valueOn: qsTr("On")
    readonly property string valueOff: qsTr("Off")
    readonly property string valueStandard: qsTr("Standard")
    readonly property string valuePhonics: qsTr("Phonics")
    //: All of the sounds: each sound of the word, then the word.
    readonly property string valueFull: qsTr("Full")
    readonly property string valueWordOnly: qsTr("The word only")
    //: The celebration sounds at a lower volume, not silence.
    readonly property string valueQuiet: qsTr("Quiet")
    //: Follow the device's own setting.
    readonly property string valueSystem: qsTr("System")
    readonly property string valueReduced: qsTr("Reduced")
    readonly property string valueLight: qsTr("Light")
    readonly property string valueDark: qsTr("Dark")
    readonly property string valueCountUp: qsTr("Count up")
    readonly property string valueCountDown: qsTr("Count down")

    // --- reset -------------------------------------------------------------------
    //: The two confirmed resets at the foot of the settings.
    readonly property string resetHeading: qsTr("Reset")
    readonly property string resetBoards: qsTr("Clear boards and finds")
    readonly property string resetBoardsSays: qsTr("This clears the last boards and the words found on them. Stars and stickers stay.")
    readonly property string resetStars: qsTr("Clear stars and stickers")
    readonly property string resetStarsSays: qsTr("This clears the stars, the best of each board size and the stickers. Boards and finds stay.")
    //: The second press that confirms a reset, not the word "clear" elsewhere.
    readonly property string resetConfirm: qsTr("Clear")
    readonly property string resetKeep: qsTr("Keep")
    readonly property string resetDone: qsTr("Cleared.")

    // --- about -----------------------------------------------------------------------
    readonly property string licencesHeading: qsTr("Licences")
    readonly property string privacyHeading: qsTr("Privacy")
    readonly property string privacySummary: qsTr("Kept on this device. No recording, no network. Nothing moves between devices.")

    function version(v) {
        return qsTr("Word Jewels Solver, version %1").arg(v);
    }

    readonly property string licenceNotice: qsTr("This program is free software. You can redistribute it and change it under the terms of the GNU General Public License, version 3, or (at your option) any later version.")
    readonly property string licenceParts: qsTr("It is built with Qt. The English word list is ENABLE, a public-domain list. The licences of everything in a package are listed with the package.")
    //: Above the full notices, in the Licences page's own scroll box.
    readonly property string licencesFull: qsTr("Everything this build includes, and the licence of each part:")
    readonly property string privacyKept: qsTr("This app keeps what this screen shows, on this device only: the words found, the size of each board, the day, whether a start tile was shown, the stars and stickers, the last twenty boards and your settings.")
    readonly property string privacyNever: qsTr("It does not record sound or pictures, does not use the network and sends nothing anywhere. It does not keep how long anyone plays.")
    readonly property string privacyDevice: qsTr("Nothing moves between devices. A new device starts fresh.")
}
