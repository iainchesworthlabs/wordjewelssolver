// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// The licences, on their own page (docs/design/dashboard.md, "Settings"): what
// this build includes and the licence of each part, with the full third-party
// notices in their own scroll. A tab of the parent area's bar, so the legal text
// never lengthens the About page. The page's title is the tab's name, said once.
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
        spacing: Theme.space6

        Label {
            Layout.fillWidth: true
            role: "title"
            text: ParentWording.licencesHeading
        }

        Paragraph {
            text: ParentWording.licenceNotice
        }
        Paragraph {
            text: ParentWording.licenceParts
        }
        Paragraph {
            text: ParentWording.licencesFull
        }
        // The full notices of this build: the same text as NOTICES.txt
        // beside the programs, embedded as :/notices/NOTICES.txt
        // (cmake/Notices.cmake). It has a scroll of its own - a licence
        // text is long, and the page should not have to be as long as the
        // licences to be read. It is set in the mono face: the notices are
        // ruled and aligned with plain characters.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sheetMaxHeight
            color: Theme.card
            border.width: Theme.hair
            border.color: Theme.line
            radius: Theme.radius

            Flickable {
                id: notices
                objectName: "licencesView"
                anchors.fill: parent
                anchors.margins: Theme.space3
                contentWidth: width
                contentHeight: noticeText.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick

                Label {
                    id: noticeText
                    // The wording check leaves this one text alone
                    // (apps/app/tests/unit/parent_ui/preview_main.cpp): it is quoted
                    // legal text, not a sentence about a child.
                    objectName: "licenceNotices"
                    width: notices.width
                    text: root.controller.licenceNotices
                    role: "caption"
                    font.family: Theme.monoFont
                    color: Theme.ink
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
