// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import WordJewelsSolver
import WordJewelsSolver.Parent

// A stub for the application's screen shell, so that the parent area's screens
// can be tried and checked before (and whatever) the shell is: a home with one
// corner button for the parent, and the parent area over it. It is test
// support, not part of the application; the real shell shows ParentArea as one
// screen of its navigation.
Window {
    id: host

    required property var controller
    // The interface's language: the real shell makes it required too, and the
    // mirroring below is bound to it from the first binding evaluation.
    required property var languageManager

    // The parent area's page, for a check to set; and whether it is showing.
    property alias page: area.page
    readonly property bool parentOpen: area.visible
    // What the language check reads: the window's mirroring reaches the screen,
    // and the bar's way back is on the leading edge.
    readonly property bool mirrored: area.mirrored
    readonly property real backButtonX: area.backButtonX

    width: 1024
    height: 700
    visible: true
    title: "Parent area (stub shell)"
    color: "#FBF8F1"

    // What the real shell (Main.qml) binds from the parent's settings and the
    // language, for the kit's tokens every parent screen reads.
    Binding {
        target: Theme
        property: "textScale"
        value: host.controller.textScale
    }
    Binding {
        target: Theme
        property: "colourScheme"
        value: host.controller.colourScheme
    }
    Binding {
        target: Theme
        property: "rightToLeft"
        value: host.languageManager.rightToLeft
    }

    function openParent() {
        area.visible = true;
        area.forceActiveFocus();
    }

    // As the real shell (Main.qml) does: a language chosen in the settings is
    // applied at once, and the engine retranslates (the C++ side connects the
    // manager's languageChanged to QQmlEngine::retranslate).
    Connections {
        target: host.controller

        function onSettingsChanged() {
            host.languageManager.apply(host.controller.language);
        }
    }

    Text {
        anchors.centerIn: parent
        text: "Home (stub)"
        font.pixelSize: 32
        color: "#4A5666"
    }

    // The quiet corner button of the home screen.
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: 64
        height: 64
        radius: 32
        color: "#DCE6F2"
        border.width: 2
        border.color: "#5B6879"
        Accessible.role: Accessible.Button
        Accessible.name: "Parent"
        Text {
            anchors.centerIn: parent
            text: "P"
            font.pixelSize: 24
            color: "#1F2A37"
        }
        TapHandler {
            onTapped: host.openParent()
        }
    }

    ParentArea {
        id: area
        anchors.fill: parent
        visible: false
        controller: host.controller
        onClosed: visible = false
        // As the real shell does: Hebrew and Yiddish mirror the parent area.
        rightToLeft: host.languageManager.rightToLeft
    }
}
