// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver

// A thin handle on the trailing edge of a scrolling list, drawn only while the
// list holds more than it shows: where the list stands, and that it goes on. It
// is a sibling of the list it follows (anchor it to the list's edge) and takes no
// touch of its own.
Item {
    id: root

    // The ListView or GridView it follows.
    required property Flickable view

    readonly property bool needed: view.contentHeight > view.height + 1

    visible: needed
    width: Theme.space1
    Accessible.ignored: true

    Rectangle {
        y: root.view.visibleArea.yPosition * root.height
        width: parent.width
        height: Math.max(Theme.space6, root.view.visibleArea.heightRatio * root.height)
        radius: width / 2
        color: Theme.outline
    }
}
