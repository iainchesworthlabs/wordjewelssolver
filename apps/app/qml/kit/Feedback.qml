// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// The seam for sound, speech and touch feedback. Every moment in the table of
// docs/design/application.md ("Audio and touch feedback") that wants a sound, a
// spoken name or a tap of the hand calls cue(); Main.qml installs the
// application's FeedbackEngine here, and the engine turns the cue and the
// parent's settings into a short asset, a spoken word and a haptic pattern
// (wjs/feedback.hpp has the mapping). Output only: the app has no audio input.
//
// kinds: "speak" (detail: the picture's or word's name), "tap" (a UI press),
// "rattle", "tile" (the tile's letters), "tile-landed" (the pitch), "word"
// (the word), "tick", "chime", "cleared".
//
// `engine` is null in a stub shell or a test suite, where only the signal is
// recorded: the cue is still emitted, and nothing is played.
QtObject {
    signal cue(string kind, string detail)

    property var engine: null

    onCue: (kind, detail) => {
        if (engine !== null) {
            engine.cue(kind, detail);
        }
    }
}
