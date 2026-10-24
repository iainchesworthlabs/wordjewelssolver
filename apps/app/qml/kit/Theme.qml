// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick
import WordJewelsSolver

// The one design system of the application: the Jewel Box. Warm paper, a deep
// tray, cream dice, an emerald action colour and a gold reward. Archivo is the
// house UI face, IBM Plex Mono the label face, Andika the learning face (dice
// letters and words). Every screen, the parent area included, reads these
// tokens; a screen writes no colour, size or type value of its own. The lint
// test in apps/app/ui-tests keeps it so (tst files `lint` and `contrast`).
//
// Light and dark are chosen by the parent's Colour scheme setting, or by the
// system when that is System. The board is the one object that stays warm in
// both: its tray, dice and gold rim are the physical toy on the table.
//
// Colour rules, held by apps/app/ui-tests/tst_contrast.qml against the design's gates
// (text 4.5, large text 3, non-text 3):
//   * Emerald (`accent`) is the one action colour. Selection is `selectedFill`
//     with an `accent` edge and a check mark - never colour alone, never gold.
//   * Gold is reward only: the hint, the lit die, the tray rim, a jewel's rim.
//   * A disabled control uses `disabledFill`/`disabledInk`/`disabledEdge`,
//     never `opacity`, so its label keeps 3:1 or better.
//   * A container is told from its surround by `line` (>= 1.6:1 on a card).
//
// Type rules (docs/design/application.md, "The Jewel Box design system"): the scale is 12,
// 14, 16, 20, 24, 32, 40, 48 and nothing is smaller than 12. Two tiers share it. The CHILD
// tier is `child` (24), `word` (24), `title` (32) and `display` (40): what a
// five-year-old reads. The PARENT tier is `body` (16), `label` (16), `caption`
// (14), `mono` (12) and `heading` (20): what an adult scans. A screen picks a
// role through kit/Label.qml and never a size.
QtObject {
    id: root

    // --- what the person or the system chooses ------------------------------
    property bool reducedMotion: false
    property real textScale: 1.0
    property string colourScheme: "system"
    property bool rightToLeft: false

    readonly property bool dark: colourScheme === "dark"
                                 || (colourScheme !== "light" && SystemColourScheme.dark)

    // --- colour: the chrome (light / dark) ----------------------------------
    // The ladder, darkest to lightest in dark: boardArea < page < panel/surface
    // < card < hoverWash. In light the card is the lightest and the panel the
    // darkest of the paper family.
    readonly property color page: dark ? "#11181B" : "#F6F1E7"
    readonly property color surface: dark ? "#1A2428" : "#FFFDF8"
    readonly property color card: dark ? "#232F35" : "#FFFDF8"
    readonly property color panel: dark ? "#1A2428" : "#EDE7DA"
    // The stage the board stands on. 1.14:1 against the panel in light and
    // 1.19:1 in dark (it was 1.02 and 1.00), and the tray stands off it 1.5:1.
    readonly property color boardArea: dark ? "#0C1214" : "#FAF6EC"
    readonly property color ink: dark ? "#F2ECDE" : "#1E2A30"
    // 4.8:1 on panel and hoverWash in light (the brief's #5C6B70 gave 4.49).
    readonly property color inkSecondary: dark ? "#B3BDBB" : "#56666A"
    // A container's edge: 1.79:1 on a card and 1.62:1 on the page in light
    // (was #D9D1C2: 1.49 and 1.35), 1.74 and 2.27 in dark.
    readonly property color line: dark ? "#445459" : "#C9BFAB"
    readonly property color outline: dark ? "#8C9A98" : "#6E7A78"
    readonly property color accent: dark ? "#63C7B2" : "#166B5C"
    readonly property color accentSoft: dark ? "#1E3A36" : "#DDEDE8"
    readonly property color buttonFill: accent
    readonly property color buttonInk: dark ? "#0E1F1C" : "#FFFFFF"
    // Reward only. One value for both themes, slightly deeper than the brief's
    // #C98A1E so that it holds 3:1 against a cream die (3.11, was 2.76) and
    // against the tray (4.07).
    readonly property color gold: "#BF8019"
    readonly property color goldInk: dark ? "#F0C879" : "#7A4E08"
    readonly property color goldSoft: dark ? "#3A2E1A" : "#F7E8C6"
    readonly property color hoverWash: dark ? "#2C3B41" : "#EDE6D6"
    readonly property color pressWash: dark ? "#36484F" : "#E0D8C4"
    readonly property color focusRing: accent
    readonly property color scrim: Qt.rgba(0.067, 0.094, 0.106, 0.64)

    // A chosen option: a soft emerald fill, an emerald edge and a check mark.
    readonly property color selectedFill: accentSoft
    readonly property color selectedEdge: accent
    readonly property color selectedInk: ink

    // A control that cannot be used now. Its label is 3.6:1 (light) and 4.5:1
    // (dark) on its fill; opacity is not used, so nothing under it shows.
    readonly property color disabledFill: dark ? "#262F34" : "#E8E2D4"
    readonly property color disabledInk: dark ? "#8A9794" : "#6B7770"
    readonly property color disabledEdge: dark ? "#3A474B" : "#D9D1C2"

    // A destructive action: the text/edge colour on a card, and the fill of the
    // confirming button with its ink.
    readonly property color danger: dark ? "#F08A78" : "#A33A2B"
    readonly property color dangerFill: danger
    readonly property color dangerInk: dark ? "#1E0F0C" : "#FFFFFF"

    // An empty progress track, 3:1 against a card.
    readonly property color track: outline

    // The hint's own card. Gold is the reward and the hint, never the chrome.
    readonly property color hintCard: goldSoft
    readonly property color hintCardInk: ink

    // The resting screen is meant to be looked at without wanting anything.
    readonly property color restPage: dark ? "#161B22" : "#F1ECDF"
    readonly property color restShape: dark ? "#2B3542" : "#D9D2BF"
    readonly property color restDetail: dark ? "#3A4757" : "#C7BFA9"

    // --- colour: the board (the same warm object in both themes) ------------
    // The tray is 1.56:1 against the dark board area (it was 1.17 at #17323A).
    readonly property color tray: "#1B3A44"
    readonly property color trayEdge: gold
    readonly property color trayLine: "#2A4A52"
    readonly property color trayShadow: "#0A1113"
    readonly property color lid: "#F7E8C6"
    readonly property color well: "#0D2229"
    readonly property color wellCell: "#1F3F49"
    readonly property color die: "#FFF7E3"
    readonly property color dieInk: "#1F2A30"
    readonly property color dieEdge: "#20323A"
    readonly property color dieLit: "#F7E8C6"
    readonly property color dieLitInk: "#1F2A30"
    readonly property color bevelLight: "#FFFFFF"
    readonly property color bevelShade: "#000000"
    readonly property color pathCasing: gold
    readonly property color pathCore: "#17323A"
    // Solo Play's path is a ribbon under the letters: a gold edge round a pale
    // core, so a dark letter stays dark on pale (14:1) where the line runs through
    // its middle, with a thin dark line outside the gold, so the ribbon holds 3:1
    // on a cream die and on a lit one (gold alone is 3.1 and 2.7) and the gold
    // holds 4:1 on the tray.
    readonly property color ribbonEdge: gold
    readonly property color ribbonCore: dieLit
    readonly property color ribbonLine: dieEdge
    // A jewel not yet found: 3.3:1 on a card (was #8FA1B8: 2.6).
    readonly property color jewelDead: "#7A8DA6"

    // A jewel's colour is decoration: its size says its tier. These are taken
    // in turn by the order a word was found.
    readonly property var jewelColors: ["#E0556B", "#3F9E8F", "#7B6FD6", "#E39A2D", "#4B8BD3"]

    // --- type --------------------------------------------------------------
    readonly property string uiFont: "Archivo"
    readonly property string fontFamily: "Andika"
    readonly property string monoFont: "IBM Plex Mono"

    // Nothing is set smaller than this, at any text scale.
    readonly property real minTextSize: 12
    // The scale, for tests and for anyone who needs the list.
    readonly property var typeScale: [12, 14, 16, 20, 24, 32, 40, 48]

    function rem(amount) {
        return Math.max(minTextSize, amount * 16 * textScale);
    }

    readonly property real monoSize: rem(0.75)          // 12
    readonly property real microSize: monoSize
    readonly property real smallSize: rem(0.875)        // 14
    readonly property real captionSize: smallSize
    readonly property real bodySize: rem(1.0)           // 16
    readonly property real headingSize: rem(1.25)       // 20
    readonly property real labelSize: bodySize          // a parent's button label
    readonly property real wordSize: rem(1.5)           // 24
    readonly property real childLabelSize: rem(1.5)     // 24
    readonly property real titleSize: rem(2.0)          // 32
    readonly property real displaySize: rem(2.5)        // 40
    readonly property real numeralSize: rem(3.0)        // 48

    // Line height, as a multiple of the size (the brief: 1.4-1.5 for text,
    // 1.15-1.25 for display).
    readonly property real lineDisplay: 1.15
    readonly property real lineTitle: 1.2
    readonly property real lineHeading: 1.3
    readonly property real lineBody: 1.5
    readonly property real lineChild: 1.3
    readonly property real lineWord: 1.35
    readonly property real lineMono: 1.35
    readonly property real lineCaption: 1.4

    // Letter spacing, in em: Label turns it into pixels. Positive tracking is
    // not applied in a right-to-left language (it breaks Hebrew's letter forms).
    readonly property real trackMono: 0.08
    readonly property real trackDisplay: -0.02
    readonly property real trackNone: 0

    // --- sizes, in dp ------------------------------------------------------
    readonly property real targetMin: 48
    readonly property real targetChild: 64
    readonly property real targetPrimary: 96
    readonly property real targetGap: 8
    readonly property real rowHeight: targetMin

    readonly property real space1: 4
    readonly property real space2: 8
    readonly property real space3: 12
    readonly property real space4: 16
    readonly property real space6: 24
    readonly property real space8: 32
    readonly property real space12: 48
    readonly property real space16: 64

    readonly property real hair: 1
    readonly property real rule: 1
    readonly property real heavy: 2
    readonly property real litEdge: 3
    readonly property real litScale: 1.03
    readonly property real focusWidth: 2
    readonly property real focusGap: 3
    readonly property real pathCoreWidth: 5
    readonly property real pathCasingWidth: 9
    // The path as a share of the tile, for a screen that scales it: the core
    // and casing are never thinner than the two widths above.
    readonly property real pathCoreShare: 0.07
    readonly property real pathCasingShare: 0.11
    // The ribbon: 14 and 22 px on the 200 px die of a roomy 3 x 3, scaled with the
    // die and never thinner than these on a small one (a 6 x 6 on a small window).
    readonly property real ribbonCoreMin: 8
    readonly property real ribbonEdgeMin: 14
    function ribbonCoreWidth(tile) {
        return Math.max(ribbonCoreMin, tile * pathCoreShare);
    }
    function ribbonEdgeWidth(tile) {
        return Math.max(ribbonEdgeMin, tile * pathCasingShare);
    }
    function ribbonLineWidth(tile) {
        return ribbonEdgeWidth(tile) + 2 * heavy;
    }
    // The hint's ring round a die: a gold band with a thin dark line on its outer
    // edge, so it holds 3:1 on a cream die as well as on the tray, and two rings
    // side by side stay two.
    readonly property real ringWidth: 4
    readonly property real ringWidthSecond: 3

    readonly property real radiusXs: 4
    readonly property real radiusSmall: 8
    readonly property real radius: 12
    readonly property real radiusLarge: 20

    readonly property real appBarHeight: 72
    readonly property real barHeight: appBarHeight

    // Pictures and panels.
    readonly property real pictureMax: 520
    readonly property real roomyPanel: 320
    readonly property real thumbInline: 96
    readonly property real thumbSheet: 112
    readonly property real thumbSheetCell: 136
    readonly property real sheetMaxWidth: 720
    readonly property real sheetMaxHeight: 560
    readonly property real modalMax: 560
    readonly property real contentWidth: 860
    // Home's column and the design's window (a screen larger than this is
    // zoomed to 1.5 at most and centred), the widest comfortable line of a
    // sentence, and a home card's picture: what it is given, the least it may
    // be given, and the tallest the card gets.
    readonly property real pageWidth: 1180
    readonly property real pageHeight: 740
    readonly property real measure: 720
    readonly property real cardArt: 240
    readonly property real cardArtMin: 160
    readonly property real cardMax: 420
    readonly property real hintDisplay: 96
    readonly property real doneWidth: 160
    readonly property real startRing: 8
    // A board-size choice (3 x 3 ... 6 x 6) is at least this wide.
    readonly property real choiceMin: 64
    readonly property real choiceCell: 88

    // Icons, on the 24 x 24 grid scaled to these.
    readonly property real iconXs: 16
    readonly property real iconSm: 20
    readonly property real iconMd: 28
    readonly property real iconLg: 32
    readonly property real iconMax: iconLg

    // The letter on a die is this share of the tile (Qu: this share of that).
    readonly property real letterShare: 0.55
    readonly property real quShare: 0.78

    // The size of a jewel in the tray, by tier, and the sizes the rest of the
    // application draws it at.
    readonly property real jewelSpark: 26
    readonly property real jewelPower: 34
    readonly property real jewelMega: 44
    readonly property real jewelXs: 16
    readonly property real jewelSm: 24
    readonly property real jewelMd: 30
    readonly property real jewelXl: 72

    // The tray around the board (kit/BoardTray.qml): its rim is `heavy`, its
    // outset beyond the tiles is a share of the tile with a floor, and it must
    // fit inside the board area's inset (apps/app/src/solo/board_layout.cpp:
    // small_inset 12, roomy_inset 24), which it does only when `limit` is
    // passed - see trayOutset().
    readonly property real trayInsetMin: 10
    readonly property real trayInsetShare: 0.08
    readonly property real trayRim: heavy
    readonly property real trayWellInset: 6
    function trayOutset(tile, limit) {
        const wanted = Math.max(trayInsetMin, tile * trayInsetShare);
        return limit === undefined ? wanted : Math.max(0, Math.min(wanted, limit - trayRim));
    }

    // --- motion, in ms -----------------------------------------------------
    readonly property int quick: ms(120, 0)
    readonly property int base: ms(200, 0)
    readonly property int screen: ms(280, 120)
    readonly property int turn: ms(380, 120)
    readonly property int restFade: ms(600, 0)
    readonly property int pathFade: 300
    readonly property int tileFlashStep: 60
    readonly property int tileFlashLength: 240
    readonly property int ringPulse: 500
    readonly property int glowRise: 600
    readonly property int glowFall: 1200
    readonly property real glowPeak: 0.28
    readonly property int wordHold: ms(450, 150)
    readonly property int clearedHold: 3000
    readonly property int holdToPlay: 1000

    function ms(full, reduced) {
        return reducedMotion ? reduced : full;
    }
}
