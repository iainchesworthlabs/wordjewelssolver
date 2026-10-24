// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// A screen with a Sheet open on it, for the size matrix: the sheet must stay
// inside the window at every size, and its Close button and the buttons it
// holds must be 48 dp and whole.
Rectangle {
    color: Theme.page

    AppBar {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        title: qsTr("Solo Play")
    }

    Sheet {
        anchors.fill: parent
        open: true
        title: qsTr("My boards")

        Label {
            Layout.fillWidth: true
            role: "body"
            text: qsTr("The boards you played are kept here, newest first.")
            wrapMode: Text.WordWrap
        }
        ActionBar {
            Layout.fillWidth: true
            primaryLabel: qsTr("Play again")
            secondaryLabel: qsTr("Home")
        }
    }
}
