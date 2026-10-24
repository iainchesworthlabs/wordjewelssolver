// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver

// The one button of the application, a child's and a parent's. A child's button
// carries a picture and a word, and speaks the word when pressed; a parent's is
// the same control at 48 dp, `quiet` so it does not read itself out.
//
// Target sizes are tokens, and none is ever below 48 dp each way:
//
//   size "min"      48 dp   anything an adult touches (`compact`: a 14 px label)
//   size "child"    64 dp   anything a child taps that is not primary
//   size "primary"  96 dp   Shake, Play, Done and the like
//
// A button grows with its label (wrapping, at the 48 dp tier), so a larger text
// size never truncates a word to "Cl...". Variants:
//
//   primary    the one action of a place: emerald fill
//   secondary  a card-coloured button with an outline
//   ghost      text (and an icon) on the paper, a wash on hover
//   danger     a destructive action: filled with `Theme.dangerFill`
//   dangerQuiet  the same colour as an outline, for a step before the last
//   tab        a page switch: text with an emerald underline when `checked`
//   picture    a ghost button whose content is the caller's own picture
//
// `checkable` buttons show their chosen state as a soft emerald fill, a heavier
// emerald edge and a check mark (never gold, never colour alone). A disabled
// button has its own fill, ink and edge; nothing is made see-through. Icon-only
// buttons are for Back and Close only (the lint test holds the list).
//
// `label` is the visible word. `accessibleName` falls back to the label, and
// `speaks` falls back to it too, so a labelled button is never silent.
Item {
    id: root

    property string variant: "secondary"   // primary | secondary | ghost | danger | dangerQuiet | tab | picture
    property string size: "child"          // min | child | primary
    property string iconName: ""
    property string label: ""
    property string accessibleName: ""
    property string speaks: ""
    // An adult's control: nothing is read out when it is pressed.
    property bool quiet: false
    // The 48 dp tier at the smaller label size.
    property bool compact: false
    property bool checkable: false
    property bool checked: false
    // A choice among several (one of a group): a radio button to assistive
    // technology, where a plain checkable button is a toggle.
    property bool radio: false
    property bool wrap: size === "min"
    property real minimumWidth: 0
    property real radius: Theme.radius
    property color ink: Theme.ink
    property color edge: Theme.outline
    property color hoverFill: Theme.hoverWash
    property color pressFill: Theme.pressWash
    property bool pointerFocus: false
    // Focus the button was given when it was made (a screen that starts on it) is
    // not announced with a ring until a key is pressed.
    property bool quietFocus: false

    default property alias content: contentArea.data

    signal clicked

    function activate() {
        if (root.enabled) {
            Feedback.cue("tap", "");
            root.clicked();
        }
    }

    readonly property real targetHeight: size === "primary" ? Theme.targetPrimary
                                       : size === "min" ? Theme.targetMin
                                       : Theme.targetChild
    readonly property bool pressed: mouse.pressed && mouse.containsMouse
    readonly property bool hovered: mouse.containsMouse
    readonly property bool showFocus: activeFocus && !pointerFocus && !quietFocus
    readonly property bool isPrimary: variant === "primary"
    readonly property bool isDanger: variant === "danger"
    readonly property bool isFilled: isPrimary || isDanger
    readonly property bool isTab: variant === "tab"
    readonly property bool isGhost: variant === "ghost" || variant === "picture" || isTab
    readonly property bool hasLabel: label.length > 0
    readonly property bool hasIcon: iconName !== ""
    readonly property bool showChoice: checkable && checked && !isTab
    readonly property real iconSize: size === "primary" ? Theme.iconLg
                                   : size === "min" ? Theme.iconSm
                                   : Theme.iconMd
    property real labelPointSize: size === "min" ? (compact ? Theme.smallSize : Theme.labelSize)
                                                 : Theme.childLabelSize
    readonly property color contentColor: {
        if (!root.enabled) {
            return Theme.disabledInk;
        }
        if (root.isPrimary) {
            return Theme.buttonInk;
        }
        if (root.isDanger) {
            return Theme.dangerInk;
        }
        if (root.variant === "dangerQuiet") {
            return Theme.danger;
        }
        if (root.checked && root.checkable && !root.isTab) {
            return Theme.selectedInk;
        }
        return root.ink;
    }

    // The room the picture and the word take inside the button, without the
    // button's own padding.
    readonly property real contentPad: size === "min" ? (compact ? Theme.space2 : Theme.space3) : Theme.space3
    // Room for the check mark: kept on both sides of the label, so the word stays
    // in the middle whether or not the option is chosen and choosing it moves nothing.
    readonly property real choiceSpace: (checkable && !isTab && size !== "child") ? 2 * (Theme.iconSm + Theme.space2) : 0
    readonly property real rowContentWidth: (hasIcon ? iconSize + (hasLabel ? Theme.space2 : 0) : 0)
                                            + (hasLabel ? labelText.implicitWidth : 0) + choiceSpace
    readonly property real rowContentHeight: Math.max(hasIcon ? iconSize : 0, hasLabel ? labelText.height : 0)
    readonly property real columnContentHeight: (hasIcon ? iconSize : 0) + (hasIcon && hasLabel ? 2 : 0)
                                                + (hasLabel ? childText.height : 0)

    implicitHeight: (hasIcon || hasLabel)
                    ? Math.max(targetHeight, size === "child" ? columnContentHeight + 2 * Theme.space1
                                                              : rowContentHeight + 2 * Theme.space2)
                    : targetHeight
    implicitWidth: Math.max(targetHeight, minimumWidth, 2 * contentPad + (size === "child"
                            ? Math.max(hasIcon ? iconSize : 0, hasLabel ? childText.implicitWidth : 0)
                            : rowContentWidth))
    activeFocusOnTab: root.enabled

    Accessible.role: root.isTab ? Accessible.PageTab
                     : root.radio ? Accessible.RadioButton
                     : Accessible.Button
    Accessible.name: root.accessibleName.length > 0 ? root.accessibleName : root.label
    Accessible.focusable: true
    Accessible.checkable: checkable
    Accessible.checked: checked
    Accessible.onPressAction: root.activate()

    Rectangle {
        id: face
        anchors.fill: parent
        radius: root.radius
        color: {
            if (!root.enabled) {
                return root.isGhost ? "transparent" : Theme.disabledFill;
            }
            if (root.isPrimary) {
                return root.pressed ? Qt.darker(Theme.buttonFill, 1.15) : Theme.buttonFill;
            }
            if (root.isDanger) {
                return root.pressed ? Qt.darker(Theme.dangerFill, 1.15) : Theme.dangerFill;
            }
            if (root.checked && root.checkable && !root.isTab) {
                return root.pressed ? root.pressFill : Theme.selectedFill;
            }
            if (root.isGhost) {
                return root.pressed ? root.pressFill : (root.hovered ? root.hoverFill : "transparent");
            }
            return root.pressed ? root.pressFill : (root.hovered ? root.hoverFill : Theme.card);
        }
        border.width: {
            if (!root.enabled) {
                return (root.variant === "secondary" || root.variant === "dangerQuiet") ? Theme.hair : 0;
            }
            if (root.checked && root.checkable && !root.isTab) {
                return Theme.heavy;
            }
            return (root.variant === "secondary" || root.variant === "dangerQuiet") ? Theme.hair : 0;
        }
        border.color: {
            if (!root.enabled) {
                return Theme.disabledEdge;
            }
            if (root.checked && root.checkable && !root.isTab) {
                return Theme.selectedEdge;
            }
            return root.variant === "dangerQuiet" ? Theme.danger : root.edge;
        }
    }

    // A tab's chosen page: an emerald underline across its foot.
    Rectangle {
        visible: root.isTab && root.checked
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
        height: Theme.litEdge
        radius: Theme.radiusXs
        color: Theme.selectedEdge
    }

    Item {
        id: contentArea
        anchors.fill: parent
    }

    // 48 and 96 dp tiers: the picture beside the word.
    Row {
        id: labelRow
        visible: root.size !== "child" && (root.hasIcon || root.hasLabel)
        anchors.centerIn: parent
        spacing: Theme.space2

        Icon {
            visible: root.checkable && !root.isTab && root.size !== "child"
            width: root.checkable && !root.isTab && root.size !== "child" ? Theme.iconSm : 0
            height: width
            name: "done"
            strokeWidth: Theme.litEdge
            anchors.verticalCenter: parent.verticalCenter
            // Held in place when not chosen, so choosing does not shift the word.
            opacity: root.showChoice ? 1 : 0
            color: root.contentColor
        }
        Icon {
            visible: root.hasIcon
            name: root.iconName
            width: root.iconSize
            height: width
            anchors.verticalCenter: parent.verticalCenter
            color: root.contentColor
            strokeWidth: root.size === "primary" ? 2.4 : 2
        }
        Label {
            id: labelText
            visible: root.hasLabel
            role: "label"
            decorative: true
            text: root.label
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, Math.max(0, root.width - 2 * root.contentPad
                                                        - (root.hasIcon ? root.iconSize + Theme.space2 : 0)
                                                        - root.choiceSpace))
            size: root.labelPointSize
            font.family: root.size === "primary" ? Theme.fontFamily : Theme.uiFont
            font.weight: (root.isPrimary || root.checked || root.size === "primary") ? Font.Bold : Font.DemiBold
            color: root.contentColor
            wrapMode: root.wrap ? Text.WordWrap : Text.NoWrap
            elide: root.wrap ? Text.ElideNone : Text.ElideRight
            maximumLineCount: root.wrap ? 3 : 1
            horizontalAlignment: Text.AlignHCenter
        }
        // The check mark's twin on the other side, empty.
        Item {
            visible: root.checkable && !root.isTab && root.size !== "child"
            width: Theme.iconSm
            height: 1
        }
    }

    // The 64 dp tier: the picture above the word.
    Column {
        id: labelColumn
        visible: root.size === "child" && (root.hasIcon || root.hasLabel)
        anchors.centerIn: parent
        spacing: 2

        Icon {
            visible: root.hasIcon
            name: root.iconName
            width: root.iconSize
            height: width
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.contentColor
            strokeWidth: 2
        }
        Label {
            id: childText
            visible: root.hasLabel
            role: "child"
            decorative: true
            text: root.label
            width: Math.min(implicitWidth, Math.max(0, root.width - 2 * Theme.space2))
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
            size: root.labelPointSize
            font.weight: Font.Bold
            color: root.contentColor
            wrapMode: root.wrap ? Text.WordWrap : Text.NoWrap
            elide: root.wrap ? Text.ElideNone : Text.ElideRight
        }
    }

    // A chosen child button carries its check at the corner.
    Rectangle {
        visible: root.showChoice && root.size === "child"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.space1
        width: Theme.iconSm
        height: width
        radius: width / 2
        color: Theme.selectedEdge
        Icon {
            anchors.centerIn: parent
            width: Theme.iconXs
            height: width
            name: "done"
            strokeWidth: Theme.litEdge
            color: Theme.buttonInk
        }
    }

    FocusRing {
        visible: root.showFocus
        inner: root.radius
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        onPressed: {
            root.pointerFocus = true;
            const spoken = root.speaks.length > 0 ? root.speaks : root.label;
            if (!root.quiet && spoken.length > 0) {
                Feedback.cue("speak", spoken);
            }
        }
        onClicked: root.activate()
    }

    Component.onCompleted: root.quietFocus = root.activeFocus

    onActiveFocusChanged: {
        if (!root.activeFocus) {
            root.pointerFocus = false;
            root.quietFocus = false;
        }
    }

    Keys.onPressed: event => {
        root.pointerFocus = false;
        root.quietFocus = false;
        // Only the keys that act are taken: Tab, Escape and the rest go on to
        // whatever is above (a Sheet's Escape, the window's Tab order).
        const acts = event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
        if (acts) {
            root.activate();
        }
        event.accepted = acts;
    }
}
