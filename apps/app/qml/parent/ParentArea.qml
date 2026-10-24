// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import WordJewelsSolver

// The parent area: the gate, then the dashboard, settings and about. The adult
// workflows live here, behind the gate. The dashboard's "Play with a real
// board" leaves this screen for the scanner while keeping the gate open, so a
// parent does not have to pass it again on the way back.
FocusScope {
    id: root

    required property var controller
    property var scanModel: null
    property var assistModel: null

    property string page: "dashboard"
    property bool rightToLeft: false
    property bool stayUnlockedOnClose: false

    signal closed
    signal scanRequested

    readonly property real backButtonX: root.rightToLeft
                                          ? root.width - Theme.space4 - bar.backItem.width / 2
                                          : Theme.space4 + bar.backItem.width / 2
    readonly property bool mirrored: LayoutMirroring.enabled

    LayoutMirroring.enabled: root.rightToLeft
    LayoutMirroring.childrenInherit: true

    function back() {
        root.stayUnlockedOnClose = false;
        root.controller.lock();
        root.page = "dashboard";
        root.closed();
    }

    focus: true

    Rectangle {
        anchors.fill: parent
        color: Theme.page
    }

    // --- the bar ---------------------------------------------------------------
    // The kit's bar: Back, the wordmark, "Parent area" and, once the gate is open,
    // the four pages as tabs.
    AppBar {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        showWordmark: true
        showParent: false
        title: ParentWording.parentArea
        backLabel: ParentWording.back
        currentTab: root.page
        tabs: root.controller.unlocked
            ? [{id: "dashboard", label: ParentWording.tabDashboard},
               {id: "settings", label: ParentWording.tabSettings},
               {id: "about", label: ParentWording.tabAbout},
               {id: "licences", label: ParentWording.tabLicences}]
            : []
        onBack: root.back()
        onTabSelected: id => {
            root.page = id;
            if (id === "dashboard") {
                root.controller.refresh();
            }
        }
    }

    Loader {
        id: screen
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        focus: true

        sourceComponent: {
            if (!root.controller.unlocked) {
                return gate;
            }
            switch (root.page) {
            case "settings":
                return settings;
            case "about":
                return about;
            case "licences":
                return licences;
            default:
                return dashboard;
            }
        }

        onLoaded: (item as Item).forceActiveFocus()
    }

    Component {
        id: gate
        GateScreen {
            controller: root.controller
        }
    }
    Component {
        id: dashboard
        DashboardScreen {
            controller: root.controller
            onScanRequested: {
                root.stayUnlockedOnClose = true;
                root.scanRequested();
            }
            onPlayRequested: root.back()
        }
    }
    Component {
        id: settings
        SettingsScreen {
            controller: root.controller
        }
    }
    Component {
        id: about
        AboutScreen {
            controller: root.controller
        }
    }
    Component {
        id: licences
        LicencesScreen {
            controller: root.controller
        }
    }

    Keys.onEscapePressed: event => {
        root.back();
        event.accepted = true;
    }

    onVisibleChanged: {
        if (!root.visible && root.controller && !root.stayUnlockedOnClose) {
            root.controller.lock();
        }
    }
    Component.onDestruction: {
        if (root.controller && !root.stayUnlockedOnClose) {
            root.controller.lock();
        }
    }
}
