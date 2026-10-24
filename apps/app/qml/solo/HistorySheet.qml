// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// Earlier boards, newest first, as a sheet over whatever screen opened it - Home's
// "My boards" opens it over Home, so the child is not taken into the box to look
// at a list. It is the child's to open on purpose and is never offered after a
// board. Back, Escape, the close button and a press on the scrim all close it;
// nothing in it can be lost or changed. Give it the whole window to dim: the
// scrim then covers the app bar too, as every sheet's does.
//
// Each board is its picture (the tray, the dice, a jewel for each word) and a
// line saying its size and how many words were found. With no boards it says
// so, in words, rather than showing a picture of nothing.
Sheet {
    id: root

    required property var solo

    readonly property bool empty: solo.history.length === 0

    title: SoloWording.earlierBoards
    closeLabel: SoloWording.close
    maxWidth: Theme.sheetMaxWidth
    maxHeight: Theme.sheetMaxHeight

    onClose: root.open = false

    // --- some boards ---------------------------------------------------------
    Item {
        visible: !root.empty
        Layout.fillWidth: true
        Layout.fillHeight: true
        // The sheet is as tall as its boards want, up to its limit.
        Layout.preferredHeight: Math.min(grid.contentHeight, Theme.sheetMaxHeight - Theme.space16)
        Layout.minimumHeight: grid.cellHeight

        GridView {
            id: grid
            anchors.fill: parent
            anchors.rightMargin: Theme.space3
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // The cells share the row out, so a sheet wider than a whole number of
            // cells does not leave the boards leaning to one side.
            cellWidth: width / Math.max(1, Math.floor(width / Theme.thumbSheetCell))
            cellHeight: Theme.thumbSheet + Theme.space6 + Theme.space1
                        + 2 * Theme.captionSize * Theme.lineCaption + Theme.space3
            model: root.solo.history

            Accessible.role: Accessible.List
            Accessible.name: SoloWording.earlierBoards

            delegate: Item {
                id: entry
                required property int index
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight

                Accessible.role: Accessible.ListItem
                Accessible.name: SoloWording.boardCaption(entry.modelData.rows, entry.modelData.found.length)

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: Theme.space1
                    width: Theme.thumbSheet + Theme.space4
                    spacing: Theme.space1

                    BoardThumb {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.thumbSheet
                        height: Theme.thumbSheet + jewelRow
                        rows: entry.modelData.rows
                        columns: entry.modelData.columns
                        tiles: entry.modelData.tiles
                        found: entry.modelData.found
                        Accessible.ignored: true
                    }
                    Label {
                        width: parent.width
                        role: "caption"
                        decorative: true
                        text: SoloWording.boardCaption(entry.modelData.rows, entry.modelData.found.length)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        ScrollCue {
            view: grid
            anchors.top: grid.top
            anchors.bottom: grid.bottom
            anchors.right: parent.right
        }
    }

    // --- no boards ---------------------------------------------------------------
    ColumnLayout {
        visible: root.empty
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Theme.space16 * 2
        spacing: Theme.space2

        Item {
            Layout.fillHeight: true
        }
        BoardThumb {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Theme.thumbSheet
            Layout.preferredHeight: Theme.thumbSheet
            rows: 3
            columns: 3
            tiles: []
            showJewels: false
            Accessible.ignored: true
        }
        Label {
            Layout.fillWidth: true
            role: "label"
            text: SoloWording.noBoardsTitle
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            role: "caption"
            learning: true
            text: SoloWording.noBoardsText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Item {
            Layout.fillHeight: true
        }
    }
}
