// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import WordJewelsSolver
import WordJewelsSolver.Parent

// The parent area (apps/app/qml/parent/): the gate, the dashboard, the settings
// and the two resets, driven through the application's own ParentController over
// the demo data's store and phases. The gate's clock is the harness's, so two
// seconds are two seconds however long the machine takes to draw.
TestCase {
    id: testCase
    name: "Parent"
    when: windowShown
    visible: true

    // Room for the settings page without scrolling it: the rows are the point of
    // these tests, and a row outside a clipped Flickable cannot be pressed.
    width: 1200
    height: 2600

    Component { id: areaComponent; ParentArea { } }

    // What the shell binds from the parent's settings (Main.qml): the Text size
    // reaches every screen through the kit's one token set.
    Binding {
        target: Theme
        property: "textScale"
        value: parentController.textScale
    }

    function init() {
        // A QML warning in a test function fails it, as one outside does the
        // whole run: a screen that warned is a screen that is wrong.
        failOnWarning(/.?/)
        fixture.reset()
        Theme.rightToLeft = false
        Theme.reducedMotion = false
    }

    function cleanup() {
        parentController.lock()
        Theme.rightToLeft = false
        Theme.reducedMotion = false
    }

    function allItems(root, found) {
        found = found || []
        found.push(root)
        const kids = root.children
        for (let i = 0; i < kids.length; ++i) {
            allItems(kids[i], found)
        }
        return found
    }

    function textsShown(root) {
        const texts = []
        for (const item of allItems(root)) {
            if (item.visible === true && typeof item.text === "string" && item.text !== "") {
                texts.push(item.text)
            }
        }
        return texts
    }

    // An AppButton: a choice with a label, a checkable state and a role.
    function control(root, text) {
        for (const item of allItems(root)) {
            if (item.label === text && typeof item.checkable === "boolean") {
                return item
            }
        }
        return null
    }

    function row(root, title) {
        for (const item of allItems(root)) {
            if (item.title === title && item.hint !== undefined) {
                return item
            }
        }
        return null
    }

    function area() {
        const area = createTemporaryObject(areaComponent, testCase,
                                           { width: 1200, height: 2600,
                                             controller: parentController })
        verify(area !== null)
        return area
    }

    function press(item) {
        mouseClick(item, item.width / 2, item.height / 2)
    }

    function openGate(area) {
        // The accessibility route, which needs no hold: the two-spot hold itself
        // is test_theGateAsksForTwoSpotsHeld.
        parentController.unlockForAssistiveTechnology()
        // The tabs are made when the gate opens, and the bar's flow places them
        // a frame or two later; a press before that would land on stale geometry.
        wait(200)
        tryVerify(() => control(area, ParentWording.tabSettings) !== null, 5000)
    }

    function openSettings() {
        const view = area()
        openGate(view)
        press(control(view, ParentWording.tabSettings))
        // The page's rows are made and laid out when it loads.
        wait(250)
        tryVerify(() => row(view, ParentWording.settingsHelp) !== null, 5000)
        return view
    }

    // --- the gate -----------------------------------------------------------
    function test_theGateAsksForTwoSpotsHeld() {
        const view = area()
        compare(parentController.unlocked, false)
        const texts = textsShown(view)
        verify(texts.indexOf(ParentWording.gateTitle) >= 0)
        verify(texts.indexOf(ParentWording.gateInstruction) >= 0)
        verify(texts.indexOf(ParentWording.gateKeys) >= 0)
        // Nothing of the data is shown at the gate.
        verify(texts.indexOf(ParentWording.tabDashboard) < 0)

        // The one target replaces the two circles: a press puts both virtual
        // touches down, so the hand and the gate's own logic are one gesture.
        const hold = allItems(view).find(item => item.objectName === "gateHoldTarget")
        verify(hold !== null)

        mousePress(hold, hold.width / 2, hold.height / 2)
        compare(parentController.gateHolding, true)
        fixture.advance(1000)
        parentController.gateTick(parentController.now())
        verify(parentController.gateProgress > 0.4)
        verify(parentController.gateProgress < 0.6)
        compare(parentController.unlocked, false)

        // Letting go starts it over: nothing is lost and nothing opens.
        mouseRelease(hold, hold.width / 2, hold.height / 2)
        compare(parentController.gateHolding, false)
        compare(parentController.gateProgress, 0)
        compare(parentController.unlocked, false)

        // A full hold opens it.
        mousePress(hold, hold.width / 2, hold.height / 2)
        fixture.advance(2100)
        parentController.gateTick(parentController.now())
        compare(parentController.unlocked, true)
        // The gate's screen is replaced by the dashboard; clear the press on
        // the test window rather than on an item that no longer exists.
        mouseRelease(testCase, 0, 0)
    }

    function test_twoSecondsOpensTheGateAndShowsTheDashboard() {
        const view = area()
        const spots = parentController.gateSpots
        verify(spots.length === 2)

        parentController.gateTouchDown(1, spots[0].x, spots[0].y, parentController.now())
        parentController.gateTouchDown(2, spots[1].x, spots[1].y, parentController.now())
        verify(parentController.gateHolding)
        fixture.advance(1000)
        parentController.gateTick(parentController.now())
        compare(parentController.unlocked, false)
        fixture.advance(1000)
        parentController.gateTick(parentController.now())
        compare(parentController.unlocked, true)

        tryVerify(() => control(view, ParentWording.tabDashboard) !== null, 5000)
        const texts = textsShown(view)
        verify(texts.indexOf(ParentWording.tabDashboard) >= 0)
        verify(texts.indexOf(ParentWording.tabSettings) >= 0)
        verify(texts.indexOf(ParentWording.tabAbout) >= 0)
        verify(texts.indexOf(ParentWording.nothingYet) < 0)

        // The numbers are the store's, as the dashboard computed them.
        const report = parentController.dashboard
        const week = report.week
        verify(week.played)
        verify(texts.indexOf(ParentWording.recordsHeading) >= 0)
        verify(texts.indexOf(ParentWording.weekSentence(week.boardsCleared, week.stars,
                                                        week.newWords)) >= 0)
        verify(texts.indexOf(ParentWording.soundsHeading) >= 0)
        verify(report.sounds.length > 0)
        const ai = report.sounds.find(sound => sound.letters === "ai")
        verify(ai !== undefined)
        verify(texts.indexOf(ParentWording.soundSentence(ai.letters, ai.boardsMet,
                                                         ai.boardsFound)) >= 0)
        verify(texts.indexOf(ParentWording.vaultHeading) >= 0)
        verify(texts.indexOf(ParentWording.bestHeading) >= 0)
    }

    function test_anEmptyStoreSaysNothingWasPlayed() {
        fixture.resetParentWithEmptyStore()
        const view = area()
        parentController.unlockForAssistiveTechnology()
        wait(200)
        tryVerify(() => textsShown(view).indexOf(ParentWording.tabDashboard) >= 0, 5000)

        const texts = textsShown(view)
        const report = parentController.dashboard
        // No week, no best, nothing in the vault: the store has nothing in it,
        // and the dashboard does not say a week with no play was anything.
        compare(report.week.played, false)
        verify(texts.indexOf(ParentWording.vaultHeading) < 0)
        verify(texts.indexOf(ParentWording.bestHeading) < 0)
        verify(texts.indexOf(ParentWording.nothingYet) < 0)
        // The phase's sounds are there and say they have not come up.
        verify(report.sounds.length > 0)
        verify(texts.indexOf(ParentWording.notMetYet) >= 0)
        verify(texts.indexOf(ParentWording.soundsHeading) >= 0)
    }

    // --- the settings -------------------------------------------------------
    function test_theSettingsRowsChangeTheController() {
        const view = openSettings()

        // Help: the start tile alone, then off.
        const help = row(view, ParentWording.settingsHelp)
        verify(help !== null)
        press(control(help, ParentWording.valueHelpStart))
        compare(parentController.settings.help, "start")
        compare(parentController.helpLevel, "start")
        press(control(help, ParentWording.valueOff))
        compare(parentController.settings.help, "off")
        press(control(help, ParentWording.valueHelpTwo))
        compare(parentController.settings.help, "two")

        // Board size: 4 x 4, as the game writes it.
        const size = row(view, ParentWording.settingsBoardSize)
        verify(size !== null)
        press(control(size, ParentWording.boardSizeLabel(4)))
        compare(parentController.settings.boardSize, 4)

        // Motion: Reduced, which is what the window applies to Theme.
        const motion = row(view, ParentWording.settingsMotion)
        verify(motion !== null)
        press(control(motion, ParentWording.valueReduced))
        compare(parentController.motion, "reduced")
        compare(parentController.reducedMotion, true)

        // Colour scheme: Dark.
        const colour = row(view, ParentWording.settingsColour)
        verify(colour !== null)
        press(control(colour, ParentWording.valueDark))
        compare(parentController.colourScheme, "dark")

        // Text size: 125%, applied to every screen's type.
        const text = row(view, ParentWording.settingsTextSize)
        verify(text !== null)
        press(control(text, ParentWording.textSizeLabel(125)))
        compare(parentController.settings.textSizePercent, 125)
        compare(parentController.textScale, 1.25)
        compare(Theme.textScale, 1.25)

        // The board's clock: Count down, and its minutes row appears with the
        // choices, one of which the store keeps.
        const timer = row(view, ParentWording.settingsTimer)
        verify(timer !== null)
        press(control(timer, ParentWording.valueCountDown))
        compare(parentController.settings.timer, "down")
        wait(250)
        const minutes = row(view, ParentWording.settingsTimerLength)
        verify(minutes !== null)
        press(control(minutes, ParentWording.timerLengthLabel(10)))
        compare(parentController.settings.timerLength, 10)

        // And off again puts the minutes row away; the minutes stay as chosen.
        press(control(timer, ParentWording.valueOff))
        compare(parentController.settings.timer, "off")
        compare(parentController.settings.timerLength, 10)
    }

    function test_aFocusSoundIsTickedAndUnticked() {
        const view = openSettings()
        const focus = row(view, ParentWording.settingsFocus)
        verify(focus !== null)
        // The demo phase B has ai and z, both ticked by the demo store.
        compare(parentController.settings.focusSounds.indexOf("ai") >= 0, true)
        const z = control(focus, "z")
        verify(z !== null)
        compare(z.checked, true)

        press(z)

        compare(parentController.settings.focusSounds.indexOf("z") < 0, true)
        compare(parentController.settings.focusSounds.indexOf("ai") >= 0, true)
        compare(z.checked, false)
    }

    function test_theResetAsksTwiceAndClearsTheBoards() {
        const view = openSettings()
        const step = allItems(view).find(item => item.label === ParentWording.resetBoards
                                                 && item.says !== undefined)
        verify(step !== null)
        verify(parentController.dashboard.week.played)

        // The first press only asks: nothing is cleared.
        press(control(step, ParentWording.resetBoards))
        compare(step.asking, true)
        verify(parentController.dashboard.week.played)

        // The second, deliberate press clears the boards and the finds.
        const confirm = control(step, ParentWording.resetConfirm)
        verify(confirm !== null)
        press(confirm)
        compare(step.done, true)
        compare(step.asking, false)
        const week = parentController.dashboard.week
        compare(week.boardsCleared, 0)
        compare(week.stars, 0)
        // The vault belongs to the other reset (stars and stickers), which this
        // one does not touch: the words it keeps are why the week can still say
        // something after the boards are gone.
        verify(week.newWords > 0)
        verify(parentController.dashboard.vault.length > 0)
    }

    function test_theWayBackShutsTheGateAgain() {
        const view = openSettings()
        const closed = closedSpy
        closed.target = view
        closed.clear()

        press(control(view, ParentWording.back))

        compare(closed.count, 1)
        compare(parentController.unlocked, false)
        // The next visit meets the gate: nothing of the data is on the screen.
        tryVerify(() => textsShown(view).indexOf(ParentWording.gateTitle) >= 0, 5000)
        verify(textsShown(view).indexOf(ParentWording.weekSentence(1, 1, 1)) < 0)
    }

    SignalSpy { id: closedSpy; signalName: "closed" }
}
