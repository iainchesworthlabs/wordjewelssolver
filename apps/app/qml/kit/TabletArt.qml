// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import WordJewelsSolver

// The picture of a tablet on its stand, with a 4 x 4 board on its screen and,
// optionally, the camera looking at it: the Family Assistant's card on the home
// screen. It was a 75-line block, copied for the wide and the narrow layouts in
// Home.qml, and drawn as a monitor whose stand floated below the screen when the
// card was tall.
//
// Here the picture is one unit: the tablet and its stand are sized from the room
// the item is given (the smaller of its width and its height governs) and the
// stand is hung directly under the screen, so it never detaches. The item can be
// given any size; the picture is centred in it.
Item {
    id: root

    property bool showCamera: true

    // The tablet's screen is 3:2; the stand is 22% of the tablet's height.
    readonly property real standShare: 0.22
    readonly property real screenWidth: Math.max(1, Math.min(width * 0.78, height / (0.66 + standShare)))
    readonly property real screenHeight: screenWidth * 0.66
    readonly property real standHeight: screenHeight * standShare
    readonly property real topOffset: (height - screenHeight - standHeight) / 2
    readonly property real cellGap: Math.max(2, screenWidth * 0.01)

    implicitWidth: 310
    implicitHeight: 250

    Accessible.ignored: true

    // The stand: a neck and a foot.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.topOffset + root.screenHeight - Theme.radiusXs
        width: Theme.space2 + Theme.space1 / 2
        height: root.standHeight - Theme.space1
        color: Theme.ink
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.topOffset + root.screenHeight + root.standHeight - Theme.space2
        width: root.screenWidth * 0.46
        height: Theme.space2
        radius: Theme.radiusXs
        color: Theme.ink
    }

    // The tablet.
    Rectangle {
        id: tablet
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.topOffset
        width: root.screenWidth
        height: root.screenHeight
        radius: Theme.radius
        color: Theme.ink
        border.width: Theme.heavy
        border.color: Theme.line

        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.space2
            radius: Theme.radiusSmall
            color: Theme.boardArea
            border.width: Theme.hair
            border.color: Theme.dieEdge

            Grid {
                anchors.centerIn: parent
                columns: 4
                spacing: root.cellGap
                Repeater {
                    model: 16
                    Rectangle {
                        width: (tablet.width * 0.44 - 3 * root.cellGap) / 4
                        height: width
                        radius: 2
                        color: Theme.die
                        border.width: Theme.hair
                        border.color: Theme.dieEdge
                    }
                }
            }

            // The camera looking at the board.
            Rectangle {
                visible: root.showCamera
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Theme.space2
                width: Theme.space3 + 1
                height: width
                radius: width / 2
                color: Theme.accent
                border.width: Theme.litEdge
                border.color: Theme.ink
            }
        }
    }
}
