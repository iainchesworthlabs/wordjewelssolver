// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// The question asked when "Start playing" is pressed with ringed tiles left: the
// reader was not sure about them, and the press is the grown-up's statement that
// they looked (docs/design/scanner.md, "Confirm and correct"). The screen is
// free to ask first, and does: a board with a wrong letter finds words that are
// not there and misses ones that are, which a five-year-old cannot tell from a
// real find. The grown-up chooses between looking again and playing as it is;
// neither is a mistake and nothing is blamed.
//
// A Sheet, like the letter picker: the scrim covers the whole screen, Escape and
// a press on the scrim both mean "look again" (the safe answer), and focus is
// kept inside.
Sheet {
    id: root

    // The grown-up wants to look at the ringed tiles first.
    signal checkThem
    // They looked, and the board is as it should be.
    signal playAnyway

    title: ScanWording.ringedTitle
    closeLabel: ScanWording.closePicker
    maxWidth: Theme.modalMax
    onClose: root.checkThem()

    Label {
        Layout.fillWidth: true
        role: "body"
        text: ScanWording.ringedBody
        wrapMode: Text.WordWrap
    }

    ActionBar {
        Layout.fillWidth: true
        tier: "child"
        primaryLabel: ScanWording.checkThem
        secondaryLabel: ScanWording.playAnyway
        onPrimaryClicked: root.checkThem()
        onSecondaryClicked: root.playAnyway()
    }
}
