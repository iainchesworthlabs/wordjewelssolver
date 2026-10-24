// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/grapheme_binder.hpp"

// What the Solo Play screens need from the game, and nothing more: the names
// of docs/design/game.md (Mode B, "The flow") as plain C++, with no Qt. The
// screens talk to SoloModel, SoloModel talks to this, and this is answered by
// the library's session (RealSoloGame, over libs/core's GameSession), which the
// program always plays; FakeSoloGame is the stand-in the tests hand to a
// SoloModel. The library's session owns the
// rules: what a traced path is, what a find earns, which tile the help lights and when the board is
// cleared. This port owns none of them and decides nothing.
//
// The states are the states of the flow, less one:
//
//   box ──shake()──▶ shaking ──settled()──▶ playing ──submit(last word)──▶ cleared
//    ▲                                                                       │
//    └─────────────── again() ◀──────────── resting ◀──── celebrated() ◀─────┘
//
// `solo.word-found` is not a state here: it is the few moments of presentation
// after a find, with the game already in `playing` or `cleared`, and it belongs
// to SoloModel (the library's session has no such state either).
//
// `shaking` and `dropping` are one stretch to the screens: the board scene
// plays the shake, the lid and the drop itself and says when the dice are
// still, so the port has one call for it (settled) and the game may report
// `shaking` or `dropping` between shake() and settled() as it likes.

namespace wjs::app {

enum class SoloState {
    box,       // solo.box: the lid on; size choice; Shake
    shaking,   // solo.shaking
    dropping,  // solo.dropping
    playing,   // solo.playing: the board and the panel
    cleared,   // solo.cleared: the last jewel has landed
    resting,   // solo.resting: the calm screen
};

[[nodiscard]] std::string_view to_string(SoloState state) noexcept;

// What a traced path is (docs/design/game.md, "Words and what each does").
enum class WordKind {
    viable,         // a word of today's set, on this board: a jewel, stars
    realNotTodays,  // a real word that is not today's: a soft tick, no word shown
    notAWord,       // anything else: the path fades, no sound, no text
};

// Spark 3 letters, Power 4, Mega 5 or more (a Qu tile counts two).
enum class WordTier { spark, power, mega };

// A board as the scene is given it: decided before the shake begins.
struct SoloBoard {
    std::size_t rows = 0;
    std::size_t columns = 0;
    // Row-major, upper-case, one to three letters. "" is a dead jewel: drawn,
    // part of no word, and a touch on it does nothing.
    std::vector<std::string> tiles;
    // The six faces of the die on each tile, row-major, and which of them is up,
    // so that faces[t][up[t]] == tiles[t]. They are the library's roll of the
    // seed (wjs::roll_with_placements): a die carries its six different letters
    // and the scene lands it on the one that is up. Empty when the board was not
    // rolled from dice: the phonics board, a game the store brought back (a
    // board's roll seed is not kept), and the fake game. The scene then draws
    // the tile's letter on the face that is up, as it did before the faces came.
    std::vector<std::vector<std::string>> faces;
    std::vector<int> up;  // one of the six, 0 to 5
    // The seed of the scene's shake and drop; the same board and seed replay
    // identically. A dealt board's seed is also the roll's seed, which is why the
    // faces above are what wjs::roll_with_placements gives for it.
    std::uint32_t seed = 0;

