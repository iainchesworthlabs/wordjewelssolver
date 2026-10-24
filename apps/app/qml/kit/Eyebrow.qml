// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver

// The small capital line above a heading or beside a count: IBM Plex Mono 12,
// capitals, 0.08 em apart. The capitals come from `font.capitalization`, not
// from an uppercased source string, so a translation is written (and read by a
// screen reader) in its own case, German keeps its mixed-case nouns where the
// language wants them, and Hebrew, which has no case, is not spaced out.
// `accented` is the emerald line of a primary card; the default is the muted ink.
Label {
    property bool accented: false

    role: "mono"
    color: accented ? Theme.accent : Theme.inkSecondary
}
