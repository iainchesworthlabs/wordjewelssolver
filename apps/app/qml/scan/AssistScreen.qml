// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// assist.playing (docs/design/game.md, "Playing"): the confirmed board in the
// leading two-thirds and the panel in the trailing third, the same split rule as
// Solo Play (SoloLayout, board in the top two-thirds of a tall window). The app
// is not the game: there is no scoreboard against the other players, no timer,
// no word count, and no "New board" that a camera noticed.
//
// It is the same room as Solo Play's: the board is the kit's tray with its gold
// rim and cream dice (AssistBoard), the panel is Solo Play's panel (the `panel`
// colour, a titled Card for the words found with an empty state of its own) and
// its play actions are the kit's ActionBar, Clear and Done, on the one height
// ladder, 16 dp from the window's edge, and each says what it does in words (the
// old row was three mismatched buttons, one of them an unlabelled picture, 8 dp
// from the edge, and had no way to hand in a word made by taps).
//
// The panel holds the words found (as the same jewels and stars as Solo Play),
// Clear and Done, the one help (the start tile), and the ways out of the round:
// the earlier boards and New board. The camera is not here: a new round is a new
// scan, and this screen's New board asks for one.
FocusScope {
    id: root

    required property AssistModel assist

    // Back: to the check screen the person came from (the board is recorded).
    signal closed
    // A new round: the scan screen, ready, from the start.
    signal newBoardRequested

    readonly property real barOffset: bar.height
    readonly property bool wide: rule.wide
    // The interface's direction, set by the shell: the split mirrors, the replica
    // of the board does not. For the RTL check in the stub shell.
    property bool rightToLeft: Theme.rightToLeft
    readonly property real panelX: panel.x
    readonly property real boardX: board.x
    readonly property bool mirrored: LayoutMirroring.enabled

    LayoutMirroring.enabled: root.rightToLeft
    LayoutMirroring.childrenInherit: true

    activeFocusOnTab: true

    SoloLayout {
        id: rule
        width: root.width
        height: root.height - root.barOffset
        boardSize: Math.max(root.assist.rows, 3)
        // The replica stays the family's board, left to right as it was confirmed;
        // the split around it follows the window's direction.
        rightToLeft: root.mirrored
    }

    readonly property rect boardArea: Qt.rect(rule.boardArea.x, rule.boardArea.y + root.barOffset,
                                              rule.boardArea.width, rule.boardArea.height)
    readonly property rect panelArea: Qt.rect(rule.panelArea.x, rule.panelArea.y + root.barOffset,
                                              rule.panelArea.width, rule.panelArea.height)

    // The board is laid out by the same rule as Solo Play's; the touch geometry
    // is in the board's own pixels, which is what PathTracer takes.
    function relayout() {
        if (root.assist.rows > 0) {
            root.assist.setGeometry(rule.tile, rule.gap);
        }
    }

    Component.onCompleted: root.relayout()
    Component.onDestruction: root.assist.leave()

    Connections {
        target: rule

        function onChanged() {
            root.relayout();
        }
    }

    // The visible way back, Esc and the system back: the history closes first if
    // it is open, then the board is saved to the store and the person goes where
    // they came from.
    function back() {
        if (history.open) {
            history.open = false;
            return;
        }
        root.assist.leave();
        root.closed();
    }

    function newBoard() {
        root.assist.leave();
        root.newBoardRequested();
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.page
    }

    // --- the bar ------------------------------------------------------------------
    AppBar {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        showBack: true
        showWordmark: false
        showParent: false
        title: qsTr("Find words")
        onBack: root.back()
    }

    // --- the replica, leading two-thirds --------------------------------------------
    Rectangle {
        x: root.boardArea.x
        y: root.boardArea.y
        width: root.boardArea.width
        height: root.boardArea.height
        color: Theme.boardArea
    }

    AssistBoard {
        id: board
        x: root.boardArea.x + rule.boardRect.x
        y: root.boardArea.y + rule.boardRect.y
        width: rule.boardRect.width
        height: rule.boardRect.height
        tile: rule.tile
        gap: rule.gap
        inset: rule.inset
        assist: root.assist
        // The replica is the board the family confirmed, read left to right as
        // the real one was: it does not mirror.
        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true
    }

    // --- the panel, trailing third ---------------------------------------------------
    Item {
        id: panel
        x: root.panelArea.x
        y: root.panelArea.y
        width: root.panelArea.width
        height: root.panelArea.height

        Rectangle {
            anchors.fill: parent
            color: Theme.panel
        }
        // The panel's edge toward the board: its leading side beside the board,
        // its top under it.
        Rectangle {
            visible: root.wide
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Theme.hair
            color: Theme.line
        }
        Rectangle {
            visible: !root.wide
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.hair
            color: Theme.line
        }

        // The panel scrolls when its parts need more room than the window gives
        // (150% text on the smallest window): the card gives up room first, down
        // to a title and one row, and only then does the column scroll.
        Flickable {
            id: panelScroll
            anchors.fill: parent
            contentWidth: width
            contentHeight: Math.max(height, column.leastHeight + 2 * Theme.space4)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height

            // The column is as wide as a phone's panel at most: on a 2400 px window
            // the buttons would otherwise stretch across a third of the screen.
            ColumnLayout {
                id: column
                x: (panelScroll.width - width) / 2
                y: Theme.space4
                width: Math.min(panelScroll.width - 2 * Theme.space4, Theme.modalMax)
                height: panelScroll.contentHeight - 2 * Theme.space4
                spacing: Theme.space3

                // What the column cannot be shorter than.
                readonly property real leastHeight: column.implicitHeight - tray.Layout.preferredHeight
                                                    + tray.Layout.minimumHeight

                // Words I found: a titled card, and an empty state that says what the
                // card is for until the first word lands in it.
                Card {
                    id: tray
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // A title and one row at the least; it likes to be a good deal more.
                    Layout.minimumHeight: Theme.targetPrimary + Theme.rowHeight
                    Layout.preferredHeight: Theme.pictureMax / 2
                    title: ScanWording.wordsFound
                    empty: root.assist.found.length === 0
                    emptyTitle: ScanWording.noWordsYet
                    emptyText: ScanWording.noWordsHint
                    clip: true

                    ListView {
                        id: words
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.assist.found
                        spacing: Theme.space1
                        boundsBehavior: Flickable.StopAtBounds
                        onCountChanged: positionViewAtEnd()

                        Accessible.role: Accessible.List
                        Accessible.name: ScanWording.wordsFound

                        // A jewel drops in; with reduced motion it appears.
                        add: Transition {
                            enabled: !Theme.reducedMotion
                            NumberAnimation {
                                property: "y"
                                from: -Theme.space8
                                duration: Theme.turn
                                easing.type: Easing.OutBounce
                            }
                            NumberAnimation {
                                property: "opacity"
                                from: 0
                                to: 1
                                duration: Theme.base
                            }
                        }

                        // A row is 48 dp, a stop for Tab and a press speaks the word:
                        // a tap and the Return key do the same.
                        delegate: Item {
                            id: row
                            required property int index
                            required property var modelData

                            width: words.width
                            height: Theme.rowHeight
                            activeFocusOnTab: true

                            Accessible.role: Accessible.ListItem
                            Accessible.name: ScanWording.foundName(row.modelData.word)
                            Accessible.focusable: true
                            Accessible.onPressAction: Feedback.cue("speak", row.modelData.word)

                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                                        || event.key === Qt.Key_Space) {
                                    Feedback.cue("speak", row.modelData.word);
                                    event.accepted = true;
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusSmall
                                color: rowMouse.pressed ? Theme.pressWash
                                     : (rowMouse.containsMouse ? Theme.hoverWash : "transparent")
                            }

                            Jewel {
                                id: jewel
                                anchors.verticalCenter: parent.verticalCenter
                                x: Theme.space2
                                tier: row.modelData.tier
                                colorIndex: row.index
                            }

                            Label {
                                anchors.left: jewel.right
                                anchors.leftMargin: Theme.space3
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.space2
                                anchors.verticalCenter: parent.verticalCenter
                                decorative: true
                                role: "word"
                                text: row.modelData.word
                                elide: Text.ElideRight
                            }

                            FocusRing {
                                visible: row.activeFocus
                                inner: Theme.radiusSmall
                            }

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: Feedback.cue("speak", row.modelData.word)
                            }
                        }
                    }
                }

                // The two plays that end a word: Clear puts a path down, Done hands
                // one in. A traced word hands itself in when the finger lifts, so Done
                // is for a word made by taps (or the keyboard and a screen reader), as
                // in Solo Play. Both wait for a path.
                ActionBar {
                    id: playActions
                    Layout.fillWidth: true
                    tier: "child"
                    primaryLabel: ScanWording.done
                    primaryIcon: "done"
                    primaryEnabled: root.assist.path.length > 0
                    secondaryLabel: ScanWording.clear
                    secondaryEnabled: root.assist.path.length > 0
                    onPrimaryClicked: root.assist.done()
                    onSecondaryClicked: root.assist.clearPath()
                }

                // The one help: the start tile, asked for and never offered (absent
                // when the parent has turned help off). A button of its own, so that
                // its label wraps at a large text size instead of being cut.
                AppButton {
                    id: showStart
                    visible: root.assist.helpAllowed
                    Layout.fillWidth: true
                    size: "child"
                    variant: "secondary"
                    wrap: true
                    iconName: "hint"
                    label: ScanWording.showStart
                    accessibleName: ScanWording.showStart
                    onClicked: root.assist.askHelp()
                }

                // The ways out of this round, quieter than the play: the earlier
                // boards (the same history as Solo Play's, opened on purpose and never
                // offered after a board) and a new board, which is a new scan. Each
                // has a word: icon-only is for Back and Close.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2

                    AppButton {
                        id: historyButton
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        size: "min"
                        variant: "ghost"
                        wrap: true
                        iconName: "history"
                        label: ScanWording.earlierBoards
                        accessibleName: ScanWording.earlierBoards
                        onClicked: history.open = true
                    }
                    AppButton {
                        id: newBoardButton
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        size: "min"
                        variant: "ghost"
                        wrap: true
                        iconName: "again"
                        label: ScanWording.newBoard
                        accessibleName: ScanWording.newBoard
                        onClicked: root.newBoard()
                    }
                }
            }
        }
    }

    // Earlier boards, over the screen (the bar dims too): newest first, the
    // store's last twenty.
    AssistHistory {
        id: history
        anchors.fill: parent
        assist: root.assist
    }
}
