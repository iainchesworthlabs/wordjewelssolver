// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver

// The lockup: a faceted jewel and the name. Word Jewels is the child-facing
// name; SOLVER is the product qualifier, in the house mono face. The same
// lockup is used in the home bar, the parent area's bar and the About page.
// Both words are Labels, so they follow the parent's Text size setting like the
// rest of the bar (they were 23/18 px and 9/7 px literals that ignored it).
Row {
    id: root

    property bool compact: false
    spacing: compact ? Theme.space2 : Theme.space3

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Word Jewels") + " " + qsTr("SOLVER")

    Jewel {
        width: root.compact ? Theme.jewelSm : Theme.jewelMd
        height: width
        anchors.verticalCenter: parent.verticalCenter
        tier: "power"
        color: Theme.gold
    }

    Column {
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        Label {
            role: "display"
            decorative: true
            text: qsTr("Word Jewels")
            size: root.compact ? Theme.headingSize : Theme.wordSize
            lineRatio: Theme.lineHeading
        }
        Label {
            role: "mono"
            decorative: true
            text: qsTr("SOLVER")
        }
    }
}
