// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// About and privacy (docs/design/dashboard.md, "Settings"). Plain text: no link
// to anywhere, because the application has nowhere to go. The licences have
// their own page (LicencesScreen.qml), a tab of the bar beside this one, so the
// legal text is not the longest thing on this page and nothing here repeats the
// tab.
Flickable {
    id: root

    required property var controller

    // The parent area's direction, from the shell.
    LayoutMirroring.enabled: Theme.rightToLeft
    LayoutMirroring.childrenInherit: true

    contentWidth: width
    contentHeight: column.implicitHeight + 2 * Theme.space6
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick

    component Paragraph: Label {
        Layout.fillWidth: true
        role: "body"
        wrapMode: Text.WordWrap
    }

    ColumnLayout {
        id: column
        x: Math.max(Theme.space4, (root.width - width) / 2)
        y: Theme.space6
        width: Math.min(root.width - 2 * Theme.space4, Theme.contentWidth)
        spacing: Theme.space8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space1
            Label {
                Layout.fillWidth: true
                role: "title"
                text: ParentWording.appName
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                role: "body"
                size: Theme.headingSize
                text: ParentWording.tagline
                color: Theme.inkSecondary
                wrapMode: Text.WordWrap
            }
        }

        // The version and the build's provenance: no heading, because the page
        // title above is the name they belong to.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            Paragraph {
                text: ParentWording.version(root.controller.version)
            }
            // The build's provenance, as the build system stamped it: the
            // release, the commit, the branch and the target. A technical
            // label, in the mono face, not a sentence to translate.
            Label {
                id: details
                objectName: "versionDetails"
                Layout.fillWidth: true
                role: "caption"
                text: root.controller.versionDetails
                font.family: Theme.monoFont
                wrapMode: Text.WordWrap
            }
        }

        // The one privacy section: the summary line, then what is kept and
        // what is never done.
        Section {
            Layout.fillWidth: true
            title: ParentWording.privacyHeading
            Paragraph {
                text: ParentWording.privacySummary
                font.weight: Font.DemiBold
            }
            Paragraph {
                text: ParentWording.privacyKept
            }
            Paragraph {
                text: ParentWording.privacyNever
            }
            Paragraph {
                text: ParentWording.privacyDevice
            }
        }
    }
}
