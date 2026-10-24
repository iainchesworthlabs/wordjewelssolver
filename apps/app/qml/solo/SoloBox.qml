// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// The box's panel: choose a board, see how the game works, and shake. The sizes
// are one segmented row of 64 dp targets (every size in a line, none wrapping
// alone onto the next), Shake is the one action and sits at the foot of the
// column, where Done sits once the board is out.
//
// The panel is a viewport over the choice and the steps, with Shake fixed at its
// foot: at the minimum window height all of it fits, and if a larger text size
// makes the steps taller than the room they scroll while Shake stays in reach.
Item {
    id: root

    required property var solo
    required property var layoutRule
    required property bool wide
    // The box is shaking: the panel stays, quiet, and says so on its button.
    property bool busy: false

    signal shakeRequested

    readonly property var offered: solo.sizes.filter(size => size <= layoutRule.largestOffered)
    readonly property bool mirrored: Theme.rightToLeft
    readonly property bool small: height < 640
    readonly property real margin: Theme.space4
    readonly property real gap: small ? Theme.space3 : Theme.space4
    // One option per size offered: the numeral is the button ("3 × 3" would need
    // 80 dp, and four of those do not fit a third of 900 dp), the box picture on
    // the left shows the compartments, and a reader hears "3 by 3 board". The row
    // is a segmented control, so its options sit a hair apart: four 64 dp targets
    // and three gaps are 268 dp, exactly the panel at 900 dp.
    readonly property var sizeOptions: root.offered.map(size => ({
        value: size,
        label: String(size),
        name: SoloWording.sizeBoard(size)
    }))
    readonly property var steps: [SoloWording.howShake, SoloWording.howFind, SoloWording.howCollect]

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
    }

    Flickable {
        id: viewport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: shake.top
        anchors.bottomMargin: root.gap
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: content.implicitHeight + root.margin

        ColumnLayout {
            id: content
            x: root.margin
            y: root.margin
            width: viewport.width - 2 * root.margin
            spacing: root.gap

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space1

                Label {
                    Layout.fillWidth: true
                    role: "heading"
                    text: SoloWording.chooseBoard
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    role: "caption"
                    learning: true
                    text: SoloWording.chooseBoardHint
                    wrapMode: Text.WordWrap
                }
            }

            SegmentedChoice {
                Layout.fillWidth: true
                title: SoloWording.chooseBoard
                size: "child"
                enabled: !root.busy
                spacing: Theme.space1
                optionMinimumWidth: Theme.choiceMin
                options: root.sizeOptions
                current: root.solo.boardSize
                onChosen: value => {
                    Feedback.cue("speak", SoloWording.sizeBoard(value));
                    root.solo.chooseSize(value);
                }
            }

            Card {
                Layout.fillWidth: true
                title: SoloWording.howItWorks
                padding: root.small ? Theme.space3 : Theme.space4

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    objectName: "howList"
                    Accessible.role: Accessible.List
                    Accessible.name: SoloWording.howItWorks

                    Repeater {
                        model: root.steps

                        RowLayout {
                            id: step
                            objectName: "howItem"
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: Theme.space3

                            Accessible.role: Accessible.ListItem
                            Accessible.name: qsTr("%1. %2").arg(step.index + 1).arg(step.modelData)

                            // The step's number, a small stone of its own: real
                            // items, not spaces typed into a sentence.
                            Rectangle {
                                Layout.preferredWidth: Theme.iconMd
                                Layout.preferredHeight: Theme.iconMd
                                Layout.alignment: Qt.AlignVCenter
                                radius: width / 2
                                color: Theme.selectedFill
                                border.width: Theme.hair
                                border.color: Theme.selectedEdge

                                Label {
                                    anchors.centerIn: parent
                                    role: "caption"
                                    decorative: true
                                    text: String(step.index + 1)
                                    color: Theme.ink
                                    font.weight: Font.Bold
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                role: "body"
                                learning: true
                                decorative: true
                                text: step.modelData
                                wrapMode: Text.WordWrap
                                color: Theme.inkSecondary
                            }
                        }
                    }
                }
            }
        }
    }

    ActionBar {
        id: shake
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: root.margin
        tier: "child"
        primaryLabel: root.busy ? SoloWording.shaking : SoloWording.shake
        primaryIcon: "shake"
        primaryEnabled: !root.busy
        primarySpeaks: SoloWording.shake
        onPrimaryClicked: root.shakeRequested()
    }
}
