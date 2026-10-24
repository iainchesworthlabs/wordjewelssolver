// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// The child's own collection, reached from home. It is where the weekly stars,
// the words and the earlier boards live, so the play panel can be about the
// board in front of the child. Nothing here is a rank against anyone else: the
// records are this device's own, and there is nothing to lose.
//
// Every block is a kit Card (a heading and, when there is nothing in it, an
// empty state); the best boards are a grid of three columns, not a line padded
// with spaces. Gold is the reward and appears only where one is shown: the big
// jewel and the points beside it, and each found word's jewel.
FocusScope {
    id: root

    required property var controller

    signal closed
    signal playRequested

    LayoutMirroring.enabled: Theme.rightToLeft
    LayoutMirroring.childrenInherit: true

    readonly property var week: controller.dashboard.week
    readonly property var vault: controller.dashboard.vault
    readonly property var recent: controller.recentBoards
    readonly property var words: vault.length > 40 ? vault.slice(vault.length - 40) : vault
    readonly property bool empty: root.controller.lifetimeWords === 0
                                        && root.controller.lifetimeStars === 0
    // The child's own best board at each size, from the boards still kept. This
    // is a personal wall, not a rank against another child: no name, no order
    // beyond the size, and no one else's score exists to compare with.
    readonly property var bestSizes: [3, 4, 5, 6]
    readonly property bool anyBest: root.bestSizes.some(size => root.bestStars(size) > 0)
    // The cells of the best-boards grid, three to a row: the size, the stars and
    // the words of that board. A size with no board kept has no row.
    readonly property var bestCells: {
        const cells = [];
        for (const size of root.bestSizes) {
            const stars = root.bestStars(size);
            if (stars > 0) {
                cells.push(JewelsWording.sizeLabel(size), JewelsWording.stars(stars),
                           JewelsWording.words(root.bestWords(size)));
            }
        }
        return cells;
    }

    function bestStars(size) {
        let best = 0;
        for (let i = 0; i < root.recent.length; ++i) {
            if (root.recent[i].columns === size) {
                best = Math.max(best, root.recent[i].stars);
            }
        }
        return best;
    }
    function bestWords(size) {
        let best = 0;
        for (let i = 0; i < root.recent.length; ++i) {
            if (root.recent[i].columns === size && root.recent[i].stars === root.bestStars(size)) {
                best = Math.max(best, root.recent[i].found);
            }
        }
        return best;
    }

    // A found word: its jewel and the word, pressed to hear it. A sticker on the
    // card (the page's own paper and its edge), reachable by keyboard.
    component WordChip: Rectangle {
        id: chip

        required property int index
        required property var modelData
        readonly property bool showFocus: activeFocus && !pointerFocus
        property bool pointerFocus: false

        implicitWidth: chipRow.implicitWidth + 2 * Theme.space3
        implicitHeight: Theme.targetMin
        radius: height / 2
        color: chipTap.pressed ? Theme.pressWash : (chipHover.hovered ? Theme.hoverWash : Theme.panel)
        border.width: Theme.hair
        border.color: Theme.line
        activeFocusOnTab: true

        Accessible.role: Accessible.Button
        Accessible.name: chip.modelData.word
        Accessible.focusable: true
        Accessible.onPressAction: Feedback.cue("speak", chip.modelData.word)

        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: Theme.space2

            Jewel {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.jewelSm
                height: width
                tier: chip.modelData.word.length <= 3 ? "spark"
                      : (chip.modelData.word.length === 4 ? "power" : "mega")
                colorIndex: chip.index
                Accessible.ignored: true
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                role: "word"
                text: chip.modelData.word
                decorative: true
            }
        }

        FocusRing {
            visible: chip.showFocus
            inner: chip.radius
        }
        HoverHandler {
            id: chipHover
        }
        TapHandler {
            id: chipTap
            onPressedChanged: {
                if (chipTap.pressed) {
                    chip.pointerFocus = true;
                }
            }
            onTapped: Feedback.cue("speak", chip.modelData.word)
        }
        Keys.onPressed: event => {
            chip.pointerFocus = false;
            const acts = event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
            if (acts) {
                Feedback.cue("speak", chip.modelData.word);
            }
            event.accepted = acts;
        }
        onActiveFocusChanged: {
            if (!chip.activeFocus) {
                chip.pointerFocus = false;
            }
        }
    }

    Component.onCompleted: {
        root.controller.refresh();
        root.forceActiveFocus();
    }

    Keys.onEscapePressed: event => {
        root.closed();
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.page
    }

    AppBar {
        id: appBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        showBack: true
        showWordmark: false
        showParent: false
        title: JewelsWording.title
        onBack: root.closed()
    }

    Flickable {
        id: page
        anchors.top: appBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * Theme.space6
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            width: Math.min(page.width - 2 * Theme.space6, Theme.contentWidth)
            x: (page.width - width) / 2
            y: Theme.space6
            spacing: Theme.space6

            // --- the big picture ------------------------------------------------
            Card {
                Layout.fillWidth: true
                radius: Theme.radiusLarge
                padding: Theme.space6
                Accessible.name: JewelsWording.jewels(root.controller.lifetimeStars)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space6

                    Jewel {
                        Layout.preferredWidth: Theme.jewelXl
                        Layout.preferredHeight: Theme.jewelXl
                        Layout.alignment: Qt.AlignVCenter
                        tier: "mega"
                        color: Theme.gold
                        Accessible.ignored: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space1
                        Label {
                            Layout.fillWidth: true
                            role: "title"
                            text: JewelsWording.jewels(root.controller.lifetimeStars)
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "label"
                            text: JewelsWording.points(root.controller.lifetimePoints)
                            color: Theme.goldInk
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "body"
                            learning: true
                            text: JewelsWording.words(root.controller.lifetimeWords)
                                   + (root.controller.longestWord.length > 0
                                      ? "  ·  " + JewelsWording.longest(root.controller.longestWord) : "")
                            color: Theme.inkSecondary
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // --- my best boards ---------------------------------------------------
            Card {
                Layout.fillWidth: true
                visible: root.anyBest
                title: JewelsWording.bestTitle

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: Theme.space6
                    rowSpacing: Theme.space2

                    Repeater {
                        model: root.bestCells

                        Label {
                            required property int index
                            required property string modelData
                            // The size leads its row; the stars and the words
                            // follow in the voice of the page.
                            role: index % 3 === 0 ? "label" : "body"
                            lineRatio: Theme.lineBody
                            learning: index % 3 !== 0
                            text: modelData
                            Layout.fillWidth: index % 3 === 2
                            Layout.alignment: Qt.AlignBaseline
                        }
                    }
                }
            }

            // --- this week -------------------------------------------------------
            Card {
                Layout.fillWidth: true
                visible: root.week.played
                title: JewelsWording.weekTitle

                Label {
                    Layout.fillWidth: true
                    role: "body"
                    learning: true
                    text: JewelsWording.weekLine(root.week.stars, root.week.newWords)
                    color: Theme.inkSecondary
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.week.longestWord.length > 0
                    role: "body"
                    learning: true
                    text: JewelsWording.longest(root.week.longestWord)
                    color: Theme.inkSecondary
                    wrapMode: Text.WordWrap
                }
            }

            // --- the words -------------------------------------------------------
            Card {
                Layout.fillWidth: true
                visible: !root.empty
                title: JewelsWording.shelfTitle
                empty: root.words.length === 0
                emptyText: JewelsWording.shelfEmpty

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space2

                    Repeater {
                        model: root.words

                        WordChip {
                        }
                    }
                }
            }

            // --- earlier boards ---------------------------------------------------
            Card {
                Layout.fillWidth: true
                visible: !root.empty
                title: JewelsWording.boardsTitle
                empty: root.recent.length === 0
                emptyText: JewelsWording.boardsEmpty

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space4

                    Repeater {
                        model: root.recent.slice(0, 8)

                        Column {
                            id: boardCard
                            required property var modelData
                            width: Theme.thumbSheetCell
                            spacing: Theme.space1

                            BoardThumb {
                                width: parent.width
                                height: width
                                rows: boardCard.modelData.rows
                                columns: boardCard.modelData.columns
                                tiles: boardCard.modelData.tiles
                                found: boardCard.modelData.words
                            }
                            Label {
                                width: parent.width
                                role: "caption"
                                text: JewelsWording.boardLine(boardCard.modelData.stars,
                                                              boardCard.modelData.found)
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            // --- a first visit ------------------------------------------------------
            Card {
                Layout.fillWidth: true
                visible: root.empty
                empty: true
                emptyTitle: JewelsWording.emptyTitle
                emptyText: JewelsWording.emptyBody
            }
            AppButton {
                Layout.alignment: Qt.AlignHCenter
                visible: root.empty
                size: "primary"
                variant: "primary"
                label: JewelsWording.play
                onClicked: root.playRequested()
            }
        }
    }
}
