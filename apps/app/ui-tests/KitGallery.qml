// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// Every interactive component of the kit, laid out as a screen would: the size
// matrix's subject for the kit itself (tst_sizematrix.qml). It is deliberately
// the narrow cases that break things - a panel 260 dp wide holding Clear and
// Done, a bar with every entry on it, labels that grow with the text size.
Rectangle {
    id: root

    color: Theme.page

    LayoutMirroring.enabled: Theme.rightToLeft
    LayoutMirroring.childrenInherit: true

    AppBar {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        showBack: true
        showWordmark: true
        showJewels: true
        showBoards: true
        showParent: true
    }

    Flickable {
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space4
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            width: parent.width
            spacing: Theme.space4

            // The three target sizes, labelled.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space3
                AppButton { size: "min"; label: qsTr("Open") }
                AppButton { size: "min"; compact: true; label: qsTr("Compact") }
                AppButton { size: "child"; iconName: "hint"; label: qsTr("Show me a start") }
                AppButton { size: "primary"; variant: "primary"; iconName: "shake"; label: qsTr("Shake") }
                AppButton { size: "child"; label: qsTr("Disabled"); enabled: false }
                AppButton { size: "min"; variant: "danger"; label: qsTr("Reset everything") }
                AppButton { size: "min"; variant: "ghost"; iconName: "back"; label: qsTr("Back") }
            }

            // A choice among several.
            SegmentedChoice {
                Layout.fillWidth: true
                title: qsTr("Board size")
                current: 4
                options: [{value: 3, label: qsTr("3 x 3")}, {value: 4, label: qsTr("4 x 4")},
                          {value: 5, label: qsTr("5 x 5")}, {value: 6, label: qsTr("6 x 6")}]
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space4

                // The play panel's narrow card: Clear and Done must both fit.
                Card {
                    Layout.preferredWidth: 280
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Words I found")
                    empty: false

                    Label {
                        Layout.fillWidth: true
                        role: "word"
                        text: qsTr("cat")
                    }
                    ActionBar {
                        Layout.fillWidth: true
                        primaryLabel: qsTr("Done")
                        primaryIcon: "done"
                        secondaryLabel: qsTr("Clear")
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Words I found")
                    hint: qsTr("Tap a word to hear it.")
                    empty: true
                    emptyTitle: qsTr("Your words will land here.")
                    emptyText: qsTr("Find a word and it is kept here.")
                    Layout.minimumHeight: 180
                }
            }

            // The home screen's two cards, as the narrow layout stacks them.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space4

                ModeCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 300
                    primary: true
                    title: qsTr("Play on my own")
                    subtitle: qsTr("Shake the box. Find words. Collect jewels.")
                    actionLabel: qsTr("Play")

                    JewelBox {
                        anchors.centerIn: parent
                        width: Math.min(parent.width * 0.9, 460)
                        height: Math.max(120, parent.height)
                        closed: false
                    }
                }
                ModeCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 300
                    grownUp: true
                    eyebrow: qsTr("For grown-ups")
                    title: qsTr("Play with a real board")
                    subtitle: qsTr("Use the camera, or type the board.")
                    actionLabel: qsTr("Continue")

                    TabletArt {
                        anchors.fill: parent
                    }
                }
            }

            HoldButton {
                label: qsTr("Play again")
                accessibleName: qsTr("Play again")
            }
        }
    }
}
