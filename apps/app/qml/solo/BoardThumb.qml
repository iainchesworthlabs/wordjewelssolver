// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import WordJewelsSolver

// A small picture of a board that was played: its tray, its tiles as cream squares
// and its dead jewels as dark ones, with the jewels of the words found beneath.
// Pictures only: no letter and no number is drawn into it.
Item {
    id: root

    property int rows: 4
    property int columns: 4
    property var tiles: []
    property var found: []
    property bool showJewels: true

    readonly property real jewelRow: showJewels ? Theme.space4 + Theme.space2 : 0
    readonly property real side: Math.min(width, height - jewelRow)
    readonly property real cell: side / Math.max(rows, columns, 1)

    implicitWidth: Theme.thumbInline
    implicitHeight: Theme.thumbInline + jewelRow

    Rectangle {
        id: tray
        width: root.side
        height: root.side
        x: (root.width - width) / 2
        color: Theme.tray
        border.width: Theme.hair
        border.color: Theme.trayEdge

        Repeater {
            model: root.tiles

            Rectangle {
                required property int index
                required property string modelData
                readonly property real inset: Math.max(1, root.cell * 0.1)
                x: (index % root.columns) * root.cell + inset
                y: Math.floor(index / root.columns) * root.cell + inset
                width: root.cell - 2 * inset
                height: width
                color: modelData === "" ? Theme.jewelDead : Theme.die
            }
        }
    }

    Row {
        visible: root.showJewels
        anchors.top: tray.bottom
        anchors.topMargin: Theme.space1
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 2

        Repeater {
            model: root.found

            Jewel {
                required property int index
                required property var modelData
                width: height
                height: Theme.space4
                tier: modelData.tier
                colorIndex: index
            }
        }
    }
}
