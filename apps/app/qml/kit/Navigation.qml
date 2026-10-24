// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// Where the person is, and what is wired. A screen is named by its id
// (docs/design/application.md, "Screens and navigation"); the solo.* ids are one
// screen here, "solo", whose own state says which of them it shows.
//
// Back goes where the person came from, with the exceptions the screens make
// (from solo.resting it goes home, and from solo.playing it goes home with the
// round left in progress, to be found on the next visit).
QtObject {
    id: root

    property string screen: "home"
    property var history: []

    // Whether a screen exists for these, so that Home shows a picture only when
    // pressing it does something. Whoever adds the screen sets them in the same
    // change (the family game's scanner; the parent area's gate is routed in Main).
    property bool parentAvailable: true
    property bool scanAvailable: true
    property bool jewelsAvailable: true

    function go(id) {
        if (id === screen) {
            return;
        }
        history = history.concat([screen]);
        screen = id;
    }

    // A new round of a screen that was already reached (Mode A's "New board"):
    // the screen is entered as if from home, so Back from it goes home and not
    // to the round just left. Not a way on: a screen that is a step forward uses
    // go().
    function restart(id) {
        if (id === screen) {
            return;
        }
        history = ["home"];
        screen = id;
    }

    // A step within the same place (a new scan for the assistant), without
    // pushing the screen being left onto the history: Back then goes to the
    // place the workflow was entered from, not to the board just left.
    function replace(id) {
        if (id === screen) {
            return;
        }
        screen = id;
    }

    function back() {
        if (history.length === 0) {
            return;
        }
        screen = history[history.length - 1];
        history = history.slice(0, history.length - 1);
    }

    function home() {
        history = [];
        screen = "home";
    }
}
