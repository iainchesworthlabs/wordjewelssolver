// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// The action zone of a play screen: the one primary action and its quieter
// neighbours, on one height ladder. It replaces the four different button styles
// of the Solo panel, the three mismatched buttons of Mode A's assist row and the
// right-aligned pair on the check screen.
//
//   tier "child"    secondary 64 dp, primary 96 dp   (Clear / Done, Shake / Play again)
//   tier "parent"   secondary 48 dp, primary 64 dp   (Cancel / Save)
//
// In a row the primary action is at the trailing end (right in a left-to-right
// language, left in a right-to-left one) and the secondary before it. Each fills
// the width it is given and none falls below 64 dp. When the buttons do not fit
// one row at their natural width - a 244 px panel at 150% text - they stack, one
// per line and each full width, with the primary last, instead of truncating a
// word to "Cl...". Any further buttons go in as children and sit between the two.
// Every button is labelled, and a label wraps rather than being cut when its button
// is narrower than the word (a long translation, 150% text); an icon beside it is
// optional.
Item {
    id: root

    property string tier: "child"
    property string primaryLabel: ""
    property string primaryIcon: ""
    property string primarySpeaks: ""
    property bool primaryEnabled: true
    property string primaryVariant: "primary"
    property string secondaryLabel: ""
    property string secondaryIcon: ""
    property string secondarySpeaks: ""
    property bool secondaryEnabled: true
    property real minimumButtonWidth: Theme.targetChild

    default property alias extra: extras.data

    signal primaryClicked
    signal secondaryClicked

    readonly property string primarySize: tier === "parent" ? "child" : "primary"
    readonly property string secondarySize: tier === "parent" ? "min" : "child"

    readonly property int visibleCount: (secondaryButton.visible ? 1 : 0) + (extras.visible ? 1 : 0)
                                        + (primaryButton.visible ? 1 : 0)
    // The width the buttons want in one row.
    readonly property real naturalWidth: (secondaryButton.visible ? secondaryButton.implicitWidth : 0)
                                         + (extras.visible ? extras.implicitWidth : 0)
                                         + (primaryButton.visible ? primaryButton.implicitWidth : 0)
                                         + Math.max(0, visibleCount - 1) * Theme.space3
    readonly property bool stacked: visibleCount > 1 && naturalWidth > root.width

    implicitWidth: grid.implicitWidth
    implicitHeight: grid.implicitHeight

    GridLayout {
        id: grid
        anchors.left: parent.left
        anchors.right: parent.right
        columns: root.stacked ? 1 : Math.max(1, root.visibleCount)
        columnSpacing: Theme.space3
        rowSpacing: Theme.space3

        AppButton {
            id: secondaryButton
            visible: root.secondaryLabel.length > 0
            Layout.fillWidth: true
            Layout.minimumWidth: root.minimumButtonWidth
            Layout.preferredWidth: implicitWidth
            Layout.alignment: Qt.AlignVCenter
            size: root.secondarySize
            wrap: true
            variant: "secondary"
            label: root.secondaryLabel
            iconName: root.secondaryIcon
            speaks: root.secondarySpeaks
            quiet: root.tier === "parent"
            enabled: root.secondaryEnabled
            onClicked: root.secondaryClicked()
        }

        RowLayout {
            id: extras
            visible: extras.children.length > 0
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.space3
        }

        AppButton {
            id: primaryButton
            visible: root.primaryLabel.length > 0
            Layout.fillWidth: true
            Layout.minimumWidth: root.minimumButtonWidth
            Layout.preferredWidth: implicitWidth
            Layout.alignment: Qt.AlignVCenter
            size: root.primarySize
            wrap: true
            variant: root.primaryVariant
            label: root.primaryLabel
            iconName: root.primaryIcon
            speaks: root.primarySpeaks
            quiet: root.tier === "parent"
            enabled: root.primaryEnabled
            onClicked: root.primaryClicked()
        }
    }
}
