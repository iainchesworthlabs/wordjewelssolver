// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// home: the primary way in is one large Play placard. A second, quieter card
// opens the grown-up area; every adult workflow — the real board, settings and
// progress — lives behind the gate there. The wordmark, the hero and the two
// cards say what the app is without asking a child to read a menu.
//
// The page is sized from the window it is given. At the 900 x 620 minimum the
// hero loses its lede and the cards lose height rather than sliding under the
// bottom edge; below 900 the two cards stack and the page scrolls. In a window
// far larger than the design the column grows (to 1.5 times) and, when it is
// still shorter than the window, sits in the middle of it: the bar spans the
// width, so the content is never left top-left over an empty lower half. The
// two cards are one pair of components laid out by a grid, and each card gets
// its height from its own words (kit/ModeCard.qml), so a larger text size makes
// the card taller instead of drawing the words over its picture.
FocusScope {
    id: root

    required property var controller

    signal soloRequested
    signal parentRequested
    signal collectionRequested
    signal boardsRequested

    readonly property bool wide: width >= 900
    // Home has one primary Play picture and one adult picture; the name is kept
    // for the shell's checks and for a future first-run layout.
    readonly property int pictures: 2

    // The space the page has under the bar (which grows at a large text size).
    readonly property real room: Math.max(0, page.height)
    readonly property bool small: room < 650
    readonly property real margin: small ? Theme.space4 : Theme.space8
    readonly property real gutter: small ? Theme.space4 : Theme.space6
    // How much bigger than the design the window is, to 1.5: the column and the
    // cards grow with it, the type does not (it follows the Text size setting).
    readonly property real zoom: Math.max(1, Math.min(page.width / Theme.pageWidth,
                                                      page.height / Theme.pageHeight, 1.5))
    // The cards take what the hero and the footer leave, up to their largest,
    // and never less than their own words and picture need.
    readonly property real cardHeight: Math.max(
        primaryCard.minimumHeight, familyCard.minimumHeight,
        Math.min(Theme.cardMax * root.zoom,
                 room - 2 * margin - hero.implicitHeight - footer.implicitHeight - 2 * gutter - Theme.hair))

    LayoutMirroring.enabled: Theme.rightToLeft
    LayoutMirroring.childrenInherit: true

    Rectangle {
        anchors.fill: parent
        color: Theme.page
    }

    AppBar {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        showBack: false
        showWordmark: true
        showParent: true
        // Always there: a child who has just started can open it and see the
        // empty shelf with the way to play, rather than waiting for a first find
        // before the collection exists.
        showJewels: true
        // The boards played: the earlier boards are one press from home, not
        // buried in the play panel.
        showBoards: true
        onParentRequested: root.parentRequested()
        onJewelsRequested: root.collectionRequested()
        onBoardsRequested: root.boardsRequested()
    }

    Flickable {
        id: page
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentHeight: Math.max(content.implicitHeight + 2 * root.margin, height)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: content
            width: Math.min(page.width - 2 * root.margin, Theme.pageWidth * root.zoom)
            x: (page.width - width) / 2
            // In the middle of a window taller than the column, otherwise below
            // the margin.
            y: Math.max(root.margin, (page.height - implicitHeight) / 2)
            spacing: root.gutter

            ColumnLayout {
                id: hero
                Layout.fillWidth: true
                spacing: Theme.space2

                Eyebrow {
                    Layout.fillWidth: true
                    text: qsTr("A word-finding game for young readers")
                    accented: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    role: root.small ? "title" : "display"
                    text: qsTr("Shake. Find. Collect.")
                    wrapMode: Text.WordWrap
                }
                Label {
                    // At the minimum height the two ways in matter more than a
                    // second sentence; it comes back as soon as there is room.
                    visible: !root.small
                    Layout.fillWidth: true
                    Layout.maximumWidth: Theme.measure
                    role: "body"
                    learning: true
                    size: Theme.headingSize
                    text: qsTr("A calm word game for little readers. Shake the jewel box, find words, and collect the jewels.")
                    color: Theme.inkSecondary
                    wrapMode: Text.WordWrap
                }
            }

            // Side by side from 900 up; below that one above the other, and the
            // page scrolls rather than squeezing a card until its label is cut.
            GridLayout {
                id: cards
                Layout.fillWidth: true
                columns: root.wide ? 2 : 1
                columnSpacing: root.gutter
                rowSpacing: root.gutter

                ModeCard {
                    id: primaryCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: root.wide ? root.cardHeight : implicitHeight
                    Layout.minimumHeight: minimumHeight
                    primary: true
                    eyebrow: HomeWording.soloEyebrow
                    title: HomeWording.soloPicture
                    subtitle: qsTr("Shake the box. Find words. Collect jewels.")
                    actionLabel: qsTr("Play")
                    onClicked: root.soloRequested()

                    JewelBox {
                        anchors.fill: parent
                        closed: false
                        tiles: ["S", "O", "S", "S", "A", "N", "N", "S", "L"]
                    }
                }

                ModeCard {
                    id: familyCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: root.wide ? root.cardHeight : implicitHeight
                    Layout.minimumHeight: minimumHeight
                    grownUp: true
                    eyebrow: HomeWording.forGrownUps
                    title: HomeWording.familyPicture
                    subtitle: qsTr("Use the camera, or type the board. A grown-up sets\u00a0this\u00a0up.")
                    actionLabel: qsTr("Continue")
                    onClicked: root.parentRequested()

                    TabletArt {
                        anchors.fill: parent
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.hair
                color: Theme.line
            }

            // The two labels keep their own edges and elide only if the window is
            // truly too narrow for both.
            RowLayout {
                id: footer
                Layout.fillWidth: true
                spacing: Theme.space4

                Eyebrow {
                    text: qsTr("Word Jewels Solver")
                    elide: Text.ElideRight
                    Layout.maximumWidth: content.width * 0.42
                }
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.hair
                }
                Label {
                    role: "caption"
                    text: qsTr("Everything stays on this device.")
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    Layout.maximumWidth: content.width * 0.55
                }
            }
        }
    }
}
