// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// The parental gate: one large press-and-hold target, not two far-apart
// circles. A parent should not have to discover that a mouse on one circle and
// Space on the keyboard have to coincide; the whole card explains the one
// gesture, and a keyboard user holds Space or Enter.
//
// The gate's own logic is unchanged: two touches, one in each hidden spot, for
// two seconds. The button presses both spots for the person, so the hand and
// the keyboard do the same thing.
FocusScope {
    id: root

    required property var controller

    LayoutMirroring.enabled: Theme.rightToLeft
    LayoutMirroring.childrenInherit: true

    // The two virtual touches the one button puts down. Ids outside the range a
    // real touch would use, so they cannot collide with a palm.
    readonly property int firstTouch: 9001
    readonly property int secondTouch: 9002
    readonly property bool holding: root.controller.gateHolding

    focus: true

    function now() {
        return root.controller.now();
    }

    function beginHold() {
        if (root.controller.gateSpots.length !== 2 || root.holding) {
            return;
        }
        const first = root.controller.gateSpots[0];
        const second = root.controller.gateSpots[1];
        root.controller.gateTouchDown(root.firstTouch, first.x, first.y, root.now());
        root.controller.gateTouchDown(root.secondTouch, second.x, second.y, root.now());
    }

    function endHold() {
        root.controller.gateTouchUp(root.firstTouch, root.now());
        root.controller.gateTouchUp(root.secondTouch, root.now());
    }

    onActiveFocusChanged: {
        if (!activeFocus) {
            root.endHold();
        }
    }

    Keys.onPressed: event => {
        if ((event.key === Qt.Key_Space || event.key === Qt.Key_Return
             || event.key === Qt.Key_Enter) && !event.isAutoRepeat) {
            root.beginHold();
            event.accepted = true;
        }
    }
    Keys.onReleased: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            root.endHold();
            event.accepted = true;
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.page
    }

    // The card sits in the middle of the page and takes its height from what is
    // on it (it was a fixed 560 x 440 with a dead third below its last line); a
    // window too short for it, or a large text size, scrolls it.
    Flickable {
        id: view
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, card.height + 2 * Theme.space8)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        interactive: contentHeight > height

        Rectangle {
            id: card
            width: Math.min(view.width - 2 * Theme.space8, Theme.modalMax)
            height: words.implicitHeight + 2 * Theme.space6
            x: (view.width - width) / 2
            y: Math.max(Theme.space8, (view.contentHeight - height) / 2)
            radius: Theme.radius
            color: Theme.card
            border.width: Theme.hair
            border.color: Theme.line

            ColumnLayout {
                id: words
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.space6
                spacing: Theme.space2

                Label {
                    Layout.fillWidth: true
                    role: "title"
                    text: ParentWording.gateTitle
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    role: "body"
                    size: Theme.headingSize
                    text: ParentWording.gateInstruction
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    role: "caption"
                    text: ParentWording.gateWhy
                    wrapMode: Text.WordWrap
                }

                // The one target. It is deliberately large: 96 dp high, the whole
                // width of the card less its margins; its edge is the action
                // colour, so it looks like a thing to press.
                Rectangle {
                    id: holdTarget
                    objectName: "gateHoldTarget"
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space4
                    Layout.preferredHeight: Math.max(Theme.targetPrimary, holdWords.implicitHeight + 2 * Theme.space3)
                    radius: Theme.radius
                    color: root.holding ? Theme.selectedFill : Theme.card
                    border.width: Theme.heavy
                    border.color: Theme.accent
                    activeFocusOnTab: true

                    readonly property bool showFocus: activeFocus && !pointerFocus
                    property bool pointerFocus: false

                    Accessible.role: Accessible.Button
                    Accessible.name: ParentWording.gateHoldButton
                    Accessible.focusable: true
                    Accessible.onPressAction: root.controller.unlockForAssistiveTechnology()

                    onActiveFocusChanged: {
                        if (!holdTarget.activeFocus) {
                            holdTarget.pointerFocus = false;
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onPressed: {
                            holdTarget.pointerFocus = true;
                            holdTarget.forceActiveFocus();
                            root.beginHold();
                        }
                        onReleased: root.endHold()
                        onCanceled: root.endHold()
                        onContainsPressChanged: {
                            if (!containsPress) {
                                root.endHold();
                            }
                        }
                    }

                    ColumnLayout {
                        id: holdWords
                        anchors.centerIn: parent
                        width: parent.width - 2 * Theme.space3
                        spacing: Theme.space1
                        Label {
                            Layout.fillWidth: true
                            role: "heading"
                            text: ParentWording.gateHoldButton
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            decorative: true
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "caption"
                            text: ParentWording.gateHoldHint
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            decorative: true
                        }
                    }

                    FocusRing {
                        visible: holdTarget.showFocus
                        inner: holdTarget.radius
                    }
                }

                // The progress is a bar, not a ring around two hidden spots: it
                // says exactly how much longer the press has to last. The empty
                // track is outlined in the token that holds 3:1 against the card.
                Rectangle {
                    id: progress
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.space2
                    radius: Theme.radiusXs
                    color: Theme.hoverWash
                    border.width: Theme.hair
                    border.color: Theme.track

                    Rectangle {
                        width: parent.width * Math.min(1, root.controller.gateProgress)
                        height: parent.height
                        radius: parent.radius
                        color: Theme.buttonFill
                    }
                }

                Label {
                    Layout.fillWidth: true
                    role: "caption"
                    horizontalAlignment: Text.AlignHCenter
                    text: ParentWording.gateKeys
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    // The gate's two spots are still laid out, because the same Gate answers
    // the virtual touches. The button covers the field, so the spots themselves
    // are never the thing a parent has to aim at.
    Item {
        id: field
        anchors.fill: parent
        visible: false

        onWidthChanged: root.layout()
        onHeightChanged: root.layout()
        Component.onCompleted: root.layout()
    }

    function layout() {
        if (field.width > 0 && field.height > 0) {
            root.controller.layoutGate(field.width, field.height, Theme.targetMin);
        }
    }

    FrameAnimation {
        running: root.controller.gateHolding
        onTriggered: root.controller.gateTick(root.now())
    }

    Component.onDestruction: root.endHold()
}
