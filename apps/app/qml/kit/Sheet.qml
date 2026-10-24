// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// The one modal of the application: the history of boards, the letter picker,
// the finish summary, a reset confirmation. One scrim rule, one shape.
//
//   * The scrim covers the whole of the Sheet's own area - give the Sheet the
//     area it should dim (anchors.fill the window root and it dims the app bar
//     too, which is the rule; fill a screen's body and the bar stays live).
//   * The sheet is a card with a 20 px radius, at most `maxWidth` wide, with a
//     heading and a Close button in its first row. Every sheet has a title.
//   * It traps focus: Tab and Shift+Tab wrap inside the sheet, focus starts on
//     the sheet and returns to whatever had it when the sheet closes, and
//     Escape closes it. A press on the scrim closes it too (`dismissOnScrim`).
//
// `close` is emitted when the person asks to close it (Escape, the scrim, the
// button); the owner sets `open` to false. The default content is the body.
FocusScope {
    id: root

    property bool open: false
    property string title: ""
    property string closeLabel: qsTr("Close")
    property real maxWidth: Theme.sheetMaxWidth
    property real maxHeight: Theme.sheetMaxHeight
    property bool dismissOnScrim: true

    default property alias content: body.data

    // The item that had focus before the sheet opened, to give it back.
    property Item opener: null

    signal close

    visible: root.open
    z: 100

    function descendantOfSheet(item) {
        for (let at = item; at; at = at.parent) {
            if (at === root) {
                return true;
            }
        }
        return false;
    }

    // The Tab stops inside the sheet, in tree order.
    function focusables() {
        const found = [];
        const walk = item => {
            if (item.visible && item.enabled && item.activeFocusOnTab === true) {
                found.push(item);
            }
            for (let i = 0; i < item.children.length; ++i) {
                walk(item.children[i]);
            }
        };
        walk(root);
        return found;
    }

    // Tab order is the window's, and Tab never reaches the sheet's own Keys
    // handler (the focused button gets it first), so the trap watches the focus
    // itself: when it lands outside the sheet it is sent back in - to the first
    // stop if it left by the last one (Tab wrapping), to the last if it left by
    // the first (Shift+Tab wrapping).
    property Item lastInside: null

    function keepFocusInside() {
        const at = root.Window.activeFocusItem;
        if (!root.open || !at) {
            return;
        }
        if (root.descendantOfSheet(at)) {
            root.lastInside = at;
            return;
        }
        const stops = root.focusables();
        if (stops.length === 0) {
            root.forceActiveFocus();
            return;
        }
        const back = root.lastInside === stops[0];
        stops[back ? stops.length - 1 : 0].forceActiveFocus(back ? Qt.BacktabFocusReason : Qt.TabFocusReason);
    }

    // The window's focused item, followed (a Connections element would need the
    // private header the kit does not ship, as in FlatBoard).
    readonly property Item windowFocus: root.Window.activeFocusItem
    // Put back on the next turn of the event loop: moving the focus while it is
    // being reported would make this binding depend on itself.
    onWindowFocusChanged: Qt.callLater(root.keepFocusInside)

    onOpenChanged: {
        if (root.open) {
            root.opener = root.Window.activeFocusItem;
            root.forceActiveFocus();
        } else {
            if (root.opener) {
                root.opener.forceActiveFocus();
            }
            root.opener = null;
        }
    }

    Keys.onEscapePressed: event => {
        root.close();
        event.accepted = true;
    }

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Theme.scrim

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (root.dismissOnScrim) {
                    root.close();
                }
            }
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(root.maxWidth, root.width - 2 * Theme.space4)
        height: Math.min(root.maxHeight, root.height - 2 * Theme.space4,
                         column.implicitHeight + 2 * Theme.space4)
        radius: Theme.radiusLarge
        color: Theme.card
        border.width: Theme.hair
        border.color: Theme.line

        Accessible.role: Accessible.Dialog
        Accessible.name: root.title

        // Clicks on the sheet are the sheet's, not the scrim's.
        MouseArea {
            anchors.fill: parent
        }

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: Theme.space4
            spacing: Theme.space3

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space3

                Label {
                    Layout.fillWidth: true
                    role: "heading"
                    text: root.title
                    wrapMode: Text.WordWrap
                }
                AppButton {
                    size: "min"
                    variant: "ghost"
                    iconName: "close"
                    accessibleName: root.closeLabel
                    quiet: true
                    onClicked: root.close()
                }
            }

            ColumnLayout {
                id: body
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.space3
            }
        }
    }
}
