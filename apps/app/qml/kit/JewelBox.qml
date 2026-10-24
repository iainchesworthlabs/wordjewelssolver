// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import WordJewelsSolver

// The jewel box: a deep tray, cream dice and a soft lid. It is 2D on purpose:
// it is the object a child sees before the shake, and the calm object on the
// resting screen. The 3D scene is the shake itself, not this picture.
//
// The dice are sized from the well, not only from the tray's width. The old
// rule made a 3 x 3 grid wider than the tray was tall, so the bottom row hung
// over the gold edge and a decorative gold line ran through the letters.
Item {
    id: root

    property bool closed: true
    property var tiles: ["S", "O", "S", "S", "A", "N", "N", "S", "L"]
    property bool showDice: true
    // The board size the closed box is waiting for. It changes the picture of
    // the compartments when the child chooses 4 x 4, 5 x 5 or 6 x 6.
    property int boardSize: 3

    implicitWidth: 360
    implicitHeight: 300

    readonly property int side: Math.max(1, boardSize)
    readonly property real trayWidth: Math.min(width * 0.90, height * 1.16)
    readonly property real trayHeight: trayWidth * 0.78
    readonly property real spacing: Math.max(3, trayWidth * 0.016)
    // The largest square grid that fits inside the tray's padded well.
    readonly property real cell: Math.max(1, (Math.min(trayWidth, trayHeight) * 0.76
                                               - (side - 1) * spacing) / side)
    // Where the first row of compartments begins, below the tray's top edge.
    readonly property real gridTop: (trayHeight - (side * cell + (side - 1) * spacing)) / 2
    // The lid: tall enough for its jewel and latch, and lowered over the tray's rim
    // by no more than the air above the first compartments (less its tilt, which
    // lifts one end a few px), so that every row of the chosen size shows and a
    // 6 x 6 does not read as five rows.
    readonly property real lidHeight: Math.max(Theme.targetMin, Math.min(trayHeight * 0.2, Theme.targetChild))
    readonly property real lidTilt: -2.5
    readonly property real lidOverlap: Math.max(0, Math.min(lidHeight * 0.5,
                                                 gridTop - Theme.space2
                                                 - Math.abs(Math.sin(lidTilt * Math.PI / 180)) * trayWidth / 2))

    Rectangle {
        // The shadow under the box.
        x: (root.width - root.trayWidth) / 2 + 10
        y: (root.height - root.trayHeight) / 2 + 16
        width: root.trayWidth
        height: root.trayHeight
        radius: Theme.radiusLarge
        color: Theme.trayShadow
        opacity: 0.18
    }

    BoardTray {
        id: tray
        x: (root.width - root.trayWidth) / 2
        y: (root.height - root.trayHeight) / 2
        width: root.trayWidth
        height: root.trayHeight
        rim: Theme.litEdge
        showWell: true
        wellInset: Theme.space2

        // The closed box shows the compartments of the size it is set to, so
        // choosing 6 x 6 really does change the object on the left.
        Grid {
            visible: root.closed
            anchors.centerIn: parent
            columns: root.side
            spacing: root.spacing
            opacity: 0.55
            Repeater {
                model: root.side * root.side
                Rectangle {
                    width: root.cell
                    height: width
                    radius: Theme.radiusSmall
                    color: Theme.wellCell
                    border.width: Theme.hair
                    border.color: Theme.trayLine
                }
            }
        }

        Grid {
            visible: root.showDice && !root.closed
            anchors.centerIn: parent
            columns: Math.max(1, Math.min(root.side, root.tiles.length))
            spacing: root.spacing
            Repeater {
                model: root.tiles
                TileFace {
                    required property string modelData
                    width: root.cell
                    height: width
                    letters: modelData
                }
            }
        }
    }

    // The lid: a cream slab with a gold rim, hinged at the back. It stands over the
    // tray's top edge and never over the compartments (see lidOverlap), so a closed
    // box reads as a box and its rows can be counted.
    Rectangle {
        id: lid
        visible: root.closed
        x: tray.x - 6
        y: tray.y - root.lidHeight + root.lidOverlap
        width: tray.width + 12
        height: root.lidHeight
        radius: Theme.radius
        color: Theme.lid
        border.width: Theme.litEdge
        border.color: Theme.gold
        rotation: root.lidTilt
        transformOrigin: Item.Center

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height * 0.28
            radius: parent.radius
            color: Theme.bevelLight
            opacity: 0.22
        }

        // The box's jewel mark.
        Jewel {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -Theme.space1
            width: Math.min(parent.height * 0.5, Theme.jewelMega)
            height: width
            tier: "power"
            color: Theme.gold
        }

        // A small gold latch on the lid's leading edge.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.space1
            width: Theme.space8 + Theme.space1
            height: Theme.space1 + 3
            radius: Theme.radiusXs
            color: Theme.gold
        }
    }

    // A thin gold reflection under the tray's leading edge, never across a
    // letter.
    Rectangle {
        x: tray.x + 16
        y: tray.y + tray.height + 3
        width: tray.width - 32
        height: 3
        radius: 2
        color: Theme.gold
        opacity: 0.4
    }
}
