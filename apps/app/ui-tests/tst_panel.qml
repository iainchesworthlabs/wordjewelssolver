// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import WordJewelsSolver

// The panel of solo.playing (apps/app/qml/solo/PlayingPanel.qml): the tray of
// words found, the hint, and Done while a tapped path waits. The model is the
// application's SoloModel over the fake game, so the words, tiers and stars are
// the design's test data and the panel is the screen's own file.
TestCase {
    id: testCase
    name: "Panel"
    when: windowShown
    visible: true

    width: 800
    height: 600

    Component { id: panelComponent; PlayingPanel { } }

    SignalSpy { id: finishSpy; signalName: "finishRequested" }
    SignalSpy { id: cueSpy; target: Feedback; signalName: "cue" }

    // The fake's 3x3 board, seed 1: E N S / O A I / M P T. SIT is tiles 2, 5, 8.
    readonly property var sit: [2, 5, 8]

    function init() {
        // A QML warning in a test function fails it, as one outside does the
        // whole run: a screen that warned is a screen that is wrong.
        failOnWarning(/.?/)
        fixture.reset()
        Theme.rightToLeft = false
        Theme.reducedMotion = false
        Theme.textScale = 1.0
    }

    function cleanup() {
        Theme.rightToLeft = false
        Theme.reducedMotion = false
        Theme.textScale = 1.0
    }

    // The tiles, in order, that spell a word on the board in play (diagonals
    // count): what a finger would trace.
    function pathFor(word) {
        const tiles = soloModel.tiles
        const columns = soloModel.columns
        const rows = soloModel.rows
        const upper = word.toUpperCase()
        function walk(at, used, depth) {
            if (tiles[at] !== upper[depth]) {
                return null
            }
            if (depth === upper.length - 1) {
                return used.concat([at])
            }
            const row = Math.floor(at / columns)
            const column = at % columns
            for (let dr = -1; dr <= 1; ++dr) {
                for (let dc = -1; dc <= 1; ++dc) {
                    const r = row + dr
                    const c = column + dc
                    if ((dr === 0 && dc === 0) || r < 0 || r >= rows || c < 0 || c >= columns) {
                        continue
                    }
                    const next = r * columns + c
                    if (used.indexOf(next) >= 0 || next === at) {
                        continue
                    }
                    const found = walk(next, used.concat([at]), depth + 1)
                    if (found !== null) {
                        return found
                    }
                }
            }
            return null
        }
        for (let first = 0; first < tiles.length; ++first) {
            const found = walk(first, [], 0)
            if (found !== null) {
                return found
            }
        }
        return null
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

    function buttonNamed(root, name) {
        for (const item of allItems(root)) {
            // A button's name is its accessibleName, or its label (an ActionBar's).
            if (item.accessibleName === name || item.label === name) {
                return item
            }
        }
        return null
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

    function panel() {
        const panel = createTemporaryObject(panelComponent, testCase,
                                            { width: 280, height: 500, wide: false,
                                              solo: soloModel })
        verify(panel !== null)
        return panel
    }

    function play() {
        soloModel.shake()
        soloModel.sceneSettled()
        compare(soloModel.state, "playing")
    }

    function tapAndHandIn() {
        for (const index of sit) {
            soloModel.tapTile(index)
        }
        soloModel.done()
    }

    function test_theTrayShowsTheWordsTiersAndStars() {
        play()
        tapAndHandIn()
        compare(soloModel.state, "word-found")
        compare(soloModel.found.length, 1)
        const found = soloModel.found[0]
        compare(found.word, "sit")
        compare(found.tier, "spark")
        compare(found.stars, 1)

        const tray = panel()
        // The tray's list is the model's own: one row, the word, and a jewel
        // whose size says the tier (a colour alone never does).
        const list = allItems(tray).find(item => item.count !== undefined
                                                 && item.model !== undefined)
        verify(list !== null)
        compare(list.count, 1)
        compare(list.model.length, 1)
        compare(list.model[0].word, "sit")
        compare(list.model[0].tier, "spark")
        compare(list.model[0].stars, 1)
        verify(textsShown(tray).indexOf("sit") >= 0)

        const jewel = allItems(tray).find(item => item.diameter !== undefined
                                                  && item.tier !== undefined)
        verify(jewel !== null)
        compare(jewel.tier, "spark")
        compare(jewel.diameter, Theme.jewelSpark)
    }

    function test_findingAWordAgainAddsNothing() {
        play()
        tapAndHandIn()
        soloModel.wordShown()
        compare(soloModel.found.length, 1)
        const tray = panel()
        const list = allItems(tray).find(item => item.count !== undefined
                                                 && item.model !== undefined)
        compare(list.count, 1)

        tapAndHandIn()

        // The word is shown again (the screen times it), and the tray is the
        // same tray: a word is one jewel however often it is made.
        compare(soloModel.state, "word-found")
        compare(soloModel.found.length, 1)
        compare(list.count, 1)
    }

    function test_theHintButtonAsksTheModelForAStart() {
        play()
        const tray = panel()
        compare(soloModel.helpTile, -1)

        const hint = buttonNamed(tray, SoloWording.showStart)
        verify(hint !== null)
        compare(hint.visible, true)
        mouseClick(hint, hint.width / 2, hint.height / 2)

        // The fake's help: the start tile of NIP, N at row 0 column 1.
        compare(soloModel.helpTile, 1)
    }

    function test_theHintResolvesWhenItsWordIsFound() {
        play()
        const tray = panel()
        const hint = buttonNamed(tray, SoloWording.showStart)
        verify(hint !== null)
        mouseClick(hint, hint.width / 2, hint.height / 2)
        compare(soloModel.helpTile, 1)

        // NIP, traced from the lit start: the rings go out, and the card says
        // nothing else begins with those letters.
        soloModel.tapTile(1)
        soloModel.tapTile(5)
        soloModel.tapTile(7)
        soloModel.done()

        compare(soloModel.state, "word-found")
        compare(soloModel.hintResolved, true)
        compare(soloModel.hintRemaining, 0)
        compare(soloModel.helpTile, -1)
        verify(textsShown(tray).indexOf(SoloWording.noMoreWordsWithStart) >= 0)
    }

    function test_aTappedPathLeavesTheHintAndThePanelAlone() {
        play()
        const tray = panel()
        compare(tray.pending, false)

        soloModel.tapTile(2)
        soloModel.tapTile(5)

        compare(tray.pending, true)
        compare(tray.helpShown, true)
        // The hint keeps its place; Done is the panel's own, at the foot of the
        // column with Clear, waiting for the path to be handed in.
        compare(buttonNamed(tray, SoloWording.showStart).visible, true)
        const done = buttonNamed(tray, SoloWording.done)
        verify(done !== null)
        compare(done.visible, true)
        compare(done.enabled, true)

        // Done hands the tapped path in, as it always has.
        soloModel.tapTile(8)
        mouseClick(done, done.width / 2, done.height / 2)
        compare(soloModel.path.length, 0)
        compare(soloModel.state, "word-found")
        compare(soloModel.found.length, 1)
        compare(soloModel.found[0].word, "sit")
    }

    function test_theFinishButtonAsksForTheSummary() {
        play()
        const tray = panel()
        finishSpy.target = tray
        finishSpy.clear()

        const button = buttonNamed(tray, SoloWording.finishBoard)
        verify(button !== null)
        mouseClick(button, button.width / 2, button.height / 2)

        compare(finishSpy.count, 1)
    }

    function test_theBoardClockShowsWhatTheModelCounts() {
        soloModel.clockMode = "up"
        play()
        soloModel.clockActive = true
        soloModel.tickClock()

        const tray = panel()
        compare(tray.clockShown, true)
        verify(textsShown(tray).indexOf("0:01") >= 0)
    }

    function test_theTrayHeaderShowsPoints() {
        play()
        tapAndHandIn()
        soloModel.wordShown()
        const tray = panel()
        // SIT is 10 + S1 I1 T1 = 13 points.
        compare(soloModel.boardPoints, 13)
        verify(textsShown(tray).indexOf("13 points") >= 0)
        verify(textsShown(tray).indexOf("1 word") >= 0)
        const list = allItems(tray).find(item => item.count !== undefined
                                                 && item.model !== undefined)
        compare(list.model[0].points, 13)
    }

    // --- the layout the design review broke --------------------------------
    function rectOf(item, root) {
        const at = item.mapToItem(root, 0, 0)
        return { x: at.x, y: at.y, w: item.width, h: item.height }
    }

    function overlap(a, b) {
        return Math.min(a.x + a.w, b.x + b.w) > Math.max(a.x, b.x)
               && Math.min(a.y + a.h, b.y + b.h) > Math.max(a.y, b.y)
    }

    function listOf(tray) {
        return allItems(tray).find(item => item.count !== undefined && item.model !== undefined)
    }

    function test_theWordsCardNeverCollapsesInATallWindow() {
        // 900 x 1200: the panel is the bottom third, 900 x 376. The words and the
        // actions share it side by side; the card was a 4 px sliver when they were
        // stacked.
        play()
        const tall = createTemporaryObject(panelComponent, testCase,
                                           { width: 900, height: 376, wide: false, solo: soloModel })
        verify(tall !== null)
        const card = findChild(tall, "wordsCard")
        verify(card !== null)
        verify(card.height >= 170, "the words card is " + card.height + " dp tall")
        const list = listOf(tall)
        verify(list.height >= 2 * Theme.rowHeight)
        // Whole rows only, so no row is cut under the heading.
        compare(list.height % Theme.rowHeight, 0)
        // The actions are beside the words, not under them.
        const done = buttonNamed(tall, SoloWording.done)
        verify(done.mapToItem(tall, 0, 0).x >= card.x + card.width)
    }

    function test_theWordsCardKeepsItsRoomInTheSmallestWideWindow() {
        // 900 x 620: the panel is 300 x 548.
        play()
        const small = createTemporaryObject(panelComponent, testCase,
                                            { width: 300, height: 548, wide: true, solo: soloModel })
        const card = findChild(small, "wordsCard")
        verify(card.height >= 170, "the words card is " + card.height + " dp tall")
        verify(listOf(small).height >= 2 * Theme.rowHeight)
    }

    function test_noActionClipsInANarrowPanel() {
        play()
        soloModel.tapTile(2)
        soloModel.tapTile(5)
        for (const textScale of [1.0, 1.5]) {
            Theme.textScale = textScale
            const narrow = createTemporaryObject(panelComponent, testCase,
                                                 { width: 300, height: 548, wide: true, solo: soloModel })
            wait(50)
            const buttons = allItems(narrow).filter(item => typeof item.activate === "function"
                                                            && item.visible)
            verify(buttons.length >= 4, "hint, Finish, Clear and Done are all there")
            for (const button of buttons) {
                const at = button.mapToItem(narrow, 0, 0)
                verify(at.x >= -0.5 && at.x + button.width <= narrow.width + 0.5,
                       button.label + " at text " + textScale + " is clipped: "
                       + at.x + " + " + button.width + " of " + narrow.width)
                verify(button.height >= Theme.targetChild - 0.5)
                for (const label of allItems(button).filter(item => item.truncated === true)) {
                    fail(button.label + " is truncated to " + label.text)
                }
            }
            narrow.destroy()
        }
        Theme.textScale = 1.0
    }

    function test_theWordInHandNeverLandsOnTheEmptyState() {
        play()
        const tray = panel()
        const empty = allItems(tray).find(item => item.text === "Your words will land here.")
        verify(empty !== null)
        soloModel.tapTile(0)
        soloModel.tapTile(1)
        const hand = allItems(tray).find(item => item.text === tray.currentWord())
        verify(hand !== null)
        verify(hand.visible)
        verify(empty.visible)
        verify(!overlap(rectOf(hand, tray), rectOf(empty, tray)))
        // And nothing moved because a path began: the line has its height reserved.
        soloModel.clearPath()
        const before = rectOf(empty, tray)
        soloModel.tapTile(0)
        soloModel.tapTile(1)
        compare(rectOf(empty, tray).y, before.y)
    }

    function test_theHintReservesItsRoom() {
        play()
        const tray = panel()
        const done = buttonNamed(tray, SoloWording.done)
        const before = rectOf(done, tray)
        const hint = buttonNamed(tray, SoloWording.showStart)
        mouseClick(hint, hint.width / 2, hint.height / 2)
        // The hint's words appear; the actions under them do not move.
        verify(textsShown(tray).indexOf(SoloWording.startHintShown) >= 0)
        const after = rectOf(buttonNamed(tray, SoloWording.done), tray)
        compare(after.y, before.y)
        compare(after.h, before.h)
    }

    function test_aWordRowIsAKeyboardStopOfTheRightSize() {
        play()
        tapAndHandIn()
        soloModel.wordShown()
        const tray = panel()
        const list = listOf(tray)
        tryVerify(() => list.itemAtIndex(0) !== null)
        const row = list.itemAtIndex(0)
        compare(row.height, Theme.rowHeight)
        verify(row.height >= 48)
        verify(row.activeFocusOnTab)

        // Space on the row says the word, as a touch does.
        cueSpy.clear()
        row.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(cueSpy.count, 1)
        compare(cueSpy.signalArguments[0][0], "speak")
        compare(cueSpy.signalArguments[0][1], "sit")
    }

    function test_theListShowsWholeRowsAndScrollsToTheNewestWord() {
        // Five words, as many as the fake game has, in a card that shows two rows:
        // the newest is in view and the top row is a whole row.
        play()
        for (const word of ["sit", "sat", "tap", "pat", "nip"]) {
            for (const index of pathFor(word)) {
                soloModel.tapTile(index)
            }
            soloModel.done()
            soloModel.wordShown()
        }
        const small = createTemporaryObject(panelComponent, testCase,
                                            { width: 300, height: 548, wide: true, solo: soloModel })
        const list = listOf(small)
        compare(list.count, 5)
        tryVerify(() => list.contentY + list.height >= list.contentHeight - 1)
        compare(list.contentY % Theme.rowHeight, 0)
    }
}
