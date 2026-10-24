// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import WordJewelsSolver

// The persistent top bar of a screen, the child's and the parent's. On home it
// carries the wordmark; on a child screen the way back and the screen's name;
// in the parent area the wordmark, "Parent area" and the four tabs. Home's
// three entries (the boards, the jewels, the grown-ups) are bordered buttons
// with a picture and a word, so they read as buttons beside one another; the
// grown-up entry is always a labelled control, never a bare person icon.
//
// The bar is 72 dp. When the leading and trailing groups do not fit one line -
// 150% text on a 900 px window - the trailing group drops to a second row and
// the bar grows (`height` follows `implicitHeight`; anchor the screen below
// `bar.bottom`), so nothing is clipped or overlapped. A long title is elided.
Item {
    id: root

    property string title: ""
    property bool showBack: true
    property bool showWordmark: false
    property bool showParent: true
    property bool compact: false
    // The child's own collection: home only, so the words and stars are one
    // press from the first screen rather than hidden behind the play panel.
    property bool showJewels: false
    // Solo Play's earlier boards: home only, so the boards are one press from
    // the first screen rather than hidden behind the play panel.
    property bool showBoards: false
    // Page switches, [{id, label}], drawn as tabs at the trailing end; the one
    // whose id is `currentTab` is chosen. Used by the parent area.
    property var tabs: []
    property string currentTab: ""
    property string backLabel: qsTr("Back")

    // The Back button, for a screen that places something by it (the parent
    // area's language check reads where it is).
    readonly property Item backItem: backButton

    signal back
    signal parentRequested
    signal jewelsRequested
    signal boardsRequested
    signal tabSelected(string id)

    readonly property real margin: Theme.space4
    readonly property bool hasTrailing: showBoards || showJewels || showParent || tabs.length > 0
    // One line if both groups fit; otherwise two.
    readonly property real naturalLeading: (backButton.visible ? backButton.implicitWidth + Theme.space3 : 0)
                                           + (mark.visible ? mark.implicitWidth + Theme.space3 : 0)
                                           + (titleLabel.visible ? titleLabel.implicitWidth : 0)
    // The width both groups want on one line.
    readonly property real naturalWidth: naturalLeading + trailing.implicitWidth + 3 * margin
    readonly property bool stacked: hasTrailing && naturalWidth > root.width

    // The first line is 72 dp or, at a large text size, whatever the wordmark and
    // the Back button need; a second line is added when the trailing group drops.
    readonly property real firstRowHeight: Math.max(Theme.appBarHeight, leading.height + 2 * Theme.space2)
    implicitHeight: stacked ? firstRowHeight + Theme.targetMin + Theme.space3 : firstRowHeight
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.hair
        color: Theme.line
    }

    // A Row aligns its children at the top, so each one centres itself on the
    // bar's middle line: the title, the wordmark and the buttons share it.
    Row {
        id: leading
        anchors.left: parent.left
        anchors.leftMargin: root.margin
        y: (root.firstRowHeight - height) / 2
        spacing: Theme.space3

        AppButton {
            id: backButton
            visible: root.showBack
            size: "min"
            variant: "ghost"
            iconName: "back"
            label: root.backLabel
            accessibleName: root.backLabel
            onClicked: root.back()
        }

        Wordmark {
            id: mark
            visible: root.showWordmark
            compact: root.compact
            anchors.verticalCenter: parent.verticalCenter
        }

        Label {
            id: titleLabel
            visible: root.title.length > 0
            height: Theme.targetMin
            verticalAlignment: Text.AlignVCenter
            // The room left of the trailing group (or of the bar's end, when
            // that group has a row of its own).
            width: Math.min(implicitWidth, Math.max(Theme.targetMin, root.width - 2 * root.margin
                                                     - (backButton.visible ? backButton.implicitWidth + Theme.space3 : 0)
                                                     - (mark.visible ? mark.implicitWidth + Theme.space3 : 0)
                                                     - (root.stacked ? 0 : trailing.implicitWidth + Theme.space3)))
            role: root.showWordmark ? "body" : "heading"
            text: root.title
            color: root.showWordmark ? Theme.inkSecondary : Theme.ink
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    Row {
        id: trailing
        anchors.right: parent.right
        anchors.rightMargin: root.margin
        y: root.stacked ? root.firstRowHeight : (root.firstRowHeight - height) / 2
        spacing: Theme.space2

        AppButton {
            visible: root.showBoards
            size: "min"
            variant: "secondary"
            quiet: true
            iconName: "history"
            label: qsTr("My boards")
            accessibleName: qsTr("My boards")
            onClicked: root.boardsRequested()
        }

        AppButton {
            visible: root.showJewels
            size: "min"
            variant: "secondary"
            quiet: true
            iconName: "jewel"
            label: qsTr("My jewels")
            accessibleName: qsTr("My jewels")
            onClicked: root.jewelsRequested()
        }

        Repeater {
            model: root.tabs
            AppButton {
                required property var modelData
                size: "min"
                variant: "tab"
                quiet: true
                label: modelData.label
                checkable: true
                checked: root.currentTab === modelData.id
                onClicked: root.tabSelected(modelData.id)
            }
        }

        AppButton {
            visible: root.showParent
            size: "min"
            variant: "secondary"
            quiet: true
            iconName: "parent"
            label: qsTr("For grown-ups")
            accessibleName: qsTr("For grown-ups")
            onClicked: root.parentRequested()
        }
    }
}
