// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// The names of home's two cards and the grown-up entry, in one place. A card's
// title is also its accessible name, so a screen reader says what is written on
// the card (a name that differs from the visible title makes a control that
// cannot be found by what it says).
QtObject {
    readonly property string soloPicture: qsTr("Play on my own")
    readonly property string soloEyebrow: qsTr("For children")
    readonly property string familyPicture: qsTr("Play with a real board")
    readonly property string forGrownUps: qsTr("For grown-ups")
}