    [[nodiscard]] bool empty() const noexcept { return tiles.empty(); }
    [[nodiscard]] const std::string& tile(Position position) const {
        return tiles[position.row * columns + position.column];
    }
};

// A word the child has found on a board. The tray shows these, in order found.
struct SoloFound {
    std::string word;  // lower-case: the tray's text
    WordTier tier = WordTier::spark;
    int stars = 0;
    // The word's child-facing points (wjs::word_points): length, letter value
    // and the long-word bonus. Stars are the tier's reward; points are the
    // running score the panel and summary show.
    int points = 0;
};

struct TraceOutcome {
    WordKind kind = WordKind::notAWord;
    // Set for a viable word, and only for one: the other two kinds carry no
    // word on purpose, so nothing can show or read a word that is not today's.
    SoloFound found;
    // A viable word that was on the tray already: no new jewel.
    bool alreadyFound = false;
    // The board has no viable word left; the game is now in `cleared`.
    bool cleared = false;
    // The most stars so far in one board of this size. Recorded silently and
    // celebrated once, inside the celebration of the last word; never shown
    // as a number.
    bool newBest = false;
    // A viable word whose tier carries a sticker, found for the first time on
    // this device. The store knows whether it is the first time. No screen draws
    // a sticker yet; the vault is the parent's to open.
    bool sticker = false;
};

// A board the child played, for the history thumbnails (newest first).
struct SoloHistoryEntry {
    SoloBoard board;
    std::vector<SoloFound> found;
};

class SoloGame {
public:
    virtual ~SoloGame() = default;

    [[nodiscard]] virtual SoloState state() const = 0;

    // The sizes offered (3x3 to 6x6 on the standard board, 3x3 and 4x4 on the
    // phonics board) and the one chosen. The 3x3 is the first board of a new
    // device. Only in `box`.
    [[nodiscard]] virtual std::span<const std::size_t> sizes() const = 0;
    [[nodiscard]] virtual std::size_t size() const = 0;
    virtual void chooseSize(std::size_t size) = 0;

    // box -> shaking. The board is dealt here, before anything moves. False
    // when no board could be dealt: the state stays `box`.
    virtual bool shake() = 0;
    // The scene's dice are still: -> playing.
    virtual void settled() = 0;
    // The board of this round: empty in `box` until the first shake().
    [[nodiscard]] virtual const SoloBoard& board() const = 0;

    // The child lifted the finger or pressed Done on `path` (only in
    // `playing`); anywhere else it is answered `notAWord` and nothing changes.
    // The last viable word moves the state to `cleared`.
    virtual TraceOutcome submit(std::span<const Position> path) = 0;
    // The grapheme blocks of a path, as it grows, for the shared bars.
    [[nodiscard]] virtual std::vector<GraphemeBinder::Block> blocks(
        std::span<const Position> path) const = 0;

    // cleared -> resting, about 3 seconds after the last jewel landed.
    virtual void celebrated() = 0;

    // Whether the parent allows the start tile, and the tile to light for one
    // press (the same until its word is found, or nothing once none is left).
    [[nodiscard]] virtual bool helpAllowed() const = 0;
    virtual std::optional<Position> startTile() = 0;
    // The tile lit now: set by startTile(), gone once its word is found. Asking
    // this lights nothing; the app never lights a tile unasked.
    [[nodiscard]] virtual std::optional<Position> litTile() const = 0;
    // The path of the word the help is for, empty when no word is lit. The
    // second rung of the help reads only its first two tiles.
    [[nodiscard]] virtual std::vector<Position> helpPath() const = 0;
    // How many of the board's words are still to find and begin with `prefix`
    // (the tile the help lit, or its first two tiles): a count, never a word.
    [[nodiscard]] virtual std::size_t wordsWithStart(std::span<const Position> prefix) const = 0;
    // The help is put away without its word being found: the child made a word
    // from the lit start, and the start is now their own.
    virtual void dismissHelp() = 0;

    // The words found on this board, in order.
    [[nodiscard]] virtual const std::vector<SoloFound>& found() const = 0;

    // playing -> box: the lid goes back on and the board is saved to history.
    virtual void lidOn() = 0;
    // resting -> box (Again, or leaving to home so the box is ready next time).
    virtual void again() = 0;

    // Earlier boards, newest first.
    [[nodiscard]] virtual std::vector<SoloHistoryEntry> history() const = 0;

    // The game's progress could not be kept: the store refused a board or a
    // game in progress, or its file could not be written. A game with nowhere to
    // keep anything (the fake) has nothing to fail and answers false. The parent
    // area is where the grown-up is told how the store is doing; this is how the
    // game says that it has just been let down.
    [[nodiscard]] virtual bool saveFailed() const { return false; }
};

}  // namespace wjs::app
