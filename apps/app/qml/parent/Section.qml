// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// A titled block of a parent screen: a rule, a heading and the content. The
// heading is a Heading to assistive technology (Label's role does that), and a
// block with no title (the week's sentence) has neither the rule nor the heading.
ColumnLayout {
    id: root

    property string title
    default property alias content: body.data

    spacing: Theme.space3

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.hair
        color: Theme.line
        visible: root.title.length > 0
    }

    Label {
        Layout.fillWidth: true
        visible: root.title.length > 0
        role: "heading"
        text: root.title
        wrapMode: Text.WordWrap
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: Theme.space2
    }
}
