// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// Place, Check, Play: the grown-up's three steps, as three numbered badges joined
// by a line. The step they are on is filled emerald, the ones behind them carry a
// check mark and the one ahead is an outline, so the state is a shape as well as
// a colour. Every label is 14 px or more (the 12 px mono line it replaces sat
// 20 px under the bar and was the smallest text on the screen), and the row reads
// in the interface's direction: it mirrors in Hebrew and Yiddish.
//
// `step` is 1 for Place, 2 for Check and 3 for Play. The Play step is the
// assistant's screen, which the shell shows after "Start playing", so this screen
// never has it as the current one.
Item {
    id: root

    property int step: 1
    readonly property var names: [ScanWording.stepPlace, ScanWording.stepCheck, ScanWording.stepPlay]

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    Accessible.role: Accessible.Grouping
    Accessible.name: ScanWording.stepsName

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Theme.space2

        Repeater {
            model: 3

            RowLayout {
                id: entry
                required property int index

                readonly property int number: entry.index + 1
                readonly property string status: entry.number < root.step ? "done"
                                                 : entry.number === root.step ? "current" : "ahead"

                spacing: Theme.space2

                Accessible.role: Accessible.ListItem
                Accessible.name: ScanWording.stepName(entry.number, root.names[entry.index], entry.status)

                Rectangle {
                    objectName: "stepBadge"
                    Layout.preferredWidth: Theme.iconMd
                    Layout.preferredHeight: Theme.iconMd
                    radius: width / 2
                    color: entry.status === "current" ? Theme.accent
                         : entry.status === "done" ? Theme.selectedFill : Theme.card
                    border.width: entry.status === "current" ? 0 : Theme.heavy
                    border.color: entry.status === "done" ? Theme.selectedEdge : Theme.outline

                    Label {
                        anchors.centerIn: parent
                        visible: entry.status !== "done"
                        decorative: true
                        role: "mono"
                        text: entry.number
                        font.capitalization: Font.MixedCase
                        color: entry.status === "current" ? Theme.buttonInk : Theme.inkSecondary
                    }
                    Icon {
                        anchors.centerIn: parent
                        visible: entry.status === "done"
                        width: Theme.iconSm
                        height: Theme.iconSm
                        name: "done"
                        color: Theme.accent
                    }
                }

                Label {
                    objectName: "stepLabel"
                    decorative: true
                    role: "caption"
                    text: root.names[entry.index]
                    font.weight: entry.status === "current" ? Font.DemiBold : Font.Normal
                    color: entry.status === "current" ? Theme.accent : Theme.inkSecondary
                }

                // The line to the next step.
                Rectangle {
                    visible: entry.index < 2
                    Layout.preferredWidth: Theme.space6
                    Layout.preferredHeight: Theme.heavy
                    Layout.leftMargin: Theme.space1
                    Layout.rightMargin: Theme.space1
                    radius: Theme.radiusXs
                    color: entry.status === "done" ? Theme.accent : Theme.line
                }
            }
        }
    }
}
