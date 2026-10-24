// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// A name, what it does, and the control: side by side where there is room, one
// above the other where there is not. A card of the kit's look (the card colour
// and the `line` edge); the name and the hint are Labels.
Rectangle {
    id: root

    property string title
    property string hint
    default property alias control: slot.data

    readonly property bool narrow: width < 560

    implicitHeight: layout.implicitHeight + 2 * Theme.space4
    radius: Theme.radius
    color: Theme.card
    border.width: Theme.hair
    border.color: Theme.line

    GridLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Theme.space4
        columns: root.narrow ? 1 : 2
        columnSpacing: Theme.space6
        rowSpacing: Theme.space2

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: root.narrow ? root.width : root.width * 0.4
            spacing: Theme.space1
            Label {
                Layout.fillWidth: true
                role: "label"
                text: root.title
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: root.hint.length > 0
                role: "caption"
                text: root.hint
                wrapMode: Text.WordWrap
            }
        }

        ColumnLayout {
            id: slot
            Layout.fillWidth: true
            Layout.preferredWidth: root.narrow ? root.width : root.width * 0.6
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.space2
        }
    }
}
