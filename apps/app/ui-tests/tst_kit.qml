// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import WordJewelsSolver

// The kit's components (apps/app/qml/kit/) as they are used, not as they are
// drawn: AppButton's three target sizes and its release-inside rule, HoldButton's
// hold, TileFace's two states, the Jewel's tier sizes, the Icon's pictures and
// the Feedback seam's cues. Nothing here reads a pixel: a state is a property,
// which is what a screen binds to.
TestCase {
    id: testCase
    name: "Kit"
    when: windowShown
    // The test window is what the kit is drawn in and where pointer events land:
    // Quick Test's own root is invisible until a suite says otherwise.
    visible: true

    width: 800
    height: 600

    Component { id: buttonComponent; AppButton { } }
    Component { id: holdComponent; HoldButton { } }
    Component { id: tileComponent; TileFace { } }
    Component { id: jewelComponent; Jewel { } }
    Component { id: iconComponent; Icon { } }
    Component { id: labelComponent; Label { } }
    Component { id: eyebrowComponent; Eyebrow { } }
    Component { id: cardComponent; Card { } }
    Component { id: choiceComponent; SegmentedChoice { } }
    Component { id: actionBarComponent; ActionBar { } }
    Component { id: appBarComponent; AppBar { } }
    Component { id: tabletComponent; TabletArt { } }
    Component { id: spyComponent; SignalSpy { } }
    Component {
        id: sheetHostComponent
        Item {
            property alias sheet: theSheet
            property alias outside: outsideButton
            AppButton { id: outsideButton; label: "Outside" }
            Sheet {
                id: theSheet
                anchors.fill: parent
                title: "Boards"
                AppButton { size: "min"; label: "First" }
                AppButton { size: "min"; label: "Second" }
            }
        }
    }

    SignalSpy { id: clickedSpy; signalName: "clicked" }
    SignalSpy { id: activatedSpy; signalName: "activated" }
    SignalSpy { id: cueSpy; target: Feedback; signalName: "cue" }

    function init() {
        // A QML warning in a test function fails it, as one outside does the
        // whole run: a screen that warned is a screen that is wrong.
        failOnWarning(/.?/)
        Theme.colourScheme = "system"
        Theme.textScale = 1.0
        Theme.reducedMotion = false
        Theme.rightToLeft = false
        cueSpy.clear()
    }

    function cleanup() {
        init()
    }

    // --- AppButton ---------------------------------------------------------
    function test_theThreeTargetsAre48_64_96() {
        const min = createTemporaryObject(buttonComponent, testCase, { size: "min" })
        verify(min !== null)
        compare(min.targetHeight, 48)
        compare(min.implicitHeight, Theme.targetMin)
        verify(min.implicitWidth >= min.implicitHeight)

        const child = createTemporaryObject(buttonComponent, testCase, { size: "child" })
        compare(child.targetHeight, 64)
        compare(child.implicitHeight, Theme.targetChild)

        const primary = createTemporaryObject(buttonComponent, testCase, { size: "primary" })
        compare(primary.targetHeight, 96)
        compare(primary.implicitHeight, Theme.targetPrimary)
        // At least a third of the width: the screen that uses it says the width,
        // and the button keeps its target height.
        primary.minimumWidth = 400
        compare(primary.implicitWidth, 400)
    }

    function test_aReleaseInsideIsAClick() {
        const button = createTemporaryObject(buttonComponent, testCase,
                                             { width: 200, height: 64, accessibleName: "Shake" })
        clickedSpy.target = button
        clickedSpy.clear()

        mouseClick(button, 100, 32)

        compare(clickedSpy.count, 1)
        compare(button.pressed, false)
    }

    function test_slidingOffBeforeTheReleaseCancels() {
        const button = createTemporaryObject(buttonComponent, testCase,
                                             { width: 200, height: 64 })
        clickedSpy.target = button
        clickedSpy.clear()

        mousePress(button, 20, 20)
        verify(button.pressed)
        mouseMove(button, button.width + 60, 20, 0, Qt.LeftButton)
        compare(button.pressed, false)
        mouseRelease(button, button.width + 60, 20)

        compare(clickedSpy.count, 0)
    }

    function test_aPressSpeaksThePicturesName() {
        const button = createTemporaryObject(buttonComponent, testCase,
                                             { width: 64, height: 64, speaks: "solo" })
        mousePress(button, 32, 32)
        compare(cueSpy.count, 1)
        compare(cueSpy.signalArguments[0][0], "speak")
        compare(cueSpy.signalArguments[0][1], "solo")
        mouseRelease(button, 32, 32)

        // A button with no name is silent: the cue is a press's, not a click's.
        // The click itself still plays the quiet tap of a control.
        const silent = createTemporaryObject(buttonComponent, testCase,
                                             { width: 64, height: 64 })
        cueSpy.clear()
        mouseClick(silent, 32, 32)
        compare(cueSpy.count, 1)
        compare(cueSpy.signalArguments[0][0], "tap")
    }

    function test_spaceActsAsAReleaseInside() {
        const button = createTemporaryObject(buttonComponent, testCase,
                                             { width: 64, height: 64 })
        clickedSpy.target = button
        clickedSpy.clear()
        button.forceActiveFocus()
        tryCompare(button, "activeFocus", true)

        keyClick(Qt.Key_Space)

        compare(clickedSpy.count, 1)
    }

    function test_aDisabledButtonDoesNothing() {
        const button = createTemporaryObject(buttonComponent, testCase,
                                             { width: 200, height: 64, enabled: false })
        clickedSpy.target = button
        clickedSpy.clear()

        mouseClick(button, 100, 32)

        compare(clickedSpy.count, 0)
        // The look is the disabled tokens, never a see-through button (which
        // gave the Done label 2.09:1); a disabled button is not on the Tab path.
        compare(button.opacity, 1.0)
        compare(button.contentColor.toString(), Theme.disabledInk.toString())
        compare(button.activeFocusOnTab, false)
    }

    // --- HoldButton --------------------------------------------------------
    function test_aHoldFillsTheRingAndThenActs() {
        const hold = createTemporaryObject(holdComponent, testCase,
                                           { width: 220, height: 64, holdMs: 200 })
        activatedSpy.target = hold
        activatedSpy.clear()
        compare(hold.progress, 0)

        mousePress(hold, 110, 32)

        // Part way: the ring is filling but has not finished, and nothing has
        // happened yet. Nothing is lost by letting go here.
        tryVerify(() => hold.progress > 0)
        tryVerify(() => hold.progress < 1)
        verify(hold.holding)
        compare(activatedSpy.count, 0)

        tryCompare(activatedSpy, "count", 1, 5000)
        compare(hold.progress, 0)
        compare(hold.holding, false)
        mouseRelease(hold, 110, 32)
    }

    function test_lettingGoEarlyStartsTheRingOver() {
        const hold = createTemporaryObject(holdComponent, testCase,
                                           { width: 220, height: 64, holdMs: 300 })
        activatedSpy.target = hold
        activatedSpy.clear()

        mousePress(hold, 110, 32)
        tryVerify(() => hold.progress > 0)
        mouseRelease(hold, 110, 32)

        compare(hold.progress, 0)
        compare(hold.holding, false)
        compare(activatedSpy.count, 0)
    }

    function test_theHoldIsAlsoASwitchAction() {
        // A screen reader or a switch activates it without holding (the
        // accessibility action), which is the same signal.
        const hold = createTemporaryObject(holdComponent, testCase,
                                           { width: 220, height: 64 })
        activatedSpy.target = hold
        activatedSpy.clear()

        hold.activated()

        compare(activatedSpy.count, 1)
    }

    // --- TileFace ----------------------------------------------------------
    function test_tileFaceAtRestAndLit() {
        const face = createTemporaryObject(tileComponent, testCase,
                                           { width: 100, height: 100, letters: "A" })
        const faceRect = face.children.find(child => child.objectName === "face")
        const letter = face.children.find(child => child.objectName === "letter")
        verify(faceRect !== undefined)
        verify(letter !== undefined)
        compare(face.shown, "A")
        compare(face.scale, 1)
        compare(faceRect.border.width, Theme.rule)
        compare(faceRect.color.toString(), Theme.die.toString())
        compare(letter.text, "A")
        compare(letter.color.toString(), Theme.dieInk.toString())

        face.lit = true

        // Lit is a shape as well as a colour: the edge turns gold and thickens.
        // The tile keeps its size so the letter over the path cannot drift with
        // the face's scale; the face and its letter stay readable.
        compare(face.scale, 1)
        compare(faceRect.border.width, Theme.litEdge)
        compare(faceRect.border.color.toString(), Theme.gold.toString())
        compare(faceRect.color.toString(), Theme.die.toString())
        compare(letter.color.toString(), Theme.dieInk.toString())
    }

    function test_theQuTileShowsTwoLettersOnOneFace() {
        const face = createTemporaryObject(tileComponent, testCase,
                                           { width: 100, height: 100, letters: "QU" })
        compare(face.shown, "Qu")
        face.letters = "Z"
        compare(face.shown, "Z")

        // The Qu face draws smaller so that both letters fit.
        const qu = createTemporaryObject(tileComponent, testCase,
                                         { width: 100, height: 100, letters: "QU" })
        const z = createTemporaryObject(tileComponent, testCase,
                                        { width: 100, height: 100, letters: "Z" })
        const quLetter = qu.children.find(child => child.objectName === "letter")
        const zLetter = z.children.find(child => child.objectName === "letter")
        verify(quLetter.font.pixelSize < zLetter.font.pixelSize)
    }

    // --- Jewel --------------------------------------------------------------
    function test_theJewelsSizeSaysItsTier() {
        const jewel = createTemporaryObject(jewelComponent, testCase)
        compare(jewel.tier, "spark")
        compare(jewel.diameter, Theme.jewelSpark)
        compare(jewel.width, Theme.jewelSpark)

        jewel.tier = "power"
        compare(jewel.diameter, Theme.jewelPower)
        jewel.tier = "mega"
        compare(jewel.diameter, Theme.jewelMega)

        // The colour is decoration and comes round the list, so nothing reads a
        // tier from it: the same tier in another place is another colour.
        jewel.colorIndex = 2
        compare(jewel.color.toString().toLowerCase(), String(Theme.jewelColors[2]).toLowerCase())
        jewel.colorIndex = 7  // the list is taken in turn
        compare(jewel.color.toString().toLowerCase(), String(Theme.jewelColors[2]).toLowerCase())
    }

    // --- Icon ---------------------------------------------------------------
    function test_theIconsPictures() {
        const icon = createTemporaryObject(iconComponent, testCase, { name: "back" })
        compare(icon.implicitWidth, 24)
        compare(icon.implicitHeight, 24)
        verify(icon.pathFor("back") !== "")
        verify(icon.mirrorsInRtl)

        // The way back is the one picture that turns round with the layout.
        icon.name = "forward"
        verify(icon.pathFor("forward") !== "")
        verify(icon.mirrorsInRtl, "the way on turns round too")
        for (const name of ["close", "parent", "history", "hint", "done", "again", "shake"]) {
            icon.name = name
            verify(icon.pathFor(name) !== "", name + " has a picture")
            verify(!icon.mirrorsInRtl, name + " does not turn round")
        }

        icon.name = "there-is-no-such-picture"
        compare(icon.pathFor(icon.name), "")
    }

    // --- Feedback ------------------------------------------------------------
    function test_theFeedbackSeamCarriesCues() {
        // Every moment of the design's table brings a cue; nothing plays them
        // yet, so the seam is all there is to hold down.
        Feedback.cue("rattle", "")
        Feedback.cue("tile", "A")
        Feedback.cue("word", "sat")
        compare(cueSpy.count, 3)
        compare(cueSpy.signalArguments[0][0], "rattle")
        compare(cueSpy.signalArguments[1][0], "tile")
        compare(cueSpy.signalArguments[1][1], "A")
        compare(cueSpy.signalArguments[2][0], "word")
        compare(cueSpy.signalArguments[2][1], "sat")
    }

    // --- AppButton: states ----------------------------------------------------
    function test_aChosenButtonIsFillEdgeAndCheckNotGold() {
        const button = createTemporaryObject(buttonComponent, testCase,
                                             { size: "min", label: "3 x 3", checkable: true, radio: true })
        compare(button.showChoice, false)
        compare(button.Accessible.role, Accessible.RadioButton)
        button.checked = true
        compare(button.showChoice, true)
        compare(button.contentColor.toString(), Theme.selectedInk.toString())
        compare(button.Accessible.checked, true)
        // The soft emerald fill and the emerald edge are the tokens; gold is
        // nowhere in a chosen button.
        const face = button.children.find(child => child.radius === button.radius && child.border !== undefined)
        verify(face !== undefined)
        compare(face.color.toString(), Theme.selectedFill.toString())
        compare(face.border.color.toString(), Theme.selectedEdge.toString())
        compare(face.border.width, Theme.heavy)
        verify(face.border.color.toString() !== Theme.gold.toString())
    }

    function test_aButtonWrapsItsLabelAndNeverFallsBelow48() {
        const button = createTemporaryObject(buttonComponent, testCase,
                                             { size: "min", label: "Reset everything and start again", width: 120 })
        verify(button.height >= Theme.targetMin)
        verify(button.implicitHeight >= Theme.targetMin)
        verify(button.implicitHeight > Theme.targetMin, "a long label makes the button taller, not truncated")

        const compact = createTemporaryObject(buttonComponent, testCase, { size: "min", compact: true, label: "ou" })
        compare(compact.implicitHeight, Theme.targetMin)
        verify(compact.implicitWidth >= Theme.targetMin)
    }

    function test_focusIsShownForTheKeyboardAndNotAfterAPress() {
        const button = createTemporaryObject(buttonComponent, testCase, { width: 200, height: 64, label: "Go" })
        button.forceActiveFocus()
        tryCompare(button, "activeFocus", true)
        compare(button.showFocus, true)
        mousePress(button, 100, 32)
        compare(button.showFocus, false)
        mouseRelease(button, 100, 32)
        keyClick(Qt.Key_Shift)
        compare(button.showFocus, true)
    }

    function test_aQuietButtonDoesNotSpeak() {
        const button = createTemporaryObject(buttonComponent, testCase,
                                             { size: "min", label: "Settings", quiet: true, width: 120, height: 48 })
        cueSpy.clear()
        mousePress(button, 60, 24)
        compare(cueSpy.count, 0)
        mouseRelease(button, 60, 24)
    }

    // --- Label ------------------------------------------------------------------
    function test_everyLabelRoleSetsItsFaceSizeAndLineHeight() {
        const faces = {display: "Archivo", title: "Archivo", heading: "Archivo", body: "Archivo",
                       label: "Archivo", child: "Andika", word: "Andika", mono: "IBM Plex Mono", caption: "Archivo"}
        for (const role in faces) {
            const label = createTemporaryObject(labelComponent, testCase, { role: role, text: "Aa" })
            compare(label.font.family, faces[role], role + " face")
            verify(Theme.typeScale.indexOf(label.font.pixelSize) >= 0, role + " size " + label.font.pixelSize + " is on the scale")
            verify(label.lineRatio >= 1.0 && label.lineRatio <= 1.5, role + " has a line height ratio")
            compare(label.lineHeight, label.lineRatio * label.size, role + " line height is that ratio of its size")
            verify(label.font.pixelSize >= 12)
        }
    }

    function test_aLabelFollowsTheTextSizeAndLearningSetsAndika() {
        const body = createTemporaryObject(labelComponent, testCase, { role: "body", text: "x" })
        compare(body.font.pixelSize, 16)
        Theme.textScale = 1.5
        compare(body.font.pixelSize, 24)
        Theme.textScale = 1.0
        body.learning = true
        compare(body.font.family, "Andika")
    }

    function test_trackingIsInEmAndNotAppliedToARightToLeftLanguage() {
        const mono = createTemporaryObject(labelComponent, testCase, { role: "mono", text: "Play" })
        compare(mono.font.capitalization, Font.AllUppercase)
        verify(Math.abs(mono.font.letterSpacing - 0.08 * 12) < 0.02, "0.08 em of 12 px")
        Theme.textScale = 1.5
        verify(Math.abs(mono.font.letterSpacing - 0.08 * 18) < 0.02, "it scales with the type")
        Theme.textScale = 1.0
        const display = createTemporaryObject(labelComponent, testCase, { role: "display", text: "Shake." })
        verify(Math.abs(display.font.letterSpacing - (-0.02 * 40)) < 0.02, "-0.02 em of 40 px")
        Theme.rightToLeft = true
        compare(mono.font.letterSpacing, 0)
        verify(display.font.letterSpacing < 0, "tight display type is kept")
    }

    function test_anEyebrowIsCapitalsByFontNotBySource() {
        const eyebrow = createTemporaryObject(eyebrowComponent, testCase, { text: "For grown-ups" })
        compare(eyebrow.text, "For grown-ups")
        compare(eyebrow.font.capitalization, Font.AllUppercase)
        compare(eyebrow.color.toString(), Theme.inkSecondary.toString())
        eyebrow.accented = true
        compare(eyebrow.color.toString(), Theme.accent.toString())
    }

    function test_headingsAreHeadingsToAssistiveTechnology() {
        const title = createTemporaryObject(labelComponent, testCase, { role: "title", text: "Settings" })
        compare(title.Accessible.role, Accessible.Heading)
        const body = createTemporaryObject(labelComponent, testCase, { role: "body", text: "x" })
        compare(body.Accessible.role, Accessible.StaticText)
        const quiet = createTemporaryObject(labelComponent, testCase, { role: "body", text: "x", decorative: true })
        compare(quiet.Accessible.ignored, true)
    }

    // --- Card -------------------------------------------------------------------
    function test_aCardHasATitleAndAnEmptyState() {
        const card = createTemporaryObject(cardComponent, testCase,
                                           { width: 300, height: 220, title: "Words I found",
                                             emptyTitle: "Your words will land here.", empty: true })
        compare(card.Accessible.role, Accessible.Grouping)
        compare(card.Accessible.name, "Words I found")
        compare(card.radius, Theme.radius)
        compare(card.border.color.toString(), Theme.line.toString())
        const texts = []
        const walk = item => {
            if (item.visible && typeof item.text === "string") {
                texts.push(item.text)
            }
            for (const child of item.children) {
                walk(child)
            }
        }
        walk(card)
        verify(texts.indexOf("Words I found") >= 0)
        verify(texts.indexOf("Your words will land here.") >= 0)
        card.empty = false
        texts.length = 0
        walk(card)
        verify(texts.indexOf("Your words will land here.") < 0, "the empty state goes when there is content")
    }

    // --- SegmentedChoice -----------------------------------------------------
    function test_aSegmentedChoiceIsAGroupOfRadioButtons() {
        const choice = createTemporaryObject(choiceComponent, testCase,
            { width: 400, title: "Help", current: "off",
              options: [{value: "off", label: "Off"}, {value: "start", label: "A start tile"},
                        {value: "two", label: "Two letters", enabled: false}] })
        compare(choice.Accessible.role, Accessible.Grouping)
        compare(choice.Accessible.name, "Help")
        const buttons = choice.children.filter(child => typeof child.checkable === "boolean")
        compare(buttons.length, 3)
        compare(buttons[0].checked, true)
        compare(buttons[1].checked, false)
        compare(buttons[0].Accessible.role, Accessible.RadioButton)
        compare(buttons[1].Accessible.name, "Help: A start tile")
        compare(buttons[2].enabled, false)
        verify(buttons[0].height >= Theme.targetMin)

        const chosenSpy = createTemporaryObject(spyComponent, testCase, { target: choice, signalName: "chosen" })
        mouseClick(buttons[1], buttons[1].width / 2, buttons[1].height / 2)
        compare(chosenSpy.count, 1)
        compare(chosenSpy.signalArguments[0][0], "start")
        mouseClick(buttons[2], 5, 5)
        compare(chosenSpy.count, 1)
    }

    // --- ActionBar ---------------------------------------------------------------
    // The buttons of a component, wherever it nests them.
    function buttonsIn(root) {
        const found = []
        const walk = item => {
            if (typeof item.checkable === "boolean" && item.label !== undefined) {
                found.push(item)
                return
            }
            for (const child of item.children) {
                walk(child)
            }
        }
        walk(root)
        return found
    }

    function test_theActionBarKeepsBothButtonsInANarrowPanel() {
        const bar = createTemporaryObject(actionBarComponent, testCase,
            { width: 244, primaryLabel: "Done", primaryIcon: "done", secondaryLabel: "Clear" })
        const buttons = buttonsIn(bar)
        compare(buttons.length, 2)
        compare(buttons[0].implicitHeight, Theme.targetChild, "secondary: 64")
        compare(buttons[1].implicitHeight, Theme.targetPrimary, "primary: 96")
        for (const button of buttons) {
            verify(button.width >= Theme.targetChild, "never narrower than 64: " + button.width)
            verify(button.x >= 0 && button.x + button.width <= 244 + 0.5, "inside the panel: " + button.x + "+" + button.width)
        }
        compare(bar.primarySize, "primary")
        // However it lays them out - a row, or one under the other when the row
        // does not fit - nothing is truncated.
        for (const button of buttons) {
            const texts = []
            const walk = item => {
                if (item.truncated === true) {
                    texts.push(item.text)
                }
                for (const child of item.children) {
                    walk(child)
                }
            }
            walk(button)
            compare(texts.length, 0, "truncated: " + texts.join())
        }

        bar.tier = "parent"
        compare(bar.primarySize, "child")
        compare(bar.secondarySize, "min")
    }

    function test_aLongLabelWrapsInAnActionBarInsteadOfBeingCut() {
        // A long translation, or 150% text, in a narrow panel: the primary's label
        // is wider than the panel, and the button grows to hold it on two lines.
        const bar = createTemporaryObject(actionBarComponent, testCase,
            { width: 200, primaryLabel: "Commencer à jouer maintenant", primaryIcon: "done",
              secondaryLabel: "Recommencer" })
        const buttons = buttonsIn(bar)
        compare(buttons.length, 2)
        for (const button of buttons) {
            const cut = []
            const walk = item => {
                if (item.truncated === true) {
                    cut.push(item.text)
                }
                for (const child of item.children) {
                    walk(child)
                }
            }
            walk(button)
            compare(cut.length, 0, "cut: " + cut.join())
            verify(button.x >= -0.5 && button.x + button.width <= 200.5, "inside: " + button.x + "+" + button.width)
        }
        verify(buttons[1].implicitHeight >= Theme.targetPrimary)
    }

    function test_anActionBarStacksWhenItsButtonsDoNotFitOneRow() {
        const bar = createTemporaryObject(actionBarComponent, testCase,
            { width: 2000, primaryLabel: "Done", primaryIcon: "done", secondaryLabel: "Clear" })
        tryCompare(bar, "stacked", false)
        const wide = buttonsIn(bar)
        verify(Math.abs(wide[0].y + wide[0].height / 2 - (wide[1].y + wide[1].height / 2)) < 40, "side by side")

        bar.width = bar.naturalWidth - 20
        tryCompare(bar, "stacked", true)
        const buttons = buttonsIn(bar)
        // The layout follows on its next polish.
        tryVerify(() => buttons[1].y >= buttons[0].y + buttons[0].height, 2000, "one under the other, the primary last")
        for (const button of buttons) {
            verify(button.x >= -0.5 && button.x + button.width <= bar.width + 0.5, "inside: " + button.x + "+" + button.width)
        }
        verify(bar.implicitHeight >= buttons[0].height + buttons[1].height)
    }

    function test_theActionBarsButtonsEmitTheirSignals() {
        const bar = createTemporaryObject(actionBarComponent, testCase,
            { width: 400, primaryLabel: "Done", secondaryLabel: "Clear" })
        const primary = createTemporaryObject(spyComponent, testCase, { target: bar, signalName: "primaryClicked" })
        const secondary = createTemporaryObject(spyComponent, testCase, { target: bar, signalName: "secondaryClicked" })
        const buttons = buttonsIn(bar)
        mouseClick(buttons[1], 10, 10)
        compare(primary.count, 1)
        compare(secondary.count, 0)
        mouseClick(buttons[0], 10, 10)
        compare(secondary.count, 1)
    }

    // --- Sheet --------------------------------------------------------------------
    function test_aSheetClosesOnEscapeAndHasATitle() {
        const host = createTemporaryObject(sheetHostComponent, testCase, { width: 600, height: 400 })
        const sheet = host.sheet
        compare(sheet.visible, false)
        sheet.open = true
        compare(sheet.visible, true)
        tryCompare(sheet, "activeFocus", true)

        const closeSpy = createTemporaryObject(spyComponent, testCase, { target: sheet, signalName: "close" })
        keyClick(Qt.Key_Escape)
        compare(closeSpy.count, 1)
    }

    function test_aSheetTrapsTabAndGivesFocusBack() {
        const host = createTemporaryObject(sheetHostComponent, testCase, { width: 600, height: 400 })
        host.outside.forceActiveFocus()
        tryCompare(host.outside, "activeFocus", true)
        host.sheet.open = true
        tryCompare(host.sheet, "activeFocus", true)

        // Tab never leaves the sheet: wherever six presses land, it is inside it.
        for (let i = 0; i < 6; ++i) {
            keyClick(Qt.Key_Tab)
            // The sheet puts the focus back on the next turn of the event loop.
            tryVerify(() => host.sheet.descendantOfSheet(host.Window.activeFocusItem), 1000, "after Tab " + (i + 1))
        }
        for (let i = 0; i < 6; ++i) {
            keyClick(Qt.Key_Backtab)
            tryVerify(() => host.sheet.descendantOfSheet(host.Window.activeFocusItem), 1000, "after Shift+Tab " + (i + 1))
        }
        verify(!host.outside.activeFocus)

        host.sheet.open = false
        tryCompare(host.outside, "activeFocus", true)
    }

    function test_aSheetsScrimClosesItAndThePanelDoesNot() {
        const host = createTemporaryObject(sheetHostComponent, testCase, { width: 600, height: 400 })
        host.sheet.open = true
        const closeSpy = createTemporaryObject(spyComponent, testCase, { target: host.sheet, signalName: "close" })
        mouseClick(host, 5, 5)
        compare(closeSpy.count, 1)
        // The panel is centred: a press in the middle is not the scrim's.
        mouseClick(host, 300, 200)
        compare(closeSpy.count, 1)
    }

    // --- AppBar ------------------------------------------------------------------
    function test_theBarCarriesTabsAndAnnouncesTheOneChosen() {
        const bar = createTemporaryObject(appBarComponent, testCase,
            { width: 1200, showWordmark: true, showParent: false, title: "Parent area",
              tabs: [{id: "dashboard", label: "Dashboard"}, {id: "settings", label: "Settings"}],
              currentTab: "settings" })
        const spy = createTemporaryObject(spyComponent, testCase, { target: bar, signalName: "tabSelected" })
        const tabs = []
        const walk = item => {
            if (item.variant === "tab") {
                tabs.push(item)
            }
            for (const child of item.children) {
                walk(child)
            }
        }
        walk(bar)
        compare(tabs.length, 2)
        compare(tabs[0].checked, false)
        compare(tabs[1].checked, true)
        compare(tabs[1].Accessible.role, Accessible.PageTab)
        verify(bar.backItem !== null)
        compare(bar.height, Theme.appBarHeight)
        mouseClick(tabs[0], 10, 10)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "dashboard")
    }

    function test_aCrowdedBarDropsItsTrailingGroupToASecondRow() {
        const bar = createTemporaryObject(appBarComponent, testCase,
            { width: 2000, showWordmark: true, showJewels: true, showBoards: true })
        tryCompare(bar, "stacked", false)
        // Just wide enough for one line at this text size (the fonts, and so the
        // widths, differ from machine to machine): 150% text is wider, so no longer.
        bar.width = bar.naturalWidth + 20
        tryCompare(bar, "stacked", false)
        verify(bar.height >= Theme.appBarHeight)
        const oneLine = bar.height
        Theme.textScale = 1.5
        // Text lays out on the next polish, so the bar follows a frame later.
        tryCompare(bar, "stacked", true)
        verify(bar.height > oneLine + Theme.targetMin)
        Theme.textScale = 1.0
        tryCompare(bar, "stacked", false)
        tryCompare(bar, "height", oneLine)
    }

    // --- TabletArt ---------------------------------------------------------------
    function test_theTabletAndItsStandStayTogether() {
        for (const size of [[310, 250], [250, 210], [200, 400], [500, 120]]) {
            const art = createTemporaryObject(tabletComponent, testCase, { width: size[0], height: size[1] })
            verify(art.screenWidth > 0)
            verify(art.topOffset >= -0.001, "starts inside the item: " + size)
            verify(art.topOffset + art.screenHeight + art.standHeight <= art.height + 0.001, "ends inside it: " + size)
            verify(art.screenWidth <= art.width * 0.78 + 0.001)
        }
    }
}
