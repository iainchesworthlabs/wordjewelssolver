// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// The letter picker: a tap on a tile of the check screen opens this, as the
// kit's one Sheet. A suggestion the reader had comes first, then A to Z, "Qu",
// and a field for a tile of two or three letters with "Use these" beside it.
//
// Being a Sheet is what the picker was missing: a 20 px radius and a heading, a
// scrim that covers the whole screen (the stepper and the app bar dim with the
// rest, where the old dialog left the stepper bright above it), a focus trap,
// Escape to close, and focus handed back to the tile that opened it. It is sized
// by its content, so it no longer leaves 200 px of nothing under the letters.
//
// The picker holds no state of its own about the tile: `picked` is the check
// screen's, `scan` is the model the letter is written to, and `chosen` /
// `dismissed` tell the owner what happened. A letter the model refuses leaves the
// picker open.
Sheet {
    id: root

    required property ScanModel scan
    // The tile being corrected, or -1 when the picker is closed.
    property int picked: -1

    // A letter, a pair or a word of up to three was chosen and the model took it.
    signal chosen
    // The picker was closed without a letter.
    signal dismissed

    readonly property var alphabet: "ABCDEFGHIJKLMNOPQRSTUVWXYZ".split("")
    readonly property real gap: Theme.space2
    readonly property real innerWidth: Math.min(root.maxWidth, root.width - 2 * Theme.space4) - 2 * Theme.space4
    readonly property int columns: Math.max(4, Math.floor((root.innerWidth + root.gap)
                                                          / (Theme.targetMin + root.gap)))
    readonly property real letterSize: Math.max(Theme.targetMin,
        Math.min(Theme.targetChild, (root.innerWidth - (root.columns - 1) * root.gap) / root.columns))

    open: root.picked >= 0
    title: ScanWording.pickerTitle
    closeLabel: ScanWording.closePicker
    maxWidth: Theme.modalMax
    // The scrim is the screen's: a press on it closes the picker, as Escape does.
    onClose: root.dismissed()

    function choose(letters) {
        if (root.picked < 0) {
            return;
        }
        // The model says whether it can be a tile; a refused picker stays open.
        if (root.scan.setTile(root.picked, letters)) {
            otherLetters.text = "";
            root.chosen();
        }
    }

    Flickable {
        id: scroller
        Layout.fillWidth: true
        Layout.preferredHeight: content.implicitHeight
        Layout.fillHeight: true
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: content
            width: scroller.width
            spacing: Theme.space3

            // The reader's other thoughts, best first.
            ColumnLayout {
                visible: suggestions.count > 0
                Layout.fillWidth: true
                spacing: Theme.space1

                Eyebrow {
                    text: ScanWording.pickerSuggestions
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: root.gap

                    Repeater {
                        id: suggestions
                        model: root.picked >= 0 ? root.scan.alternatives(root.picked) : []

                        AppButton {
                            id: suggestion
                            required property string modelData
                            size: "min"
                            variant: "secondary"
                            quiet: true
                            minimumWidth: root.letterSize
                            label: suggestion.modelData
                            labelPointSize: Theme.wordSize
                            accessibleName: suggestion.modelData
                            onClicked: root.choose(suggestion.modelData)
                        }
                    }
                }
            }

            Grid {
                id: letters
                Layout.fillWidth: true
                columns: root.columns
                spacing: root.gap

                Repeater {
                    model: root.alphabet

                    AppButton {
                        id: letterButton
                        required property string modelData
                        width: root.letterSize
                        size: "min"
                        variant: "secondary"
                        quiet: true
                        minimumWidth: root.letterSize
                        label: letterButton.modelData
                        labelPointSize: Theme.wordSize
                        accessibleName: letterButton.modelData
                        onClicked: root.choose(letterButton.modelData)
                    }
                }
            }

            // Qu is one die face; the box is for anything else the die says.
            RowLayout {
                Layout.fillWidth: true
                spacing: root.gap

                AppButton {
                    size: "min"
                    variant: "secondary"
                    quiet: true
                    minimumWidth: root.letterSize
                    label: ScanWording.pickerQu
                    labelPointSize: Theme.wordSize
                    accessibleName: ScanWording.pickerQu
                    onClicked: root.choose("QU")
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.targetMin
                    radius: Theme.radius
                    color: Theme.card
                    border.width: otherLetters.activeFocus ? Theme.heavy : Theme.hair
                    border.color: otherLetters.activeFocus ? Theme.focusRing : Theme.outline

                    TextInput {
                        id: otherLetters
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space3
                        anchors.rightMargin: Theme.space3
                        verticalAlignment: TextInput.AlignVCenter
                        horizontalAlignment: TextInput.AlignHCenter
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.wordSize
                        font.weight: Font.Bold
                        font.capitalization: Font.AllUppercase
                        color: Theme.ink
                        maximumLength: 3
                        selectByMouse: true
                        activeFocusOnTab: true
                        Accessible.role: Accessible.EditableText
                        Accessible.name: ScanWording.pickerOther
                        onAccepted: root.choose(otherLetters.text)

                        // The field says what it is for while it is empty.
                        Label {
                            anchors.fill: parent
                            visible: otherLetters.text.length === 0 && !otherLetters.preeditText
                            decorative: true
                            role: "caption"
                            text: ScanWording.pickerOther
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }

                AppButton {
                    size: "min"
                    variant: "primary"
                    quiet: true
                    enabled: otherLetters.text.length > 0
                    label: ScanWording.useLetters
                    accessibleName: ScanWording.useLetters
                    onClicked: root.choose(otherLetters.text)
                }
            }
        }
    }
}
