// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import WordJewelsSolver

// What a parent sets (docs/design/dashboard.md, "Settings"). Every value is one
// the table allows; the controller refuses any other, and the store holds the
// result on this device and nowhere else.
Flickable {
    id: root

    required property var controller

    // The parent area's direction, from the shell: Hebrew and Yiddish mirror the
    // rows and every choice.
    LayoutMirroring.enabled: Theme.rightToLeft
    LayoutMirroring.childrenInherit: true

    readonly property var s: controller.settings

    // A group's heading: the same Heading as a Section's, so every group
    // (Reset included) is one level.
    component GroupTitle: Label {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space4
        role: "heading"
        wrapMode: Text.WordWrap
    }

    function onOff() {
        return [{value: true, label: ParentWording.valueOn}, {value: false, label: ParentWording.valueOff}];
    }

    contentWidth: width
    contentHeight: column.implicitHeight + 2 * Theme.space6
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick

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
                text: ParentWording.tabSettings
            }
            Label {
                Layout.fillWidth: true
                role: "body"
                text: ParentWording.settingsSubtitle
                color: Theme.inkSecondary
                wrapMode: Text.WordWrap
            }
        }

        Section {
            Layout.fillWidth: true

            GroupTitle { text: ParentWording.groupPlay }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsHelp
                hint: ParentWording.settingsHelpHint
                SegmentedChoice {
                    title: ParentWording.settingsHelp
                    options: [
                        {value: "off", label: ParentWording.valueOff},
                        {value: "start", label: ParentWording.valueHelpStart},
                        {value: "two", label: ParentWording.valueHelpTwo}
                    ]
                    current: root.s.help
                    onChosen: value => root.controller.setSetting("help", value)
                }
            }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsBoard
                hint: ParentWording.settingsBoardHint
                SegmentedChoice {
                    title: ParentWording.settingsBoard
                    options: [
                        {value: "standard", label: ParentWording.valueStandard},
                        {value: "phonics", label: ParentWording.valuePhonics, enabled: root.controller.phonicsAvailable}
                    ]
                    current: root.s.board
                    onChosen: value => root.controller.setSetting("board", value)
                }
                Label {
                    Layout.fillWidth: true
                    visible: !root.controller.phonicsAvailable
                    role: "caption"
                    text: ParentWording.settingsBoardNoPhonics
                    wrapMode: Text.WordWrap
                }
            }

            SettingRow {
                Layout.fillWidth: true
                visible: root.controller.phonicsAvailable
                title: ParentWording.settingsPhase
                SegmentedChoice {
                    title: ParentWording.settingsPhase
                    options: root.controller.phases.map(name => ({value: name, label: name}))
                    current: root.s.phase
                    onChosen: value => root.controller.setSetting("phase", value)
                }
            }

            SettingRow {
                id: focusRow
                Layout.fillWidth: true
                visible: root.controller.focusChoices.length > 0
                title: ParentWording.settingsFocus
                hint: ParentWording.settingsFocusHint

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space2

                    Repeater {
                        model: root.controller.focusChoices

                        AppButton {
                            size: "min"
                            quiet: true
                            required property string modelData
                            label: modelData
                            compact: true
                            checkable: true
                            checked: root.s.focusSounds.indexOf(modelData) >= 0
                            Accessible.name: ParentWording.settingsFocus + ": " + modelData
                            onClicked: root.controller.toggleFocusSound(modelData)
                        }
                    }
                }

                // How each ticked sound is going, in the words of the dashboard.
                Repeater {
                    model: root.controller.dashboard.sounds.filter(row => row.focus)

                    ColumnLayout {
                        id: tickedRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            Layout.fillWidth: true
                            textFormat: Text.StyledText
                            wrapMode: Text.WordWrap
                            role: "caption"
                            color: Theme.ink
                            text: "<i>" + tickedRow.modelData.letters + "</i>: "
                                  + (tickedRow.modelData.boardsMet > 0
                                         ? ParentWording.onBoardsFoundOn(tickedRow.modelData.boardsMet, tickedRow.modelData.boardsFound)
                                         : ParentWording.notMetYet)
                            Accessible.name: text.replace(/<[^>]*>/g, "")
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: tickedRow.modelData.fewWords
                            wrapMode: Text.WordWrap
                            role: "caption"
                            text: ParentWording.fewWords
                        }
                    }
                }
            }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsBoardSize
                hint: ParentWording.settingsBoardSizeHint
                SegmentedChoice {
                    title: ParentWording.settingsBoardSize
                    options: (root.s.board === "standard" ? [3, 4, 5, 6] : [3, 4]).map(
                        size => ({value: size, label: ParentWording.boardSizeLabel(size)}))
                    current: root.s.boardSize
                    onChosen: value => root.controller.setSetting("boardSize", value)
                }
            }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsTimer
                hint: ParentWording.settingsTimerHint
                SegmentedChoice {
                    title: ParentWording.settingsTimer
                    options: [
                        {value: "off", label: ParentWording.valueOff},
                        {value: "up", label: ParentWording.valueCountUp},
                        {value: "down", label: ParentWording.valueCountDown}
                    ]
                    current: root.s.timer
                    onChosen: value => root.controller.setSetting("timer", value)
                }
            }

            SettingRow {
                Layout.fillWidth: true
                visible: root.s.timer === "down"
                title: ParentWording.settingsTimerLength
                SegmentedChoice {
                    title: ParentWording.settingsTimerLength
                    options: root.controller.timerLengthChoices.map(choice => ({value: choice, label: ParentWording.timerLengthLabel(choice)}))
                    current: root.s.timerLength
                    onChosen: value => root.controller.setSetting("timerLength", value)
                }
            }

            GroupTitle { text: ParentWording.groupSoundTouch }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsSoundItOut
                SegmentedChoice {
                    title: ParentWording.settingsSoundItOut
                    options: [
                        {value: "full", label: ParentWording.valueFull},
                        {value: "word", label: ParentWording.valueWordOnly},
                        {value: "off", label: ParentWording.valueOff}
                    ]
                    current: root.s.soundItOut
                    onChosen: value => root.controller.setSetting("soundItOut", value)
                }
            }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsCelebration
                SegmentedChoice {
                    title: ParentWording.settingsCelebration
                    options: [
                        {value: "full", label: ParentWording.valueFull},
                        {value: "quiet", label: ParentWording.valueQuiet},
                        {value: "off", label: ParentWording.valueOff}
                    ]
                    current: root.s.celebrationSounds
                    onChosen: value => root.controller.setSetting("celebrationSounds", value)
                }
            }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsSound
                SegmentedChoice {
                    title: ParentWording.settingsSound
                    options: root.onOff()
                    current: root.s.sound
                    onChosen: value => root.controller.setSetting("sound", value)
                }
            }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsVibration
                SegmentedChoice {
                    title: ParentWording.settingsVibration
                    options: root.onOff()
                    current: root.s.vibration
                    onChosen: value => root.controller.setSetting("vibration", value)
                }
            }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsShake
                SegmentedChoice {
                    title: ParentWording.settingsShake
                    options: root.onOff()
                    current: root.s.shake
                    onChosen: value => root.controller.setSetting("shake", value)
                }
            }

            GroupTitle { text: ParentWording.groupAppearance }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsMotion
                hint: ParentWording.settingsMotionHint
                SegmentedChoice {
                    title: ParentWording.settingsMotion
                    options: [
                        {value: "system", label: ParentWording.valueSystem},
                        {value: "reduced", label: ParentWording.valueReduced}
                    ]
                    current: root.s.motion
                    onChosen: value => root.controller.setSetting("motion", value)
                }
            }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsColour
                hint: ParentWording.settingsColourHint
                SegmentedChoice {
                    title: ParentWording.settingsColour
                    options: [
                        {value: "system", label: ParentWording.valueSystem},
                        {value: "light", label: ParentWording.valueLight},
                        {value: "dark", label: ParentWording.valueDark}
                    ]
                    current: root.s.colourScheme
                    onChosen: value => root.controller.setSetting("colourScheme", value)
                }
            }

            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsTextSize
                SegmentedChoice {
                    title: ParentWording.settingsTextSize
                    options: [100, 125, 150].map(percent => ({value: percent, label: ParentWording.textSizeLabel(percent)}))
                    current: root.s.textSizePercent
                    onChosen: value => root.controller.setSetting("textSizePercent", value)
                }
            }

            // Offered only if a font whose licence lets it be bundled exists.
            SettingRow {
                Layout.fillWidth: true
                visible: root.controller.schoolFonts.length > 0
                title: ParentWording.settingsSchoolFont
                SegmentedChoice {
                    title: ParentWording.settingsSchoolFont
                    options: [{value: "", label: ParentWording.valueOff}].concat(
                        root.controller.schoolFonts.map(name => ({value: name, label: name})))
                    current: root.s.schoolFont
                    onChosen: value => root.controller.setSetting("schoolFont", value)
                }
            }

            GroupTitle { text: ParentWording.groupLanguage }

            // The interface's language. The board and the words on it are the
            // same English board in every language; the screens around it change,
            // and Hebrew and Yiddish turn the layout around.
            SettingRow {
                Layout.fillWidth: true
                title: ParentWording.settingsLanguage
                hint: ParentWording.settingsLanguageHint
                SegmentedChoice {
                    title: ParentWording.settingsLanguage
                    options: [{value: "", label: ParentWording.valueSystem}].concat(
                        ParentWording.languageChoices)
                    current: root.s.language
                    onChosen: value => root.controller.setSetting("language", value)
                }
            }
        }

        Section {
            Layout.fillWidth: true
            title: ParentWording.resetHeading

            ResetStep {
                Layout.fillWidth: true
                label: ParentWording.resetBoards
                says: ParentWording.resetBoardsSays
                onConfirmed: root.controller.resetBoardsAndFinds()
            }
            ResetStep {
                Layout.fillWidth: true
                label: ParentWording.resetStars
                says: ParentWording.resetStarsSays
                onConfirmed: root.controller.resetStarsAndStickers()
            }
        }
    }
}
