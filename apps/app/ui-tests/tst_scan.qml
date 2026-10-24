// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtTest
import WordJewelsSolver
import WordJewelsSolver.Scan
import "SizeMatrix.js" as SizeMatrix

// Mode A's way in (apps/app/qml/scan/ScanScreen.qml and CheckScreen.qml): the
// ready screen with no camera and the retry that looks for one, the live view a
// running camera source shows, the typed board, a correction through the letter
// picker (a Sheet over the whole screen), the camera-only Turn, the secondary
// action ("Start over" vs "Scan again"), the stepper, the frame's corner brackets,
// "Start playing" (disabled until the board is whole, and asking first when ringed
// tiles are left) and the typed board's helper text. The
// models are the application's ScanModel and AssistModel; a live camera is never
// started, because on this tree's desktops there is none and the suites are
// headless. The camera path uses the fixture's fake source, which is a real
// CameraSource over a backend that returns no frames.
TestCase {
    id: testCase
    name: "Scan"
    when: windowShown
    visible: true

    width: 900
    height: 700

    Component { id: screenComponent; ScanScreen { } }

    SignalSpy { id: playSpy; signalName: "playRequested" }
    SignalSpy { id: closedSpy; signalName: "closed" }

    function init() {
        // A QML warning in a test function fails it, as one outside does the
        // whole run: a screen that warned is a screen that is wrong.
        failOnWarning(/.?/)
        fixture.reset()
        Theme.rightToLeft = false
        Theme.reducedMotion = false
    }

    function cleanup() {
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

    function screen() {
        const screen = createTemporaryObject(screenComponent, testCase,
                                             { width: 900, height: 700,
                                               scan: scanModel, assist: assistModel })
        verify(screen !== null)
        return screen
    }

    function checkScreen(root) {
        return allItems(root).find(item => item.picked !== undefined
                                            && item.boardRows !== undefined)
    }

    function tiles(root) {
        return allItems(root).filter(item => typeof item.row === "number"
                                              && typeof item.column === "number")
    }

    // A positioner lays its children out in the window's polish, a frame after
    // the model changed, and the check screen's own geometry is resolved over
    // the first few frames as its sub-items arrive. A click before that would be
    // turned into a point on stale geometry, so a suite lets the window draw and
    // then waits for the layout it is about to press rather than for a number of
    // milliseconds alone.
    function waitForTiles(root) {
        wait(150)
        tryVerify(() => {
            const cells = tiles(root)
            const first = cells.find(cell => cell.row === 0 && cell.column === 0)
            const next = cells.find(cell => cell.row === 0 && cell.column === 1)
            const below = cells.find(cell => cell.row === 1 && cell.column === 0)
            return first !== undefined && next !== undefined && below !== undefined
                   && next.x > first.x && next.y === first.y && below.y > first.y
        })
    }

    function waitForLetters(root) {
        wait(100)
        tryVerify(() => {
            const a = buttonNamed(root, "A")
            const b = buttonNamed(root, "B")
            return a !== null && b !== null && b.x > a.x && b.y === a.y
        })
    }

    function fill(letter) {
        for (let index = 0; index < scanModel.rows * scanModel.columns; ++index) {
            scanModel.setTile(index, letter)
        }
    }

    // --- scan.ready ---------------------------------------------------------
    function test_theCameraCodesAreTheDesignsSentences() {
        // CameraSource reports these three; the platform interface's older
        // codes still map to the same sentences. A camera that is not found may
        // still be there, so the absent sentence says so and "Try camera again"
        // follows it (the no-camera test below).
        compare(ScanWording.cameraProblem("no_camera"), ScanWording.cameraAbsent)
        compare(ScanWording.cameraProblem("permission"), ScanWording.cameraRefused)
        compare(ScanWording.cameraProblem("failed"), ScanWording.cameraBroken)
        compare(ScanWording.cameraProblem("unavailable"), ScanWording.cameraAbsent)
        compare(ScanWording.cameraProblem(""), "")
        verify(ScanWording.tryCameraAgain.length > 0)
    }

    function test_readySaysThereIsNoCamera() {
        const view = screen()
        compare(scanModel.state, "ready")
        // Opening the screen starts the camera; this desktop's interface answers
        // "unavailable", and the screen says so in the design's words.
        tryCompare(scanModel, "cameraProblem", "unavailable")
        const texts = textsShown(view)
        verify(texts.indexOf(ScanWording.cameraAbsent) >= 0)
        // There is no Capture without a camera, and the typed board is the way on,
        // the primary action of the screen.
        verify(buttonNamed(view, ScanWording.capture) === null)
        verify(buttonNamed(view, ScanWording.typeBoard).visible)
        // The frame says so with a picture as well as the sentence under it.
        const picture = allItems(view).find(item => item.objectName === "noCameraPicture")
        verify(picture !== undefined)
        verify(picture.visible)
        // The camera's absence can be retried without leaving the screen: the
        // attempt re-enumerates and, with no device, says the same truth again.
        const retry = buttonNamed(view, ScanWording.tryCameraAgain)
        verify(retry !== null)
        verify(retry.visible)
        mouseClick(retry, retry.width / 2, retry.height / 2)
        compare(scanModel.state, "ready")
        compare(scanModel.cameraProblem, "unavailable")
        verify(textsShown(view).indexOf(ScanWording.cameraAbsent) >= 0)
        compare(scanModel.readProblem, "")
        // No live view either: there is no picture to put in the frame.
        compare(view.previewShown, false)
        verify(allItems(view).every(item => item.objectName !== "cameraPreview"))
    }

    // --- scan.ready with a camera -------------------------------------------
    function test_aRunningCameraShowsThePreviewAndOffersCapture() {
        // A fake source, not a device: the preview is created, so the QML path
        // that a camera would take is the one a headless run drives. The frame
        // it would hand over is not needed to show the live view.
        fixture.useCamera()
        const view = screen()
        tryCompare(scanModel, "cameraAvailable", true)
        compare(scanModel.cameraCapturing, true)
        compare(view.previewShown, true)
        verify(allItems(view).some(item => item.objectName === "cameraPreview"))
        verify(buttonNamed(view, ScanWording.capture).visible)
        verify(textsShown(view).indexOf(ScanWording.cameraAbsent) < 0)
        verify(buttonNamed(view, ScanWording.typeBoard).visible)
    }

    // --- scan.ready -> scan.check -------------------------------------------
    function test_typingABoardOpensTheCheckScreen() {
        const view = screen()
        const typeBoard = buttonNamed(view, ScanWording.typeBoard)
        verify(typeBoard !== null)
        mouseClick(typeBoard, typeBoard.width / 2, typeBoard.height / 2)

        compare(scanModel.state, "check")
        compare(view.scanState, "check")
        compare(scanModel.rows, 4)
        compare(scanModel.columns, 4)
        compare(scanModel.draftFromCamera, false)
        const texts = textsShown(view)
        verify(texts.indexOf(ScanWording.checkTitle) >= 0)
        // A typed board was not read, so the secondary action starts a fresh
        // typed board rather than pretending to scan again.
        verify(texts.indexOf(ScanWording.startOver) >= 0)
        verify(texts.indexOf(ScanWording.scanAgain) < 0)
        verify(texts.indexOf(ScanWording.startPlaying) >= 0)
        // Turn is the camera board's control.
        compare(buttonNamed(view, ScanWording.turn).visible, false)
        // The typed board rings nothing: there is no reader's doubt to explain.
        compare(scanModel.doubtful.length, 0)
        verify(texts.indexOf(ScanWording.doubtfulHint) < 0)
    }

    // --- the typed board's blank grid ------------------------------------------
    function test_aBlankTypedBoardSaysWhatToDoAndCannotStart() {
        const view = screen()
        scanModel.typeBoard()
        waitForTiles(view)
        // The blank grid has a helper sentence, and Start playing is not offered
        // until every square has a letter.
        verify(textsShown(view).indexOf(ScanWording.typedHint) >= 0)
        const yes = buttonNamed(view, ScanWording.startPlaying)
        verify(yes !== null)
        compare(yes.enabled, false)

        // One letter in, the sentence is what is still missing.
        scanModel.setTile(0, "A")
        verify(textsShown(view).indexOf(ScanWording.typedHint) < 0)
        verify(textsShown(view).indexOf(ScanWording.incomplete) >= 0)
        compare(yes.enabled, false)

        // Every square: it opens, and the sentence is gone for the question.
        fill("A")
        compare(yes.enabled, true)
        verify(textsShown(view).indexOf(ScanWording.incomplete) < 0)
    }

    // --- the board size ------------------------------------------------------
    function sizeChoices(root) {
        const group = allItems(root).find(item => item.options !== undefined && item.current !== undefined)
        return group
    }

    function test_theSizesOfferedAreTheGamesAndASizeTheReaderCountedIsShown() {
        const view = screen()
        scanModel.typeBoard()
        waitForTiles(view)
        const group = sizeChoices(view)
        verify(group !== undefined)
        // 3x3 to 6x6, the sizes the game plays (PRD 12.7), and the typed board's 4x4
        // is the one chosen.
        compare(group.options.map(option => option.value), [3, 4, 5, 6])
        compare(group.current, 4)

        // The reader counts 3 to 8: a 7x7 it counted is shown, chosen, and can be
        // changed, so the board never has a size the grown-up cannot see.
        verify(scanModel.resize(7, 7))
        wait(50)
        compare(group.options.map(option => option.value), [3, 4, 5, 6, 7])
        compare(group.current, 7)
        verify(scanModel.resize(5, 5))
        wait(50)
        compare(group.options.map(option => option.value), [3, 4, 5, 6])
        compare(group.current, 5)
    }

    // --- the secondary action and its alignment ------------------------------
    function test_typedBoardsStartOverOnAFreshTypedBoard() {
        const view = screen()
        scanModel.typeBoard()
        scanModel.setTile(0, "A")
        waitForTiles(view)
        compare(scanModel.state, "check")

        const again = buttonNamed(view, ScanWording.startOver)
        verify(again !== null)
        verify(again.visible)
        mouseClick(again, again.width / 2, again.height / 2)

        // The typing task stays: same screen, same size, no letters, no camera.
        compare(scanModel.state, "check")
        compare(scanModel.rows, 4)
        compare(scanModel.columns, 4)
        compare(scanModel.draftFromCamera, false)
        compare(scanModel.letters[0], "")
        compare(scanModel.doubtful.length, 0)
        verify(buttonNamed(view, ScanWording.startOver).visible)
        compare(buttonNamed(view, ScanWording.turn).visible, false)
    }

    function test_theCheckScreensActionsAreAlignedAndOnTheTrailingSide() {
        const view = screen()
        scanModel.typeBoard()
        waitForTiles(view)

        const secondary = buttonNamed(view, ScanWording.startOver)
        const primary = buttonNamed(view, ScanWording.startPlaying)
        verify(secondary !== null && primary !== null)
        const secondaryBox = secondary.mapToItem(view, 0, 0)
        const primaryBox = primary.mapToItem(view, 0, 0)
        // The heights are the ladder's: 64 dp for the secondary, 96 dp for the primary.
        compare(secondary.height, Theme.targetChild)
        compare(primary.height, Theme.targetPrimary)
        // One group: either a row on one centre line or a stack on one edge, never
        // a secondary left hanging from the top of the primary.
        const sameLine = Math.abs((secondaryBox.y + secondary.height / 2)
                                  - (primaryBox.y + primary.height / 2)) < 1
        const sameEdge = Math.abs((secondaryBox.x + secondary.width)
                                  - (primaryBox.x + primary.width)) < 1
        verify(sameLine || sameEdge)
        // The secondary comes first in reading order, the primary last.
        verify(primaryBox.x > secondaryBox.x || primaryBox.y > secondaryBox.y)
        // The group is at the trailing side, 16 dp from the window's edge and from
        // the bottom, like Solo Play's panel.
        verify(primaryBox.x + primary.width > view.width / 2)
        compare(view.width - (primaryBox.x + primary.width), Theme.space4)
        // The action zone is inside the check screen, under the stepper, so it is
        // as far from the bottom as it is from the side (the screen's own bottom).
        verify(primaryBox.y + primary.height <= view.height - Theme.space4 + 1)
    }

    function test_rightToLeftKeepsTheActionsOnTheTrailingSide() {
        // Hebrew and Yiddish: the group follows the direction; the replica of
        // the board does not.
        Theme.rightToLeft = true
        const view = screen()
        compare(view.mirrored, true)
        scanModel.typeBoard()
        waitForTiles(view)

        const secondary = buttonNamed(view, ScanWording.startOver)
        const primary = buttonNamed(view, ScanWording.startPlaying)
        verify(secondary !== null && primary !== null)
        const primaryX = primary.mapToItem(view, 0, 0).x
        const secondaryX = secondary.mapToItem(view, 0, 0).x
        verify(primaryX + primary.width < view.width / 2)
        // Secondary first in reading order: right of the primary in a row, above it
        // in a stack.
        verify(secondaryX > primaryX || secondary.mapToItem(view, 0, 0).y < primary.mapToItem(view, 0, 0).y)
        // The tiles still read left to right, as the real board does.
        const first = tiles(view).find(cell => cell.row === 0 && cell.column === 0)
        const next = tiles(view).find(cell => cell.row === 0 && cell.column === 1)
        verify(first !== null && next !== null)
        verify(next.x > first.x)
    }

    function test_scanAgainReturnsACameraBoardToTheFrame() {
        verify(fixture.readDoubtfulBoard())
        compare(scanModel.draftFromCamera, true)
        const view = screen()
        waitForTiles(view)

        const scanAgain = buttonNamed(view, ScanWording.scanAgain)
        verify(scanAgain !== null)
        verify(scanAgain.visible)
        verify(buttonNamed(view, ScanWording.startOver) === null
               || !buttonNamed(view, ScanWording.startOver).visible)
        // Turn is offered for a camera-read board, and only for one.
        verify(buttonNamed(view, ScanWording.turn).visible)
        // A camera board's actions are enabled: its tiles are all read.
        mouseClick(scanAgain, scanAgain.width / 2, scanAgain.height / 2)

        compare(scanModel.state, "ready")
        compare(scanModel.rows, 0)
        compare(scanModel.draftFromCamera, false)
        compare(view.scanState, "ready")
        verify(buttonNamed(view, ScanWording.typeBoard).visible)
    }

    function test_backFromTheCheckScreenReturnsToTheFrame() {
        const view = screen()
        closedSpy.target = view
        closedSpy.clear()
        scanModel.typeBoard()
        compare(view.scanState, "check")

        const back = buttonNamed(view, ScanWording.back)
        verify(back !== null)
        mouseClick(back, back.width / 2, back.height / 2)

        // Back is the step back, not the typing task's Start over: it drops the
        // draft and shows the frame (with no camera, the typed board is there).
        compare(scanModel.state, "ready")
        compare(scanModel.rows, 0)
        compare(closedSpy.count, 0)
        compare(view.scanState, "ready")
    }

    // --- the stepper ---------------------------------------------------------
    function steps(root) {
        return allItems(root).filter(item => item.objectName === "stepLabel")
    }

    function test_theStepperShowsPlaceCheckAndPlay() {
        const view = screen()
        compare(view.step, 1)
        let texts = textsShown(view)
        verify(texts.indexOf(ScanWording.stepPlace) >= 0)
        verify(texts.indexOf(ScanWording.stepCheck) >= 0)
        verify(texts.indexOf(ScanWording.stepPlay) >= 0)

        scanModel.typeBoard()
        compare(view.step, 2)
        texts = textsShown(view)
        verify(texts.indexOf(ScanWording.stepCheck) >= 0)
        verify(texts.indexOf(ScanWording.stepPlay) >= 0)
    }

    function test_theStepperIsReadableAndMovesWithTheStep() {
        const view = screen()
        const labels = steps(view)
        compare(labels.length, 3)
        // Nothing under the 12 px floor, and the labels are the caption size.
        for (const label of labels) {
            verify(label.font.pixelSize >= 12)
        }
        const badges = allItems(view).filter(item => item.objectName === "stepBadge")
        compare(badges.length, 3)
        // The current step's badge is filled (accent); the one ahead is an outline.
        compare(badges[0].color, Theme.accent)
        compare(badges[1].color, Theme.card)

        scanModel.typeBoard()
        // Place is behind: its badge is the chosen look, a soft fill and an edge.
        compare(badges[0].color, Theme.selectedFill)
        compare(badges[1].color, Theme.accent)
        // The stepper sits clear of the bar: 12 dp or more under it.
        const stepper = allItems(view).find(item => item.names !== undefined && item.step !== undefined)
        verify(stepper !== undefined)
        const bar = allItems(view).find(item => item.backItem !== undefined)
        verify(stepper.mapToItem(view, 0, 0).y >= bar.mapToItem(view, 0, bar.height).y + Theme.space3 - 1)
    }

    function frame(root) {
        const found = allItems(root).find(item => item.aspect !== undefined && item.showWell !== undefined)
        return found === undefined ? null : found
    }

    function test_theFrameTakesTheRoomItIsGiven() {
        // A taller window gives a bigger frame, and no empty void is left under it:
        // the frame's bottom is near the messages and the buttons below it.
        const small = screen()
        const smallFrame = frame(small)
        verify(smallFrame !== null)
        const smallWidth = smallFrame.width
        small.destroy()
        const tall = createTemporaryObject(screenComponent, testCase,
                                         { width: 900, height: 1200, scan: scanModel,
                                           assist: assistModel })
        verify(tall !== null)
        tryVerify(() => frame(tall).width > 0)
        verify(frame(tall).height > smallFrame.height)
        verify(frame(tall).width >= smallWidth)
        // Not the old fixed 350 px.
        verify(frame(tall).width > 350)
    }

    // --- the frame's bracket geometry ---------------------------------------
    function test_theCornerBracketsPointIntoTheFrame() {
        const view = screen()
        const corners = allItems(view).filter(item => item.objectName === "frameCorner")
        compare(corners.length, 4)
        for (const corner of corners) {
            const parts = allItems(corner)
            const horizontal = parts.find(item => item.objectName === "cornerHorizontal")
            const vertical = parts.find(item => item.objectName === "cornerVertical")
            verify(horizontal !== undefined && vertical !== undefined)
            // Top corners carry the horizontal bar at the top, bottom corners at
            // the bottom; the vertical bar runs the full side on the outside.
            compare(horizontal.y, corner.onBottom ? corner.height - horizontal.height : 0)
            compare(horizontal.x, 0)
            compare(vertical.x, corner.onRight ? corner.width - vertical.width : 0)
            compare(vertical.y, 0)
            compare(vertical.height, corner.height)
        }
    }

    // --- a correction -------------------------------------------------------
    function test_aCorrectionSetsTheTilesLetter() {
        const view = screen()
        scanModel.typeBoard()
        waitForTiles(view)
        const check = checkScreen(view)
        verify(check !== null)

        const cells = tiles(view)
        compare(cells.length, 16)
        const first = cells.find(cell => cell.row === 0 && cell.column === 0)
        verify(first !== null)
        mouseClick(first, first.width / 2, first.height / 2)

        // The picker opens on that tile and offers A to Z.
        compare(check.picked, 0)
        waitForLetters(view)
        const a = buttonNamed(view, "A")
        verify(a !== null)
        verify(a.visible)
        mouseClick(a, a.width / 2, a.height / 2)

        compare(scanModel.letters[0], "A")
        compare(check.picked, -1)
        compare(scanModel.complete, false)
    }

    function sheetTitled(root, title) {
        return allItems(root).find(item => item.title === title && item.maxWidth !== undefined)
    }

    function test_theSheetsFitEverySize_data() {
        return SizeMatrix.sizes.map(size => ({
            tag: size.w + "x" + size.h + " text " + Math.round(size.scale * 100) + "%",
            w: size.w, h: size.h, scale: size.scale
        }))
    }

    // Each of Mode A's two modals, open, at every size of the matrix: the dialog is
    // inside the window, every control in it is 48 dp or more and none is cut.
    // (The screen under a modal cannot be audited with it open: its controls are
    // under the scrim, and the audit would count that as overlap.)
    function test_theSheetsFitEverySize(data) {
        Theme.textScale = data.scale
        verify(fixture.readDoubtfulBoard())
        const view = createTemporaryObject(screenComponent, testCase,
                                           { width: data.w, height: data.h, scan: scanModel,
                                             assist: assistModel })
        verify(view !== null)
        const check = checkScreen(view)
        wait(150)

        check.picked = 1
        wait(100)
        const picker = sheetTitled(view, ScanWording.pickerTitle)
        compare(picker.open, true)
        let found = SizeMatrix.audit(picker, Theme.targetMin)
        compare(found.length, 0, "picker: " + found.join("\n"))
        check.picked = -1

        check.askingRinged = true
        wait(100)
        const question = sheetTitled(view, ScanWording.ringedTitle)
        compare(question.open, true)
        found = SizeMatrix.audit(question, Theme.targetMin)
        compare(found.length, 0, "question: " + found.join("\n"))
        check.askingRinged = false
    }

    function test_theLetterPickerIsASheetOverTheWholeScreen() {
        const view = screen()
        scanModel.typeBoard()
        waitForTiles(view)
        const check = checkScreen(view)
        const sheet = sheetTitled(view, ScanWording.pickerTitle)
        verify(sheet !== undefined)
        compare(sheet.open, false)

        const first = tiles(view).find(cell => cell.row === 0 && cell.column === 0)
        mouseClick(first, first.width / 2, first.height / 2)
        compare(check.picked, 0)
        compare(sheet.open, true)
        // The scrim is the screen's, not the check screen's: it starts at the top of
        // the window, so the bar and the stepper dim with the rest.
        compare(sheet.mapToItem(view, 0, 0).y, 0)
        compare(sheet.height, view.height)
        compare(sheet.width, view.width)
        // The dialog is sized by its content and is inside the window.
        waitForLetters(view)
        const use = buttonNamed(view, ScanWording.useLetters)
        verify(use !== null)
        const useBox = use.mapToItem(view, 0, 0)
        verify(useBox.y + use.height <= view.height)
        // "Use these" sits on the same line as "Qu" and the field: aligned.
        const qu = buttonNamed(view, ScanWording.pickerQu)
        verify(qu !== null)
        verify(Math.abs((use.mapToItem(view, 0, 0).y + use.height / 2)
                        - (qu.mapToItem(view, 0, 0).y + qu.height / 2)) < 1)

        // Escape closes it, with no letter chosen.
        keyClick(Qt.Key_Escape)
        compare(check.picked, -1)
        compare(sheet.open, false)
        compare(scanModel.letters[0], "")
    }

    // --- the reader's doubts -------------------------------------------------
    function test_aDoubtfulTileIsRingedAndCanBeSettled() {
        // A reading with three tiles the reader could not trust: the check
        // screen rings them and says what a ring means.
        verify(fixture.readDoubtfulBoard())
        compare(scanModel.state, "check")
        compare(scanModel.rows, 2)
        compare(scanModel.doubtful.length, 3)

        const view = screen()
        waitForTiles(view)
        const marks = allItems(view).filter(item => item.visible === true
                                                   && item.text === ScanWording.doubtfulMark)
        compare(marks.length, 3)
        verify(textsShown(view).indexOf(ScanWording.doubtfulHint) >= 0)

        // The picker offers the reader's other thought first: O before A to Z.
        const check = checkScreen(view)
        const doubtfulTile = tiles(view).find(cell => cell.row === 0 && cell.column === 1)
        mouseClick(doubtfulTile, doubtfulTile.width / 2, doubtfulTile.height / 2)
        compare(check.picked, 1)
        waitForLetters(view)
        compare(scanModel.alternatives(1).length, 1)
        const suggestion = buttonNamed(view, "O")
        verify(suggestion !== null)
        mouseClick(suggestion, suggestion.width / 2, suggestion.height / 2)

        // A correction is the grown-up's: the tile's doubt is settled.
        compare(scanModel.letters[1], "O")
        compare(scanModel.doubtful.length, 2)
        compare(scanModel.doubtful.indexOf(1), -1)
    }

    // --- Start playing -------------------------------------------------------
    function test_startPlayingWaitsForAWholeBoard() {
        const view = screen()
        playSpy.target = view
        playSpy.clear()
        scanModel.typeBoard()
        scanModel.setTile(0, "A")
        waitForTiles(view)

        // Disabled, so it cannot be pressed to be refused: the sentence that says
        // why is on the screen.
        const yes = buttonNamed(view, ScanWording.startPlaying)
        verify(yes !== null)
        compare(yes.enabled, false)
        mouseClick(yes, yes.width / 2, yes.height / 2)

        compare(playSpy.count, 0)
        compare(assistModel.active, false)
        compare(scanModel.state, "check")
        verify(textsShown(view).indexOf(ScanWording.incomplete) >= 0)
    }

    function test_startPlayingAsksBeforeItPlaysRingedTiles() {
        // A camera board whose three doubts are not settled: the press asks
        // whether to check them first. "Check them" goes back to the board with
        // nothing played; "Play anyway" is the grown-up's statement that they
        // looked.
        verify(fixture.readDoubtfulBoard())
        // The tile the reader could not read at all is typed in, so the board is
        // whole; the other two doubts stay.
        scanModel.setTile(2, "E")
        compare(scanModel.complete, true)
        verify(scanModel.doubtful.length > 0)
        const view = screen()
        playSpy.target = view
        playSpy.clear()
        waitForTiles(view)
        const check = checkScreen(view)

        const yes = buttonNamed(view, ScanWording.startPlaying)
        compare(yes.enabled, true)
        mouseClick(yes, yes.width / 2, yes.height / 2)
        compare(check.askingRinged, true)
        compare(playSpy.count, 0)
        compare(assistModel.active, false)
        const question = sheetTitled(view, ScanWording.ringedTitle)
        verify(question !== undefined)
        compare(question.open, true)
        // Over the whole screen, like the picker.
        compare(question.mapToItem(view, 0, 0).y, 0)

        const looking = buttonNamed(view, ScanWording.checkThem)
        verify(looking !== null)
        mouseClick(looking, looking.width / 2, looking.height / 2)
        compare(check.askingRinged, false)
        compare(question.open, false)
        compare(playSpy.count, 0)
        compare(scanModel.state, "check")
        verify(scanModel.doubtful.length > 0)

        // Escape on the question is the safe answer too.
        mouseClick(yes, yes.width / 2, yes.height / 2)
        compare(question.open, true)
        keyClick(Qt.Key_Escape)
        compare(question.open, false)
        compare(playSpy.count, 0)

        mouseClick(yes, yes.width / 2, yes.height / 2)
        const anyway = buttonNamed(view, ScanWording.playAnyway)
        verify(anyway !== null)
        mouseClick(anyway, anyway.width / 2, anyway.height / 2)
        compare(check.askingRinged, false)
        // The board was confirmed and handed over, or the assistant said it could
        // not start it: either way the press was answered and nothing is left open.
        verify(playSpy.count === 1 || check.problem === "noGame")
    }

    function test_aBoardWithNoRingsStartsWithoutAsking() {
        const view = screen()
        playSpy.target = view
        playSpy.clear()
        scanModel.typeBoard()
        fill("A")
        waitForTiles(view)
        const check = checkScreen(view)
        const yes = buttonNamed(view, ScanWording.startPlaying)
        mouseClick(yes, yes.width / 2, yes.height / 2)
        compare(check.askingRinged, false)
        compare(playSpy.count, 1)
    }

    function test_startPlayingHandsTheConfirmedBoardToTheAssistant() {
        const view = screen()
        playSpy.target = view
        playSpy.clear()
        scanModel.typeBoard()
        fill("A")
        compare(scanModel.complete, true)
        waitForTiles(view)

        const yes = buttonNamed(view, ScanWording.startPlaying)
        verify(yes !== null)
        mouseClick(yes, yes.width / 2, yes.height / 2)

        compare(playSpy.count, 1)
        verify(assistModel.active)
        compare(assistModel.rows, 4)
        compare(assistModel.columns, 4)
        compare(assistModel.tiles.length, 16)
        compare(assistModel.tiles[0], "A")
        // The shell shows the assistant's screen; the scan screen itself only
        // says the board is confirmed and play can start.
        compare(assistModel.path.length, 0)
    }

    function test_theWayBackLeavesModeAFromTheReadyScreen() {
        const view = screen()
        closedSpy.target = view
        closedSpy.clear()

        const back = buttonNamed(view, ScanWording.back)
        verify(back !== null)
        mouseClick(back, back.width / 2, back.height / 2)

        compare(closedSpy.count, 1)
        compare(scanModel.state, "ready")
    }
}
