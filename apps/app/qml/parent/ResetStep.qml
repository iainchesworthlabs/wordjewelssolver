// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// One of the parent area's two resets (docs/design/dashboard.md, "Reset"): a
// button that names what it clears, and, once pressed, says what it will do and
// asks for a second, deliberate press. Nothing is cleared by the first.
//
// A step is a card, so it is a thing on the page and not a lone button at the
// margin, and it is in the danger colour throughout: the first button is the
// danger outline, the confirming one the filled danger button, and the question
// between them has the danger edge.
Rectangle {
    id: root

    property string label
    property string says
    property bool done: false

    signal confirmed

    property bool asking: false

    implicitHeight: column.implicitHeight + 2 * Theme.space4
    radius: Theme.radius
    color: Theme.card
    border.width: root.asking ? Theme.heavy : Theme.hair
    border.color: root.asking ? Theme.danger : Theme.line

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: Theme.space4
        spacing: Theme.space3

        AppButton {
            size: "min"
            quiet: true
            variant: "dangerQuiet"
            visible: !root.asking
            label: root.label
            onClicked: {
                root.done = false;
                root.asking = true;
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.asking
            role: "body"
            text: root.says
            wrapMode: Text.WordWrap
        }
        Flow {
            Layout.fillWidth: true
            visible: root.asking
            spacing: Theme.space2

            AppButton {
                size: "min"
                quiet: true
                label: ParentWording.resetKeep
                onClicked: root.asking = false
            }
            AppButton {
                size: "min"
                quiet: true
                label: ParentWording.resetConfirm
                variant: "danger"
                Accessible.name: root.label
                onClicked: {
                    root.asking = false;
                    root.done = true;
                    root.confirmed();
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.done && !root.asking
            role: "caption"
            text: ParentWording.resetDone
        }
    }
}
