// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// What a parent sees (docs/design/dashboard.md, "What the dashboard shows"):
// this week, the sounds, the vault and the best, computed on the device when
// this screen is opened. Observations and never assessments: no time on screen,
// no streak, no comparison, no grade.
//
// The screen is two parts with a boundary between them: the one thing a parent
// came here to do (set up a real board) stands alone at the top, and everything
// about the child's play - the week, the sounds, the vault, the best - is under
// a heading of its own below it, so an action is never read as a record.
Flickable {
    id: root

    required property var controller

    signal scanRequested
    signal playRequested

    // The result of the parent's one retry of a failed save, if they pressed it.
    property bool retryTried: false
    property bool retrySucceeded: false

    // The parent area's direction, from the shell: the rows and their counts
    // follow it (the numbers themselves do not).
    LayoutMirroring.enabled: Theme.rightToLeft
    LayoutMirroring.childrenInherit: true

    readonly property var report: controller.dashboard
    readonly property var week: report.week
    readonly property bool anything: week.played || report.sounds.length > 0 || report.vault.length > 0 || report.best.length > 0

    // A sentence with <i> in it, as a screen reader says it: the words, no tags.
    function plain(text) {
        return text.replace(/<[^>]*>/g, "");
    }

    contentWidth: width
    contentHeight: column.implicitHeight + 2 * Theme.space6
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick

    ColumnLayout {
        id: column
        x: Math.max(Theme.space4, (root.width - width) / 2)
        y: Theme.space6
        width: Math.min(root.width - 2 * Theme.space4, Theme.contentWidth)
        spacing: Theme.space8

        // --- what a parent came here to do -------------------------------------
        // The adult way into the family game. The scanner is a parent's
        // workflow, so it lives behind this gate rather than on the child's
        // home, and it is the first thing on the screen: the action is not
        // one of the records below it. It is the page's primary card (the
        // action colour's heavier edge, as on home), not a gold-edged one: gold
        // is the reward.
        Card {
            Layout.fillWidth: true
            title: ParentWording.welcomeTitle
            border.width: Theme.heavy
            border.color: Theme.accent

            Label {
                Layout.fillWidth: true
                role: "body"
                text: ParentWording.welcomeBody
                color: Theme.inkSecondary
                wrapMode: Text.WordWrap
            }
            // A Flow, so the two actions share a line where there is room
            // and stack where there is not: neither ever leaves the card.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space2
                AppButton {
                    size: "min"
                    quiet: true
                    label: ParentWording.setUpBoard
                    variant: "primary"
                    onClicked: root.scanRequested()
                }
                AppButton {
                    size: "min"
                    quiet: true
                    label: ParentWording.backToPlay
                    onClicked: root.playRequested()
                }
            }
        }

        // A note about the saved progress, for the parent only.
        Rectangle {
            Layout.fillWidth: true
            visible: root.controller.storeNote.length > 0 || root.retryTried
            implicitHeight: noteColumn.implicitHeight + 2 * Theme.space3
            radius: Theme.radius
            color: Theme.panel
            border.width: Theme.heavy
            border.color: Theme.outline

            ColumnLayout {
                id: noteColumn
                anchors.fill: parent
                anchors.margins: Theme.space3
                spacing: Theme.space2

                Label {
                    id: noteText
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    role: "body"
                    text: {
                        switch (root.controller.storeNote) {
                        case "unreadable":
                            return ParentWording.noteUnreadable;
                        case "newer":
                            return ParentWording.noteNewer;
                        case "unsaved":
                            return ParentWording.noteUnsaved;
                        default:
                            return "";
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.retryTried
                    wrapMode: Text.WordWrap
                    role: "caption"
                    color: Theme.ink
                    text: root.retrySucceeded ? ParentWording.saved : ParentWording.saveStillNotSaved
                }
                AppButton {
                    size: "min"
                    quiet: true
                    visible: root.controller.storeNote === "unsaved"
                    label: ParentWording.retry
                    onClicked: {
                        root.retrySucceeded = root.controller.retrySave();
                        root.retryTried = true;
                    }
                }
            }
        }

        // --- how the child's play is going --------------------------------------
        // The boundary between the action above and the records below: a
        // heading of their own, so the week, the sounds, the vault and the
        // best read as what the play has made, never as one more button.
        Eyebrow {
            Layout.fillWidth: true
            visible: root.anything
            accented: true
            text: ParentWording.recordsHeading
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.Heading
        }

        Label {
            Layout.fillWidth: true
            visible: !root.anything
            role: "heading"
            text: ParentWording.nothingYet
            color: Theme.inkSecondary
            wrapMode: Text.WordWrap
        }

        // --- this week --------------------------------------------------------
        // The sentence begins "This week:", so the block has no heading of its
        // own. It is a card like the sounds' rows, so every record shares one
        // visual language.
        Card {
            Layout.fillWidth: true
            visible: root.week.played

            Label {
                Layout.fillWidth: true
                role: "body"
                text: ParentWording.weekSentence(root.week.boardsCleared, root.week.stars, root.week.newWords)
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: root.week.longestWord.length > 0
                role: "body"
                text: ParentWording.longestWord(root.week.longestWord)
                wrapMode: Text.WordWrap
            }
        }

        // --- the sounds ---------------------------------------------------------
        Section {
            Layout.fillWidth: true
            visible: root.report.sounds.length > 0
            title: ParentWording.soundsHeading
            Accessible.role: Accessible.List
            Accessible.name: ParentWording.soundsHeading

            Repeater {
                model: root.report.sounds

                Rectangle {
                    id: row
                    required property var modelData

                    Layout.fillWidth: true
                    implicitHeight: Math.max(Theme.targetMin + 2 * Theme.space2, rowText.implicitHeight + 2 * Theme.space3)
                    radius: Theme.radius
                    color: Theme.card
                    border.width: Theme.hair
                    border.color: Theme.line

                    Accessible.role: Accessible.ListItem
                    Accessible.name: root.plain(row.modelData.boardsMet > 0
                        ? ParentWording.soundSentence(row.modelData.letters, row.modelData.boardsMet, row.modelData.boardsFound)
                        : row.modelData.letters + ": " + ParentWording.notMetYet)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.space3
                        spacing: Theme.space4

                        Label {
                            Layout.preferredWidth: Theme.space12 + Theme.space4
                            role: "child"
                            size: Theme.titleSize
                            text: row.modelData.letters
                            horizontalAlignment: Text.AlignHCenter
                            decorative: true
                        }

                        ColumnLayout {
                            id: rowText
                            Layout.fillWidth: true
                            spacing: Theme.space1

                            Label {
                                Layout.fillWidth: true
                                textFormat: Text.StyledText
                                wrapMode: Text.WordWrap
                                role: "body"
                                decorative: true
                                text: row.modelData.boardsMet > 0
                                    ? ParentWording.soundSentence(row.modelData.letters, row.modelData.boardsMet, row.modelData.boardsFound)
                                    : ParentWording.notMetYet
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: row.modelData.wordsUnassisted > 0
                                wrapMode: Text.WordWrap
                                role: "caption"
                                text: ParentWording.foundWithoutStartTile(row.modelData.wordsUnassisted)
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: row.modelData.oftenFound
                                wrapMode: Text.WordWrap
                                role: "caption"
                                font.weight: Font.Bold
                                color: Theme.ink
                                text: ParentWording.oftenFound
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: row.modelData.fewWords
                                wrapMode: Text.WordWrap
                                role: "caption"
                                text: ParentWording.fewWords
                            }
                        }
                    }
                }
            }
        }

        // --- the vault ------------------------------------------------------------
        Section {
            Layout.fillWidth: true
            visible: root.report.vault.length > 0
            title: ParentWording.vaultHeading

            // Each word is a sticker on the page: the card colour with the
            // `line` edge, so it stands off the paper in both themes.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space2
                Accessible.role: Accessible.List
                Accessible.name: ParentWording.vaultHeading

                Repeater {
                    model: root.report.vault

                    Rectangle {
                        id: sticker
                        required property var modelData

                        width: stickerText.implicitWidth + 2 * Theme.space3
                        height: Theme.space8 + Theme.space1
                        radius: height / 2
                        color: Theme.card
                        border.width: Theme.hair
                        border.color: Theme.line

                        Accessible.role: Accessible.ListItem
                        Accessible.name: sticker.modelData.word

                        Label {
                            id: stickerText
                            anchors.centerIn: parent
                            role: "body"
                            learning: true
                            text: sticker.modelData.word
                            decorative: true
                        }
                    }
                }
            }
        }

        // --- the best -----------------------------------------------------------------
        Section {
            Layout.fillWidth: true
            visible: root.report.best.length > 0
            title: ParentWording.bestHeading
            Accessible.role: Accessible.List
            Accessible.name: ParentWording.bestHeading

            Repeater {
                model: root.report.best

                Label {
                    required property var modelData
                    Layout.fillWidth: true
                    role: "body"
                    text: ParentWording.bestLine(modelData.size, modelData.stars)
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.ListItem
                }
            }
        }

        // The privacy promise is a reassurance on the way out, not an
        // instruction on the way in: it closes the page, as on the child's
        // home.
        Label {
            Layout.fillWidth: true
            role: "caption"
            text: ParentWording.privacyLine
            wrapMode: Text.WordWrap
        }
    }
}
