// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver

// A large choice on the home screen: a picture, a verb and a short line. The
// whole card is the target, so a child does not have to find a small button.
//
// The picture takes the space the words leave, not a fixed share of the card:
// the art area is anchored between the card's top and the words block, so a
// shorter card (the minimum window height) or larger text shrinks the art and
// the label block keeps its place at the bottom instead of the two overlapping.
// The picture is also promised a room of its own: `implicitHeight` is the art's
// preferred height plus the words' own height, and `minimumHeight` the least
// the art may be given (Theme.cardArtMin), so a screen that lays the card out
// reads its height from the card and never writes a number. Both cards use one
// title role; `primary` only changes the edge and the fill.
//
// The card draws the keyboard focus ring (not drawn after a tap or a click, nor
// on the first paint of the card that holds focus from the start), and its
// arrow is Icon "forward", which turns round in a right-to-left layout.
Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    property string actionLabel: ""
    property string eyebrow: ""
    // What a screen reader says: the visible title unless a screen says otherwise,
    // so the name of the card is the words written on it.
    property string accessibleName: root.title
    property bool primary: false
    property bool grownUp: false
    // The least number of lines the sentence under the title is given room for.
    property int subtitleLines: 2
    property bool pointerFocus: false
    // Focus the card was given when it was made is not announced with a ring.
    property bool quietFocus: false

    readonly property bool showFocus: activeFocus && !pointerFocus && !quietFocus

    default property alias art: artArea.data
    signal clicked

    // The art's room above the words, and the words' own height under it.
    readonly property real wordsBlock: words.implicitHeight + Theme.space6
    implicitHeight: Theme.space4 + Theme.cardArt + Theme.space3 + wordsBlock
    readonly property real minimumHeight: Theme.space4 + Theme.cardArtMin + Theme.space3 + wordsBlock
    radius: Theme.radiusLarge
    color: primary ? Theme.surface : Theme.card
    border.width: primary ? Theme.heavy : Theme.hair
    border.color: primary ? Theme.accent : Theme.line
    activeFocusOnTab: true
    focus: primary

    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: root.clicked()

    Component.onCompleted: root.quietFocus = root.activeFocus

    onActiveFocusChanged: {
        if (!root.activeFocus) {
            root.pointerFocus = false;
            root.quietFocus = false;
        }
    }

    Keys.onPressed: event => {
        root.pointerFocus = false;
        root.quietFocus = false;
        event.accepted = false;
    }
    Keys.onSpacePressed: event => {
        root.clicked();
        event.accepted = true;
    }
    Keys.onReturnPressed: event => {
        root.clicked();
        event.accepted = true;
    }
    Keys.onEnterPressed: event => {
        root.clicked();
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: taps.pressed ? Theme.pressWash : (hover.hovered ? Theme.hoverWash : "transparent")
    }

    Item {
        id: artArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: words.top
        anchors.margins: Theme.space4
        anchors.bottomMargin: Theme.space3
        clip: true
    }

    Column {
        id: words
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space6
        spacing: Theme.space1

        Eyebrow {
            visible: root.eyebrow.length > 0
            width: parent.width
            text: root.eyebrow
            accented: !root.grownUp
            elide: Text.ElideRight
            decorative: true
        }
        Label {
            width: parent.width
            role: "title"
            text: root.title
            font.weight: Font.ExtraBold
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            decorative: true
        }
        // The line under the title always has room for `subtitleLines` lines, so
        // two cards side by side start their words at the same height whichever
        // of the two sentences wraps.
        Item {
            width: parent.width
            height: Math.max(subtitleText.implicitHeight, root.subtitleLines * subtitleText.lineRatio * subtitleText.size)

            Label {
                id: subtitleText
                width: parent.width
                role: "body"
                learning: true
                text: root.subtitle
                color: Theme.inkSecondary
                wrapMode: Text.WordWrap
                maximumLineCount: root.primary ? 2 : 3
                elide: Text.ElideRight
                decorative: true
            }
        }
        Row {
            spacing: Theme.space2
            Label {
                anchors.verticalCenter: parent.verticalCenter
                role: "label"
                text: root.actionLabel
                color: Theme.accent
                decorative: true
            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "forward"
                width: Theme.iconSm
                height: width
                color: Theme.accent
                strokeWidth: Theme.litEdge - 0.5
            }
        }
    }

    FocusRing {
        visible: root.showFocus
        inner: root.radius
    }

    HoverHandler {
        id: hover
    }
    TapHandler {
        id: taps
        onPressedChanged: {
            if (taps.pressed) {
                root.pointerFocus = true;
            }
        }
        onTapped: root.clicked()
    }
}
