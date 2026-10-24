// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// The panel beside the flat board: the words found, and under them one zone of
// actions - the word in hand, the hint, Finish board, and Clear and Done - that
// follow one height ladder (64 dp, with Done at 96) and never clip, wrapping their
// labels rather than cutting them. Every card has a title and an empty state; no
// card is a blank rectangle.
//
// The panel is one full-height column in a wide window and two columns in a tall
// one (the words beside the actions: a bottom third is too short for both stacked,
// which once left the words card a 4 px sliver). Nothing moves when a path starts:
// the line for the word in hand has its height reserved, and so does the hint's
// words, which share it. If a window and a text size leave less room than the
// actions need, the panel scrolls instead of hiding one under the edge.
Item {
    id: root

    LayoutMirroring.enabled: Theme.rightToLeft
    LayoutMirroring.childrenInherit: true

    required property var solo
    required property bool wide
    // The line over the actions: the word being made, or what just happened.
    property string statusText: ""
    property string statusDetail: ""
    property bool statusWord: false
    signal finishRequested

    readonly property bool helpShown: solo.helpAllowed
    readonly property bool pending: solo.path.length > 0
    readonly property bool mirrored: Theme.rightToLeft
    readonly property int hintLevel: solo.hintLevel
    // At the parent's middle level ("start") the first press lights the tile
    // and the next press hides it; only the fullest level offers the second
    // rung.
    readonly property string hintLabel: hintLevel === 0 ? SoloWording.showStart
                                      : (hintLevel === 1 && solo.helpLevel === "two")
                                        ? SoloWording.twoLetters
                                        : SoloWording.hideHint
    readonly property string hintHelp: hintLevel === 0 ? ""
                                     : hintLevel === 1 ? SoloWording.startHintShown
                                                       : SoloWording.twoLettersShown
    // The answer to the last question: a find made from the lit start puts the
    // rings away, and this says whether the same letters still begin more of the
    // board's words.
    readonly property string hintAnswer: !solo.hintResolved ? ""
                                         : solo.hintRemaining > 0
                                           ? SoloWording.moreWordsWithStart(solo.hintRemaining)
                                           : SoloWording.noMoreWordsWithStart
    readonly property string hintLine: hintAnswer.length > 0 ? hintAnswer : hintHelp

    // The clock the parent set for the board, shown at the top of the words.
    readonly property bool clockShown: solo.clockMode !== "off"
    readonly property bool actionsShown: solo.state === "playing" || solo.state === "word-found"
    readonly property string actionLine: actionsShown && solo.path.length > 0
                                         ? qsTr("Tap the last die again, or press Done")
                                         : ""
    // What the line over the actions says: the word in hand (or what just
    // happened) first, the hint's words when there is nothing in hand.
    readonly property bool inHand: statusText.length > 0 || solo.path.length > 0
    readonly property bool hintOnly: !inHand && helpShown && hintLine.length > 0

    readonly property real margin: Theme.space4
    readonly property real gap: Theme.space3
    // The tall layout's second column.
    readonly property real zoneWidth: Math.min(width * 0.45, Theme.modalMax - Theme.space16)
    // The line's reserved height: a word and its small line, or two lines of the hint.
    readonly property real stripHeight: Math.max(
        Theme.wordSize * Theme.lineWord + Theme.captionSize * Theme.lineCaption + Theme.space1,
        2 * Theme.bodySize * Theme.lineBody + 2 * Theme.space2)

    function currentWord() {
        var letters = [];
        for (var i = 0; i < solo.path.length; ++i) {
            const tile = solo.tiles[solo.path[i]];
            letters.push(tile === "QU" ? "Qu" : tile);
        }
        return letters.join(" · ");
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.panel
    }
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.hair
        color: Theme.line
    }

    Flickable {
        id: viewport
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: Math.max(layout.implicitHeight + 2 * root.margin, height)

        GridLayout {
            id: layout
            x: root.margin
            y: root.margin
            width: viewport.width - 2 * root.margin
            // The room the viewport gives it, or what it needs if that is more.
            height: Math.max(implicitHeight, viewport.height - 2 * root.margin)
            columns: root.wide ? 1 : 2
            rowSpacing: root.gap
            columnSpacing: root.gap

            // --- words I found -------------------------------------------------
            Rectangle {
                id: wordsCard
                objectName: "wordsCard"
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Never a sliver: the heading, its counts and two whole rows.
                Layout.minimumHeight: wordsColumn.implicitHeight + 2 * Theme.space3
                Layout.preferredHeight: wordsColumn.implicitHeight + 2 * Theme.space3
                radius: Theme.radius
                color: Theme.card
                border.width: Theme.hair
                border.color: Theme.line
                clip: true

                Accessible.role: Accessible.Grouping
                Accessible.name: SoloWording.wordsFound

                ColumnLayout {
                    id: wordsColumn
                    anchors.fill: parent
                    anchors.margins: Theme.space3
                    spacing: Theme.space1

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2

                        Label {
                            Layout.fillWidth: true
                            role: "heading"
                            text: SoloWording.wordsFound
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            visible: root.clockShown
                            role: "heading"
                            font.family: Theme.monoFont
                            text: root.solo.clockText
                            color: root.solo.clockMode === "down" && root.solo.clockSeconds <= 30
                                   ? Theme.goldInk : Theme.ink
                            Accessible.role: Accessible.StaticText
                            Accessible.name: SoloWording.clockName(root.solo.clockMode, root.solo.clockText)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space3

                        Label {
                            role: "caption"
                            text: root.solo.found.length === 1 ? qsTr("1 word")
                                                               : qsTr("%1 words").arg(root.solo.found.length)
                        }
                        Label {
                            role: "caption"
                            color: Theme.goldInk
                            text: qsTr("%1 points").arg(root.solo.boardPoints)
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.space1
                        Layout.preferredHeight: Theme.hair
                        color: Theme.line
                    }

                    // The words, in whole rows: the list is a whole number of rows
                    // tall, so a row is never cut under the heading, and a thin
                    // handle says there are more when there are.
                    Item {
                        id: listArea
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 2 * Theme.rowHeight
                        Layout.preferredHeight: 2 * Theme.rowHeight

                        ListView {
                            id: words
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.rightMargin: Theme.space3
                            height: Math.floor(listArea.height / Theme.rowHeight) * Theme.rowHeight
                            clip: true
                            model: root.solo.found
                            boundsBehavior: Flickable.StopAtBounds
                            snapMode: ListView.SnapToItem

                            Accessible.role: Accessible.List
                            Accessible.name: SoloWording.wordsFound

                            onCountChanged: Qt.callLater(() => words.positionViewAtEnd())
                            Component.onCompleted: Qt.callLater(() => words.positionViewAtEnd())

                            delegate: Item {
                                id: row
                                required property int index
                                required property var modelData
                                width: ListView.view.width
                                height: Theme.rowHeight

                                activeFocusOnTab: true
                                Accessible.role: Accessible.ListItem
                                Accessible.name: SoloWording.found(row.modelData.word)
                                Accessible.focusable: true
                                Accessible.onPressAction: row.speak()

                                function speak() {
                                    Feedback.cue("speak", row.modelData.word);
                                }

                                Keys.onPressed: event => {
                                    const acts = event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                                 || event.key === Qt.Key_Enter;
                                    if (acts) {
                                        row.speak();
                                    }
                                    event.accepted = acts;
                                }
                                // A row the keyboard reaches is scrolled into view, whole.
                                onActiveFocusChanged: {
                                    if (row.activeFocus) {
                                        ListView.view.positionViewAtIndex(row.index, ListView.Contain);
                                    }
                                }

                                Jewel {
                                    id: jewel
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.space1
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Theme.jewelMd
                                    height: Theme.jewelMd
                                    tier: row.modelData.tier
                                    colorIndex: row.index
                                    Accessible.ignored: true
                                }
                                Label {
                                    anchors.left: jewel.right
                                    anchors.leftMargin: Theme.space2
                                    anchors.right: points.left
                                    anchors.rightMargin: Theme.space2
                                    anchors.verticalCenter: parent.verticalCenter
                                    role: "word"
                                    decorative: true
                                    text: row.modelData.word
                                    elide: Text.ElideRight
                                }
                                Label {
                                    id: points
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.space1
                                    anchors.verticalCenter: parent.verticalCenter
                                    role: "mono"
                                    decorative: true
                                    font.capitalization: Font.MixedCase
                                    color: Theme.goldInk
                                    text: qsTr("+%1").arg(row.modelData.points)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: row.speak()
                                }
                                Rectangle {
                                    visible: row.activeFocus
                                    anchors.fill: parent
                                    radius: Theme.radiusSmall
                                    color: "transparent"
                                    border.width: Theme.focusWidth
                                    border.color: Theme.focusRing
                                    Accessible.ignored: true
                                }
                            }
                        }

                        ScrollCue {
                            view: words
                            anchors.top: words.top
                            anchors.bottom: words.bottom
                            anchors.right: parent.right
                        }

                        // Nothing found yet: where the words will go, in the list's
                        // own place (so the word in hand never lands on it).
                        Column {
                            visible: root.solo.found.length === 0
                            anchors.centerIn: parent
                            spacing: Theme.space2
                            Jewel {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Theme.jewelMega
                                height: Theme.jewelMega
                                tier: "power"
                                color: Theme.jewelDead
                                Accessible.ignored: true
                            }
                            Label {
                                role: "caption"
                                learning: true
                                text: qsTr("Your words will land here.")
                            }
                        }
                    }
                }
            }

            // --- the actions: the word in hand, the hint, Finish, Clear and Done ----
            ColumnLayout {
                id: zone
                Layout.fillWidth: root.wide
                Layout.preferredWidth: root.wide ? -1 : root.zoneWidth
                Layout.alignment: Qt.AlignTop
                spacing: root.gap

                // The word in hand (or what just happened), or the hint's words:
                // one line of a fixed height, so nothing under it moves.
                Item {
                    id: strip
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(root.stripHeight, handColumn.implicitHeight,
                                                     hintText.implicitHeight + 2 * Theme.space2)

                    Rectangle {
                        visible: root.hintOnly
                        anchors.fill: parent
                        radius: Theme.radius
                        color: Theme.hintCard
                        border.width: Theme.hair
                        border.color: Theme.gold
                    }

                    Column {
                        id: handColumn
                        visible: root.inHand
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0

                        Label {
                            width: parent.width
                            role: "word"
                            font.weight: Font.DemiBold
                            text: root.statusText.length > 0 ? root.statusText : root.currentWord()
                            color: root.statusText.length > 0 && !root.statusWord
                                   ? Theme.inkSecondary : Theme.ink
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
                        }
                        Label {
                            width: parent.width
                            visible: root.statusDetail.length > 0 || root.solo.path.length > 0
                            role: "caption"
                            learning: true
                            text: root.statusDetail.length > 0 ? root.statusDetail : root.actionLine
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }

                    Label {
                        id: hintText
                        visible: root.hintOnly
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: Theme.space3
                        anchors.verticalCenter: parent.verticalCenter
                        role: "body"
                        learning: true
                        color: Theme.hintCardInk
                        text: root.hintLine
                        wrapMode: Text.WordWrap
                    }
                }

                // A start for a word, a second letter, or the hint put away.
                AppButton {
                    visible: root.helpShown
                    Layout.fillWidth: true
                    size: "child"
                    variant: "secondary"
                    wrap: true
                    iconName: "hint"
                    label: root.hintLabel
                    accessibleName: root.hintLabel
                    onClicked: root.solo.askHelp()
                }

                // The standard board holds tens to hundreds of dictionary words, so
                // its "all words found" ending is not a real ending for a child.
                // This is: the board is safely saved, and the summary shows what
                // was found.
                AppButton {
                    visible: root.actionsShown
                    Layout.fillWidth: true
                    size: "child"
                    variant: "secondary"
                    wrap: true
                    label: SoloWording.finishBoard
                    accessibleName: SoloWording.finishBoard
                    onClicked: root.finishRequested()
                }

                // Clear clears a path; Done hands one in. A traced word hands itself
                // in when the finger lifts, so Done is for a word made by taps (or
                // the keyboard and a screen reader).
                ActionBar {
                    visible: root.actionsShown
                    Layout.fillWidth: true
                    tier: "child"
                    secondaryLabel: qsTr("Clear")
                    secondaryEnabled: root.solo.path.length > 0 || root.statusText.length > 0
                    primaryLabel: qsTr("Done")
                    primaryIcon: "done"
                    primaryEnabled: root.solo.path.length > 0
                    onSecondaryClicked: root.solo.clearPath()
                    onPrimaryClicked: root.solo.done()
                }
            }
        }
    }
}
