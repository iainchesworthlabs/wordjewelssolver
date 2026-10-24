// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import WordJewelsSolver
import WordJewelsSolver.Scan

// The Family Assistant's screen (apps/app/qml/scan/AssistScreen.qml and
// AssistBoard.qml): the board as Solo Play's object (the kit's tray with its gold
// rim, cream dice, the path under the letters), the panel's titled card and its
// empty state, the words found as 48 dp rows, the one help and its ring, the
// action zone on the kit's height ladder and 16 dp from the window's edge, New
// board, Back, and the earlier boards in a Sheet. The model is the application's
// AssistModel over the fixture's test dictionary (SAT, SIT, PIN, NIP, TAP).
//
// TEST DATA: the board is SAT across the top and PIN below it, X for the rest.
TestCase {
    id: testCase
    name: "Assist"
    when: windowShown
    visible: true

    width: 1100
    height: 700

    Component { id: screenComponent; AssistScreen { } }

    SignalSpy { id: closedSpy; signalName: "closed" }
    SignalSpy { id: newBoardSpy; signalName: "newBoardRequested" }

    readonly property var letters: ["S", "A", "T", "X", "P", "I", "N", "X",
                                    "X", "X", "X", "X", "X", "X", "X", "X"]

    function init() {
        failOnWarning(/.?/)
        fixture.reset()
        Theme.rightToLeft = false
        Theme.reducedMotion = true
        Theme.textScale = 1.0
    }

    function cleanup() {
        Theme.rightToLeft = false
        Theme.reducedMotion = false
        Theme.textScale = 1.0
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

    function screen(width, height) {
        verify(assistModel.begin(4, 4, letters))
        const view = createTemporaryObject(screenComponent, testCase,
                                           { width: width || 1100, height: height || 700,
                                             assist: assistModel })
        verify(view !== null)
        wait(100)
        return view
    }

    function itemNamed(root, objectName) {
        return allItems(root).find(item => item.objectName === objectName)
    }

    function find(root, predicate) {
        return allItems(root).find(predicate)
    }

    // SAT across the top: tiles 0, 1, 2.
    function findSat() {
        assistModel.tapTile(0)
        assistModel.tapTile(1)
        assistModel.tapTile(2)
        assistModel.done()
    }

    // --- the board is Solo Play's object ------------------------------------
    function test_theBoardStandsInTheKitsTray() {
        const view = screen()
        const tray = find(view, item => item.showWell !== undefined && item.rim !== undefined)
        verify(tray !== undefined, "the board has the kit's tray")
        verify(tray.visible)
        compare(tray.color, Theme.tray)
        compare(tray.border.color, Theme.trayEdge)
        compare(tray.border.width, Theme.trayRim)
        // The tray surrounds the dice and fits in the board area's inset: it does
        // not run off the area as the old board's did.
        const board = find(view, item => item.coreWidth !== undefined)
        verify(board !== undefined)
        verify(tray.x < 0 && tray.y < 0)
        verify(-tray.x <= board.inset)
        // Sixteen cream dice, and the letters over them in the learning face.
        const faces = allItems(view).filter(item => item.objectName === "face")
        compare(faces.length, 16)
        compare(faces[0].color, Theme.die)
        const lettered = allItems(view).filter(item => item.font !== undefined
                                                         && item.font.family === Theme.fontFamily
                                                         && item.font.weight === Font.Bold
                                                         && letters.indexOf(item.text) >= 0)
        verify(lettered.length >= 16)
    }

    function test_thePathIsAsWideAsTheTileAllowsAndUnderTheLetters() {
        const view = screen()
        const board = find(view, item => item.coreWidth !== undefined)
        verify(board.coreWidth >= Theme.pathCoreWidth)
        verify(board.casingWidth >= Theme.pathCasingWidth)
        // On a tile of about 150 dp the ribbon is a line a finger drew: the share
        // of the tile, not a hair across the letter.
        verify(board.tile > 100)
        compare(board.coreWidth, Math.max(Theme.pathCoreWidth, board.tile * Theme.pathCoreShare))
        assistModel.tapTile(0)
        assistModel.tapTile(1)
        compare(assistModel.path.length, 2)
        // Layers: the shape comes after the faces and before the letters.
        const kids = board.children
        const shapeAt = kids.findIndex(item => item.preferredRendererType !== undefined && item.visible)
        const letterAt = kids.findIndex(item => item.font !== undefined && item.text === "S")
        verify(shapeAt >= 0 && letterAt > shapeAt)
    }

    // --- the panel -----------------------------------------------------------
    function test_theWordsCardHasATitleAndAnEmptyState() {
        const view = screen()
        const card = find(view, item => item.title === ScanWording.wordsFound && item.emptyTitle !== undefined)
        verify(card !== undefined)
        compare(card.empty, true)
        compare(card.color, Theme.card)
        compare(card.radius, Theme.radius)
        const texts = textsShown(view)
        verify(texts.indexOf(ScanWording.wordsFound) >= 0)
        verify(texts.indexOf(ScanWording.noWordsYet) >= 0)
        verify(texts.indexOf(ScanWording.noWordsHint) >= 0)
        // The panel is Solo Play's: the panel colour, on the trailing side.
        const panel = find(view, item => item.color !== undefined && Qt.colorEqual(item.color, Theme.panel)
                                          && item.width === view.panelArea.width)
        verify(panel !== undefined)
        verify(view.panelX > view.width / 2)
    }

    function test_aFoundWordIsAFortyEightDpRowThatTakesTab() {
        const view = screen()
        findSat()
        compare(assistModel.found.length, 1)
        const card = find(view, item => item.title === ScanWording.wordsFound && item.emptyTitle !== undefined)
        compare(card.empty, false)
        wait(50)
        const row = find(view, item => item.modelData !== undefined && item.modelData.word === "sat")
        verify(row !== undefined)
        compare(row.height, Theme.rowHeight)
        compare(row.activeFocusOnTab, true)
        verify(textsShown(view).indexOf("sat") >= 0)
    }

    // --- the start tile ------------------------------------------------------
    function test_theHelpLightsATileWithAGoldRing() {
        const view = screen()
        const ring = itemNamed(view, "startRing")
        verify(ring !== undefined)
        compare(ring.visible, false)

        const help = buttonNamed(view, ScanWording.showStart)
        verify(help !== null)
        verify(help.visible)
        mouseClick(help, help.width / 2, help.height / 2)
        verify(assistModel.litTile >= 0)
        compare(ring.visible, true)
        compare(ring.border.color, Theme.gold)
        // The ring is round the tile the model lit, a few dp outside it.
        const board = find(view, item => item.coreWidth !== undefined)
        compare(ring.width, board.tile + 2 * Theme.startRing)
        compare(ring.x, board.tileX(assistModel.litTile) - Theme.startRing)
    }

    // --- the action zone -----------------------------------------------------
    function test_theActionsAreOnTheLadderAndSixteenDpFromTheEdge() {
        const view = screen()
        const clear = buttonNamed(view, ScanWording.clear)
        const done = buttonNamed(view, ScanWording.done)
        const help = buttonNamed(view, ScanWording.showStart)
        const newBoard = buttonNamed(view, ScanWording.newBoard)
        verify(clear !== null && done !== null && help !== null && newBoard !== null)
        verify(clear.visible && done.visible && help.visible && newBoard.visible)
        // The ladder: Done 96 dp, Clear 64 dp, the help at least 64 dp (its picture sits above
        // its word), the ways out 48 dp.
        compare(done.height, Theme.targetPrimary)
        compare(clear.height, Theme.targetChild)
        verify(help.height >= Theme.targetChild, "the help (a picture above its word): " + help.height)
        compare(newBoard.height, Theme.targetMin)
        // The edge: 16 dp from the window's right edge (not the old 8 dp) and from
        // the bottom.
        const doneBox = done.mapToItem(view, 0, 0)
        const clearBox = clear.mapToItem(view, 0, 0)
        const helpBox = help.mapToItem(view, 0, 0)
        const newBox = newBoard.mapToItem(view, 0, 0)
        const right = Math.max(doneBox.x + done.width, helpBox.x + help.width)
        verify(Math.abs(view.width - right - Theme.space4) < 1)
        verify(Math.abs(view.height - (newBox.y + newBoard.height) - Theme.space4) < 1)
        // Done last in its pair, in a row or a stack.
        verify(doneBox.x > clearBox.x || doneBox.y > clearBox.y)
        // Every visible button has a word: no picture-only button but Back and
        // Close.
        for (const item of allItems(view)) {
            if (item.iconName !== undefined && item.iconName !== "" && item.visible) {
                verify(item.label !== "" || item.iconName === "back" || item.iconName === "close",
                       "icon-only button: " + item.iconName)
            }
        }
    }

    function test_clearAndDoneWaitForAPathAndDoWhatTheySay() {
        const view = screen()
        const clear = buttonNamed(view, ScanWording.clear)
        const done = buttonNamed(view, ScanWording.done)
        // Nothing to hand in or put down yet.
        compare(clear.enabled, false)
        compare(done.enabled, false)

        // A word made by taps: SAT, then Done hands it in (as the Return key did).
        assistModel.tapTile(0)
        assistModel.tapTile(1)
        assistModel.tapTile(2)
        compare(assistModel.path.length, 3)
        compare(clear.enabled, true)
        compare(done.enabled, true)
        mouseClick(done, done.width / 2, done.height / 2)
        compare(assistModel.found.length, 1)
        compare(assistModel.path.length, 0)
        compare(done.enabled, false)

        // A path put down with Clear is not a word.
        assistModel.tapTile(4)
        assistModel.tapTile(5)
        compare(assistModel.path.length, 2)
        mouseClick(clear, clear.width / 2, clear.height / 2)
        compare(assistModel.path.length, 0)
        compare(assistModel.found.length, 1)
    }

    function test_theEarlierBoardsAreAButtonWithAWord() {
        const view = screen()
        const history = buttonNamed(view, ScanWording.earlierBoards)
        verify(history !== null)
        verify(history.visible)
        verify(history.label.length > 0)
        compare(history.iconName, "history")
        verify(history.height >= Theme.targetMin)
    }

    function test_thePanelsColumnIsNoWiderThanAPhonesOnABigWindow() {
        const view = screen(2400, 1200)
        const done = buttonNamed(view, ScanWording.done)
        const help = buttonNamed(view, ScanWording.showStart)
        verify(help.width <= Theme.modalMax + 0.5, "the help: " + help.width)
        verify(done.width <= Theme.modalMax + 0.5, "Done: " + done.width)
        // And it is centred in the panel, not pushed to one edge.
        const box = help.mapToItem(view, 0, 0)
        const panelCentre = view.panelX + view.panelArea.width / 2
        verify(Math.abs(box.x + help.width / 2 - panelCentre) < 1)
    }

    function test_newBoardAsksForANewScan() {
        const view = screen()
        newBoardSpy.target = view
        newBoardSpy.clear()
        const newBoard = buttonNamed(view, ScanWording.newBoard)
        mouseClick(newBoard, newBoard.width / 2, newBoard.height / 2)
        compare(newBoardSpy.count, 1)
    }

    function test_backLeavesTheBoardAndGoesBack() {
        const view = screen()
        closedSpy.target = view
        closedSpy.clear()
        const back = buttonNamed(view, ScanWording.back)
        verify(back !== null)
        mouseClick(back, back.width / 2, back.height / 2)
        compare(closedSpy.count, 1)
    }

    // --- the earlier boards --------------------------------------------------
    function sheet(view) {
        return find(view, item => item.title === ScanWording.earlierBoards && item.maxWidth !== undefined)
    }

    function test_anEmptyHistoryIsASheetThatSaysSo() {
        const view = screen()
        const earlier = sheet(view)
        verify(earlier !== undefined)
        compare(earlier.open, false)
        const button = buttonNamed(view, ScanWording.earlierBoards)
        mouseClick(button, button.width / 2, button.height / 2)
        compare(earlier.open, true)
        // Over the whole window: the bar dims with the rest.
        compare(earlier.mapToItem(view, 0, 0).y, 0)
        compare(earlier.height, view.height)
        const texts = textsShown(view)
        verify(texts.indexOf(ScanWording.historyEmpty) >= 0)
        verify(texts.indexOf(ScanWording.historyEmptyHint) >= 0)
        // The sheet has a title and a radius, as every sheet does.
        const panel = find(earlier, item => item.radius === Theme.radiusLarge && item.width > 200)
        verify(panel !== undefined)
        // Escape closes it; the way back closes it first, and the screen stays.
        keyClick(Qt.Key_Escape)
        compare(earlier.open, false)
        closedSpy.target = view
        closedSpy.clear()
        mouseClick(button, button.width / 2, button.height / 2)
        compare(earlier.open, true)
        view.back()
        compare(earlier.open, false)
        compare(closedSpy.count, 0)
    }

    function test_aBoardLeftIsInTheHistoryWithACaption() {
        verify(assistModel.begin(4, 4, letters))
        findSat()
        assistModel.leave()
        compare(assistModel.history.length, 1)
        const view = screen()
        const earlier = sheet(view)
        const button = buttonNamed(view, ScanWording.earlierBoards)
        mouseClick(button, button.width / 2, button.height / 2)
        compare(earlier.open, true)
        wait(50)
        const caption = ScanWording.historyCaption(4, 1)
        verify(textsShown(view).indexOf(caption) >= 0)
        verify(textsShown(view).indexOf(ScanWording.historyEmpty) < 0)
    }

    // --- the way the screen lays out -----------------------------------------
    function test_aTallWindowPutsThePanelUnderTheBoard() {
        const view = screen(900, 1200)
        compare(view.wide, false)
        const newBoard = buttonNamed(view, ScanWording.newBoard)
        verify(newBoard.mapToItem(view, 0, 0).y > view.height / 2)
        // The column is capped, and centred in the full-width panel.
        const help = buttonNamed(view, ScanWording.showStart)
        verify(help.width <= Theme.modalMax + 0.5)
        const box = help.mapToItem(view, 0, 0)
        verify(Math.abs(box.x + help.width / 2 - view.width / 2) < 1)
    }

    function test_bigTextWrapsTheHelpInsteadOfCuttingIt() {
        Theme.textScale = 1.5
        const view = screen(900, 620)
        const help = buttonNamed(view, ScanWording.showStart)
        verify(help.visible)
        const label = find(help, item => item.text === ScanWording.showStart)
        verify(label !== undefined)
        compare(label.truncated, false)
        // And it is still inside the window.
        const box = help.mapToItem(view, 0, 0)
        verify(box.y + help.height <= view.height)
        verify(box.x + help.width <= view.width)
    }
}
