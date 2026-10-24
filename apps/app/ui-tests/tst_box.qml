// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import WordJewelsSolver

// solo.box (apps/app/qml/solo/SoloScreen.qml and SoloBox.qml): the board-size
// choice, the Shake button and Space. The screen is the application's own; the
// 3D board scene it imports is replaced by the harness's stub (apps/app/ui-tests/
// scene_stub), which records the play(board, seed) a shake hands it and settles
// at once - no display and no Quick 3D in a test.
TestCase {
    id: testCase
    name: "Box"
    when: windowShown
    visible: true

    width: 1100
    height: 700

    Component { id: screenComponent; SoloScreen { } }
    Component { id: boxComponent; SoloBox { } }
    Component { id: layoutComponent; SoloLayout { } }
    Component { id: historyComponent; HistorySheet { } }
    Component { id: jewelBoxComponent; JewelBox { } }
    // A game that offers every size, for the segmented row of four.
    Component {
        id: fourSizesComponent
        QtObject {
            property var sizes: [3, 4, 5, 6]
            property int boardSize: 3
            property var history: []
            function chooseSize(size) {
                boardSize = size
            }
        }
    }
    Component { id: ruleStubComponent; QtObject { property int largestOffered: 6 } }

    // The device's motion without hardware: a MotionSource-shaped object the
    // screen can start and stop, whose sample and shake signals a suite emits.
    Component {
        id: motionComponent
        QtObject {
            property bool available: true
            property bool active: false
            property bool shakeEnabled: true
            signal sample(real ax, real ay, real az, real wx, real wy, real wz)
            signal shakeDetected()
        }
    }

    SignalSpy { id: homeSpy; signalName: "homeRequested" }
    SignalSpy { id: cueSpy; target: Feedback; signalName: "cue" }

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

    function soloScreen() {
        const screen = createTemporaryObject(screenComponent, testCase,
                                             { width: 1100, height: 700, solo: soloModel })
        verify(screen !== null)
        return screen
    }

    // The 3D scene is made by an asynchronous Loader, so a test that needs it
    // waits a moment rather than finding the stub before it exists.
    function sceneOf(screen) {
        tryVerify(() => findChild(screen, "boardSceneStub") !== null, 5000)
        return findChild(screen, "boardSceneStub")
    }

    function boxFor(width, height, wide) {
        const rule = createTemporaryObject(layoutComponent, testCase,
                                           { width: width, height: height, boardSize: 3 })
        const box = createTemporaryObject(boxComponent, testCase,
                                          { width: width, height: height, wide: wide,
                                            solo: soloModel, layoutRule: rule })
        verify(box !== null)
        return box
    }

    // The board-size options: the radio buttons of the box's segmented row.
    function sizeButtons(box) {
        return allItems(box).filter(item => item.radio === true && item.checkable === true)
    }

    // The tiles, in order, that spell a word on the board in play.
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
            for (let dr = -1; dr <= 1; ++dr) {
                for (let dc = -1; dc <= 1; ++dc) {
                    const r = Math.floor(at / columns) + dr
                    const c = at % columns + dc
                    if ((dr === 0 && dc === 0) || r < 0 || r >= rows || c < 0 || c >= columns) {
                        continue
                    }
                    const next = r * columns + c
                    if (used.indexOf(next) >= 0) {
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

    // The fake game's five words, found one after another: the last clears it.
    function clearTheBoard() {
        soloModel.shake()
        tryCompare(soloModel, "state", "playing")
        for (const word of ["sit", "sat", "tap", "pat", "nip"]) {
            for (const index of pathFor(word)) {
                soloModel.tapTile(index)
            }
            soloModel.done()
            soloModel.wordShown()
        }
        compare(soloModel.state, "cleared")
    }

    // --- the size choice ----------------------------------------------------
    function test_theSizesOfferedAreTheOnesThatFit() {
        // A window with room for the 3x3 and the 4x4 at 48 dp a tile: both are
        // offered, and the model's own list is what is offered from.
        const box = boxFor(1100, 700, false)
        compare(box.offered.length, 2)
        compare(box.offered[0], 3)
        compare(box.offered[1], 4)
        compare(sizeButtons(box).length, 2)

        // A small window where the 3x3's tile is 54 dp: only it is offered. The
        // 4x4's tile would be 39 dp, so it is not shown - not shown disabled.
        const small = boxFor(300, 300, false)
        compare(small.offered.length, 1)
        compare(small.offered[0], 3)
        compare(small.layoutRule.offers(4), false)
        compare(sizeButtons(small).length, 1)

        // A window too small for even the 3x3 at 48 dp: no size is offered, and
        // Shake is still there.
        const tiny = boxFor(200, 200, false)
        compare(tiny.offered.length, 0)
        compare(sizeButtons(tiny).length, 0)
        verify(buttonNamed(tiny, SoloWording.shake) !== null)
    }

    function test_theBoxPictureFollowsTheChosenSize() {
        const screen = soloScreen()
        const art = allItems(screen).find(item => item.closed === true
                                                  && item.boardSize !== undefined)
        verify(art !== null)
        compare(art.boardSize, 3)
        // The fake game offers 3 and 4; the picture follows the model, not a
        // hard-coded preview.
        soloModel.chooseSize(4)
        compare(art.boardSize, 4)
    }

    function test_aSizeIsChosenByPressingItsPicture() {
        const box = boxFor(1100, 700, false)
        const buttons = sizeButtons(box)
        compare(buttons.length, 2)
        // The size in play is shown pressed in (a state, not colour alone).
        compare(buttons[0].checked, true)
        compare(buttons[1].checked, false)

        mouseClick(buttons[1], buttons[1].width / 2, buttons[1].height / 2)

        compare(soloModel.boardSize, 4)
        compare(buttons[1].checked, true)
        compare(buttons[0].checked, false)
        // The choice is the box's: it does nothing once the dice are out.
        soloModel.shake()
        soloModel.chooseSize(3)
        compare(soloModel.boardSize, 4)
    }

    function test_theBoxIsTheShaperAndTheShakeButtonRunsWithIt() {
        // Wide: Shake across the panel with the sizes beneath it. Tall: the
        // sizes beside it, Shake taking the rest and at least a third wide.
        const wide = boxFor(400, 420, true)
        const wideShake = buttonNamed(wide, SoloWording.shake)
        verify(wideShake !== null)
        compare(wideShake.size, "primary")
        compare(wideShake.height, Theme.targetPrimary)
        compare(wideShake.width, wide.width - 2 * Theme.space4)

        const tall = boxFor(400, 420, false)
        const tallShake = buttonNamed(tall, SoloWording.shake)
        verify(tallShake.width >= tall.width / 3)
        verify(tallShake.height >= Theme.targetPrimary)
    }

    // --- the shake and the scene -------------------------------------------
    function test_shakeDealsABoardAndTheSceneIsGivenIt() {
        const screen = soloScreen()
        const scene = sceneOf(screen)
        verify(scene !== null)
        // The box goes to the scene at start-up, with its lid on: no tiles.
        tryVerify(() => scene.showCount >= 1)
        compare(scene.plays.length, 0)
        compare(soloModel.state, "box")

        const shake = buttonNamed(screen, SoloWording.shake)
        verify(shake !== null)
        mouseClick(shake, shake.width / 2, shake.height / 2)
        compare(soloModel.state, "shaking")

        // The screen asks for the board's faces a frame ahead, then plays it.
        tryCompare(scene, "playCount", 1)
        const played = scene.plays[0]
        compare(played.board.rows, 3)
        compare(played.board.columns, 3)
        compare(played.board.tiles.length, 9)
        compare(played.board.tiles[0], soloModel.tiles[0])
        compare(played.seed, soloModel.seed)
        // And the stub's settle moves the game on, as the real scene's would.
        tryCompare(soloModel, "state", "playing")
        compare(soloModel.rows, 3)
    }

    function test_spaceShakesTheBox() {
        const screen = soloScreen()
        const scene = sceneOf(screen)
        verify(scene !== null)

        screen.forceActiveFocus()
        keyClick(Qt.Key_Space)

        compare(soloModel.state, "shaking")
        tryCompare(scene, "playCount", 1)
        tryCompare(soloModel, "state", "playing")
    }

    function test_aShakeInPlayDoesNothing() {
        const screen = soloScreen()
        const scene = sceneOf(screen)
        verify(scene !== null)
        soloModel.shake()
        tryCompare(soloModel, "state", "playing")

        soloModel.shake()

        compare(soloModel.state, "playing")
        compare(scene.playCount, 1)
    }

    function test_theDevicesMotionDrivesTheSceneAndRollsTheBox() {
        // MotionSource's half of the Seam: while the box needs it, its samples
        // go to the scene's pushMotion, and a detected shake rolls the box as
        // the Shake button does. No sensor: a QML object with the same shape.
        const motion = createTemporaryObject(motionComponent, testCase)
        const screen = createTemporaryObject(screenComponent, testCase,
                                             { width: 1100, height: 700, solo: soloModel,
                                               motionSource: motion })
        verify(screen !== null)
        const scene = sceneOf(screen)
        verify(scene !== null)

        // The box is on screen and the parent allows shaking: the source is
        // running and its own shake setting follows the screen's.
        tryCompare(motion, "active", true)
        compare(motion.shakeEnabled, true)
        compare(scene.motions.length, 0)

        motion.sample(0.1, -0.2, 0.9, 0.01, 0.02, 0.03)
        compare(scene.motions.length, 1)
        compare(scene.motions[0].ax, 0.1)
        compare(scene.motions[0].az, 0.9)
        compare(scene.motions[0].wz, 0.03)

        motion.shakeDetected()
        compare(soloModel.state, "shaking")
        tryCompare(scene, "playCount", 1)
        tryCompare(soloModel, "state", "playing")

        // Play is on: the box no longer needs the sensors, so they stop.
        tryCompare(motion, "active", false)
    }

    function test_theShakeSettingStopsTheSensorsAndTheRoll() {
        // The parent's Shake row off: the source is told, never started, and a
        // shake that arrives anyway does not roll the box. The Shake button
        // and Space are unaffected (test_spaceShakesTheBox).
        const motion = createTemporaryObject(motionComponent, testCase)
        const screen = createTemporaryObject(screenComponent, testCase,
                                             { width: 1100, height: 700, solo: soloModel,
                                               motionSource: motion, shakeEnabled: false })
        verify(screen !== null)

        compare(motion.shakeEnabled, false)
        compare(motion.active, false)
        motion.shakeDetected()
        compare(soloModel.state, "box")
    }

    function test_theLidGoesBackOnWithTheWayBack() {
        // The bar's way back from solo.box asks to leave for home; the screen
        // says so, and the shell takes it home (the suites do not show home).
        const screen = soloScreen()
        homeSpy.target = screen
        homeSpy.clear()

        const back = buttonNamed(screen, SoloWording.back)
        verify(back !== null)
        mouseClick(back, back.width / 2, back.height / 2)

        compare(homeSpy.count, 1)
    }

    function test_backFromPlayLeavesTheRoundInProgress() {
        // Back from a board in play does not put the lid on: the round is left
        // in progress, and the next visit finds the same board.
        const screen = soloScreen()
        homeSpy.target = screen
        homeSpy.clear()
        const back = buttonNamed(screen, SoloWording.back)
        verify(back !== null)

        // Control: the same press from the box reaches the button.
        mouseClick(back, back.width / 2, back.height / 2)
        compare(homeSpy.count, 1)
        homeSpy.clear()

        soloModel.shake()
        tryCompare(soloModel, "state", "playing")
        const tiles = soloModel.tiles

        mouseClick(back, back.width / 2, back.height / 2)
        compare(homeSpy.count, 1)
        compare(soloModel.state, "playing")
        compare(soloModel.tiles, tiles)
    }

    function summaryOf(screen) {
        return allItems(screen).find(item => item.dismissOnScrim === false)
    }

    function test_theFinishSummaryOffersTheWayOn() {
        // Finish board shows the summary: the reward, the words, and Play
        // again as the first offer, which shakes a new board.
        const screen = soloScreen()
        soloModel.shake()
        tryCompare(soloModel, "state", "playing")
        // The panel slides in from its edge; wait until the Finish button is
        // inside the clipped slot, where the press can reach it.
        wait(450)

        const finish = buttonNamed(screen, SoloWording.finishBoard)
        verify(finish !== null)
        mouseClick(finish, finish.width / 2, finish.height / 2)
        compare(screen.finishSummary, true)

        const again = buttonNamed(screen, SoloWording.playAgain)
        verify(again !== null)
        const home = buttonNamed(screen, SoloWording.home)
        verify(home !== null)
        const sheet = summaryOf(screen)
        verify(sheet !== null)
        compare(sheet.open, true)

        // The sheet takes the focus and keeps it: Play again holds it on arrival,
        // Tab walks to Home, and on round the sheet without leaving it.
        tryVerify(() => again.activeFocus)
        keyClick(Qt.Key_Tab)
        verify(home.activeFocus)
        keyClick(Qt.Key_Tab)
        tryVerify(() => sheet.descendantOfSheet(Window.activeFocusItem))

        again.forceActiveFocus()
        mouseClick(again, again.width / 2, again.height / 2)
        compare(screen.finishSummary, false)
        tryCompare(soloModel, "state", "playing")
    }

    function test_theSummaryIsTheSharedSheet() {
        // One scrim rule, one radius, one size cap: the summary is a kit Sheet over
        // the whole window (the bar too), with a title, and Escape closes it.
        const screen = soloScreen()
        soloModel.shake()
        tryCompare(soloModel, "state", "playing")
        screen.finishBoard()
        const sheet = summaryOf(screen)
        verify(sheet !== null)
        compare(sheet.width, screen.width)
        compare(sheet.height, screen.height)
        compare(sheet.title, SoloWording.finishTitle)
        compare(sheet.maxWidth, Theme.modalMax)
        const panel = allItems(sheet).find(item => item.radius === Theme.radiusLarge
                                                   && item.color === Theme.card)
        verify(panel !== null)
        verify(panel.width <= Theme.modalMax)
        verify(panel.height <= Theme.modalMax)

        // The two ways on are the same size.
        const again = buttonNamed(screen, SoloWording.playAgain)
        const home = buttonNamed(screen, SoloWording.home)
        compare(again.height, home.height)
        compare(again.width, home.width)

        // Escape puts the lid back on, as Close does.
        tryVerify(() => sheet.descendantOfSheet(Window.activeFocusItem))
        keyClick(Qt.Key_Escape)
        compare(screen.finishSummary, false)
        tryCompare(soloModel, "state", "box")
    }

    function test_aClearedBoardShowsTheSummaryAndStaysOnIt() {
        // The cleared board is not timed out to a resting screen: the summary is
        // the end of the board, and it waits for a choice (docs/design/game.md's
        // resting screen was never built; the summary replaced it).
        const screen = soloScreen()
        clearTheBoard()
        compare(screen.summaryShown, true)
        const sheet = summaryOf(screen)
        compare(sheet.open, true)
        compare(sheet.title, qsTr("You found all %1 words!").arg(5))

        wait(Theme.clearedHold + 500)
        compare(soloModel.state, "cleared")
        compare(sheet.open, true)

        const again = buttonNamed(screen, SoloWording.playAgain)
        mouseClick(again, again.width / 2, again.height / 2)
        // Play again from a cleared board goes through resting to the box, in one
        // step: a new game is waiting with its lid on.
        tryCompare(soloModel, "state", "box")
        compare(screen.summaryShown, false)
    }

    function test_aClearedBoardPutAwayIsShownAsTheSummaryToo() {
        // A game brought back from the store can open on "resting": the screen
        // shows that board under the same summary, never a blank.
        const screen = soloScreen()
        clearTheBoard()
        soloModel.celebrated()
        compare(soloModel.state, "resting")
        compare(screen.summaryShown, true)
        compare(screen.boardShown, true)
        const sheet = summaryOf(screen)
        compare(sheet.open, true)

        keyClick(Qt.Key_Escape)
        tryCompare(soloModel, "state", "box")
    }

    // --- the box, as the review found it ------------------------------------
    function test_fourSizesAreOneRowOfSixtyFourDpTargets() {
        // 900 x 620: the panel is 300 dp wide. Four sizes fit one row; 6 x 6 does
        // not wrap alone onto a second.
        const game = createTemporaryObject(fourSizesComponent, testCase)
        const rule = createTemporaryObject(ruleStubComponent, testCase)
        const box = createTemporaryObject(boxComponent, testCase,
                                          { width: 300, height: 548, wide: true,
                                            solo: game, layoutRule: rule })
        verify(box !== null)
        wait(50)
        const buttons = sizeButtons(box)
        compare(buttons.length, 4)
        for (const button of buttons) {
            verify(button.height >= Theme.targetChild)
            verify(button.width >= Theme.choiceMin - 0.5)
            compare(button.mapToItem(box, 0, 0).y, buttons[0].mapToItem(box, 0, 0).y)
            verify(button.mapToItem(box, 0, 0).x + button.width <= box.width)
        }
        mouseClick(buttons[3], buttons[3].width / 2, buttons[3].height / 2)
        compare(game.boardSize, 6)
        compare(buttons[3].checked, true)
        // Chosen by a check mark and a fill as well as a colour: the control says
        // which, in words, to assistive technology.
        verify(buttons[3].accessibleName.indexOf("6") >= 0)
    }

    function test_shakeIsAtTheFootAndTheHowItWorksListIsRealItems() {
        const box = boxFor(300, 548, true)
        const shake = buttonNamed(box, SoloWording.shake)
        verify(shake.mapToItem(box, 0, 0).y + shake.height > box.height - 2 * Theme.space6 - Theme.space4)
        const steps = allItems(box).filter(item => item.objectName === "howItem")
        compare(steps.length, 3)
        verify(findChild(box, "howList") !== null)
        // Each step is an item with a number of its own, not spaces in a sentence.
        for (const step of steps) {
            verify(allItems(step).some(item => item.text === String(steps.indexOf(step) + 1)))
        }
    }

    function test_theShakePanelStaysWhileTheBoxShakes() {
        const screen = soloScreen()
        const panel = allItems(screen).find(item => item.busy !== undefined && item.layoutRule !== undefined)
        verify(panel !== null)
        compare(panel.busy, false)
        soloModel.shake()
        compare(soloModel.state, "shaking")
        compare(panel.busy, true)
        // Not blank paper: the panel is there, with its button saying so.
        verify(panel.visible)
        verify(buttonNamed(panel, SoloWording.shaking) !== null)
        compare(buttonNamed(panel, SoloWording.shaking).enabled, false)
    }

    function test_theLidNeverCoversTheCompartments() {
        // The lid stands over the tray's rim, not over a row of the box: every
        // size shows all its rows (a 6 x 6 read as five).
        for (const side of [3, 4, 5, 6]) {
            for (const width of [360, 560]) {
                const art = createTemporaryObject(jewelBoxComponent, testCase,
                                                  { width: width, height: width * 0.8,
                                                    closed: true, showDice: false, boardSize: side })
                verify(art !== null)
                // The lowest the lid reaches below the tray's top edge, counting its
                // tilt, is above the first compartment.
                const tilt = Math.abs(Math.sin(art.lidTilt * Math.PI / 180)) * art.trayWidth / 2
                verify(art.lidOverlap + tilt <= art.gridTop,
                       side + " x " + side + " at " + width + ": the lid reaches "
                       + (art.lidOverlap + tilt) + " dp into the tray, the first row is at " + art.gridTop)
                art.destroy()
            }
        }
    }

    // --- the boards played ----------------------------------------------------
    function test_theBoardsSheetHasATitleAndAWordedEmptyState() {
        const game = createTemporaryObject(fourSizesComponent, testCase)
        const sheet = createTemporaryObject(historyComponent, testCase,
                                            { width: 900, height: 620, solo: game, open: true })
        verify(sheet !== null)
        compare(sheet.title, SoloWording.earlierBoards)
        // No boards: a title and a line, not a picture of nothing.
        const texts = allItems(sheet).filter(item => item.visible === true
                                                     && typeof item.text === "string")
                                     .map(item => item.text)
        verify(texts.indexOf(SoloWording.earlierBoards) >= 0)
        verify(texts.indexOf(SoloWording.noBoardsTitle) >= 0)
        verify(texts.indexOf(SoloWording.noBoardsText) >= 0)
        const panel = allItems(sheet).find(item => item.radius === Theme.radiusLarge
                                                   && item.color === Theme.card)
        verify(panel !== null)

        keyClick(Qt.Key_Escape)
        compare(sheet.open, false)
    }

    function test_aBoardInTheSheetSaysItsSizeAndItsWords() {
        const game = createTemporaryObject(fourSizesComponent, testCase)
        game.history = [{ rows: 3, columns: 3, tiles: ["C", "A", "T", "S", "E", "R", "P", "I", "N"],
                          found: [{ word: "cat", tier: "spark" }, { word: "sat", tier: "spark" }] }]
        const sheet = createTemporaryObject(historyComponent, testCase,
                                            { width: 900, height: 620, solo: game, open: true })
        const texts = allItems(sheet).filter(item => item.visible === true
                                                     && typeof item.text === "string")
                                     .map(item => item.text)
        verify(texts.indexOf(SoloWording.boardCaption(3, 2)) >= 0)
        verify(texts.indexOf(SoloWording.noBoardsTitle) < 0)
    }

    function test_theFlatBoardsLineFollowsThePathAndTheShownWord() {
        const screen = soloScreen()
        const scene = sceneOf(screen)
        verify(scene !== null)
        soloModel.shake()
        tryCompare(soloModel, "state", "playing")

        const board = allItems(screen).find(item => item.linePath !== undefined)
        verify(board !== null)
        compare(board.linePath.length, 0)

        // The path being made is the line, one tile at a time.
        soloModel.tapTile(2)
        soloModel.tapTile(5)
        soloModel.tapTile(8)
        compare(board.linePath.length, 3)
        compare(board.linePath[0], 2)
        compare(board.linePath[2], 8)

        // At the hand-in the line is the found word's path, not a flicker.
        soloModel.done()
        compare(soloModel.state, "word-found")
        compare(board.linePath.length, 3)
    }
}
