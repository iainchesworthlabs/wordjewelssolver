// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import WordJewelsSolver

// An outline button that needs a deliberate press: hold it for a second while a
// ring fills (docs/design/game.md, "The resting screen"). It is a brake and not
// a gate: a child or a parent can do it. A screen reader, a switch or a key
// activates it by the usual action, with no hold. Letting go early starts the
// ring over; nothing is lost.
Item {
    id: root

    property string iconName: "again"
    property string label: ""
    property string accessibleName: ""
    property int holdMs: Theme.holdToPlay

    signal activated

    // 0 to 1, how far the ring has filled.
    property real progress: 0
    property bool pointerFocus: false
    readonly property bool holding: hold.running

    implicitWidth: Theme.targetChild * 2
    implicitHeight: Theme.targetChild
    activeFocusOnTab: true
    onActiveFocusChanged: {
        if (!root.activeFocus) {
            root.pointerFocus = false;
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: root.activated()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.holding ? Theme.pressWash : Theme.card
        border.width: Theme.hair
        border.color: Theme.outline
    }

    Row {
        anchors.centerIn: parent
        spacing: Theme.space2
        Icon {
            name: root.iconName
            width: Theme.iconMd
            height: width
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.ink
        }
        Label {
            visible: root.label.length > 0
            role: "label"
            decorative: true
            text: root.label
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // The ring that fills, around the edge of the button.
    Shape {
        anchors.fill: parent
        visible: root.progress > 0
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.buttonFill
            strokeWidth: Theme.heavy * 2
            fillColor: "transparent"
            startX: 0
            startY: 0
            PathLine { x: root.width * Math.min(1, root.progress * 4); y: 0 }
        }
        ShapePath {
            strokeColor: Theme.buttonFill
            strokeWidth: Theme.heavy * 2
            fillColor: "transparent"
            startX: root.width
            startY: 0
            PathLine { x: root.width; y: root.height * Math.max(0, Math.min(1, root.progress * 4 - 1)) }
        }
        ShapePath {
            strokeColor: Theme.buttonFill
            strokeWidth: Theme.heavy * 2
            fillColor: "transparent"
            startX: root.width
            startY: root.height
            PathLine { x: root.width * (1 - Math.max(0, Math.min(1, root.progress * 4 - 2))); y: root.height }
        }
        ShapePath {
            strokeColor: Theme.buttonFill
            strokeWidth: Theme.heavy * 2
            fillColor: "transparent"
            startX: 0
            startY: root.height
            PathLine { x: 0; y: root.height * (1 - Math.max(0, Math.min(1, root.progress * 4 - 3))) }
        }
    }

    FocusRing {
        visible: root.activeFocus && !root.pointerFocus
        inner: Theme.radius
    }

    NumberAnimation {
        id: hold
        target: root
        property: "progress"
        from: 0
        to: 1
        duration: root.holdMs
        onFinished: {
            root.progress = 0;
            root.activated();
        }
    }

    MouseArea {
        anchors.fill: parent
        onPressed: {
            root.pointerFocus = true;
            hold.restart();
        }
        onReleased: release()
        onCanceled: release()
        // Sliding off the button before the ring is full cancels.
        onContainsPressChanged: {
            if (!containsPress) {
                release();
            }
        }

        function release() {
            if (hold.running) {
                hold.stop();
                root.progress = 0;
            }
        }
    }

    Keys.onPressed: event => {
        root.pointerFocus = false;
        const acts = event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
        if (acts) {
            root.activated();
        }
        event.accepted = acts;
    }
}
