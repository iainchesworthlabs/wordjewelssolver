// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// The Family Assistant's earlier boards, newest first, over `assist.playing`
// (docs/design/game.md, "The panel": the same history as Solo Play's). It is the
// child's to open on purpose; Back, the close button, Escape and a press on the
// scrim all close it, and nothing in it can be lost or changed.
//
// It is the kit's Sheet, which the old one was not: a heading ("Earlier
// boards"), a 20 px radius, a scrim over the whole screen (the bar dims too: give
// it the screen's root to fill), a focus trap and the focus handed back to the
// button that opened it. Each board has a caption (its size and how many words
// were found) and an empty history says so in words, with a still jewel, instead
// of an unlabelled tray.
//
// The boards are the store's, as `AssistModel::history` gives them; a board in
// play is not in the list until the round is left. The picture is BoardThumb, the
// same one Solo Play's history uses; every sentence is ScanWording's.
Sheet {
    id: root

    required property AssistModel assist

    readonly property bool hasBoards: root.assist.history.length > 0

    title: ScanWording.earlierBoards
    closeLabel: ScanWording.closePicker
    maxWidth: Theme.sheetMaxWidth
    onClose: root.open = false

    // The boards, in as many columns as fit, the leftover width shared out.
    GridView {
        id: grid
        visible: root.hasBoards
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: Math.min(grid.contentHeight, Theme.sheetMaxHeight)
        Layout.minimumHeight: Theme.thumbSheetCell
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        cellWidth: grid.width / Math.max(1, Math.floor(grid.width / Theme.thumbSheetCell))
        cellHeight: Theme.thumbSheetCell + Theme.space6 + Theme.space6
        model: root.assist.history

        Accessible.role: Accessible.List
        Accessible.name: ScanWording.earlierBoards

        delegate: Item {
            id: thumbCell
            required property var modelData

            width: grid.cellWidth
            height: grid.cellHeight

            Accessible.role: Accessible.ListItem
            Accessible.name: ScanWording.historyCaption(thumbCell.modelData.rows,
                                                        thumbCell.modelData.found.length)

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.space1

                BoardThumb {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Theme.thumbSheet
                    Layout.preferredHeight: Theme.thumbSheet + jewelRow
                    rows: thumbCell.modelData.rows
                    columns: thumbCell.modelData.columns
                    tiles: thumbCell.modelData.tiles
                    found: thumbCell.modelData.found
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    decorative: true
                    role: "caption"
                    text: ScanWording.historyCaption(thumbCell.modelData.rows,
                                                     thumbCell.modelData.found.length)
                }
            }
        }
    }

    // An empty history says what it is for.
    ColumnLayout {
        visible: !root.hasBoards
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
        spacing: Theme.space2

        Jewel {
            Layout.alignment: Qt.AlignHCenter
            tier: "power"
            color: Theme.jewelDead
            Accessible.ignored: true
        }
        Label {
            Layout.fillWidth: true
            role: "label"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: ScanWording.historyEmpty
        }
        Label {
            Layout.fillWidth: true
            role: "caption"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: ScanWording.historyEmptyHint
        }
    }
}
