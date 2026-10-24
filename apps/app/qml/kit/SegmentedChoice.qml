// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// One choice among a few: a row of buttons, the chosen one filled soft emerald
// with an emerald edge and a check mark. It replaces the parent area's PChoice
// and Solo Play's board-size cards. Each option is { value, label } and may
// carry `enabled: false` and `iconName`. The options wrap onto another line when
// there is not room, and every button is at least 48 dp high (`size`).
//
// For assistive technology the whole is a labelled group (`title`) and each
// option is a radio button named "<title>: <label>" - not, as PChoice was, a
// focusable radio button wrapped round more radio buttons. An option that carries
// `name` is named by it instead (a numeral on screen, "3 by 3 board" to a reader).
Flow {
    id: root

    property var options: []
    property var current
    // The name of the group: "Help", and each option is "Help: On".
    property string title
    property string size: "min"
    property bool compact: false
    // The least width of one option; Solo Play's size cards use Theme.choiceMin.
    property real optionMinimumWidth: 0

    signal chosen(var value)

    Layout.fillWidth: true
    spacing: Theme.space2
    // The options are read from the leading side: left in a left-to-right
    // language, right in a right-to-left one.

    Accessible.role: Accessible.Grouping
    Accessible.name: root.title

    Repeater {
        model: root.options
        AppButton {
            required property var modelData
            size: root.size
            compact: root.compact
            quiet: root.size === "min"
            minimumWidth: root.optionMinimumWidth
            label: modelData.label
            iconName: modelData.iconName === undefined ? "" : modelData.iconName
            checkable: true
            radio: true
            checked: root.current === modelData.value
            enabled: modelData.enabled === undefined || modelData.enabled
            accessibleName: modelData.name !== undefined ? modelData.name
                            : root.title.length > 0 ? root.title + ": " + modelData.label : modelData.label
            onClicked: root.chosen(modelData.value)
        }
    }
}
