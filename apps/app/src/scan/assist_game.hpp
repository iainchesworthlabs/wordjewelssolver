// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/calendar.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/game_rules.hpp"
#include "wjs/store.hpp"

// What the Family Assistant screens need from the game, and nothing more: the
// name of docs/design/game.md's `assist.playing`, as plain C++ with no Qt. The
// screens talk to AssistModel, AssistModel talks to this.
//
// Mode A is not a game and this port keeps no scoreboard. It is given one board
// a grown-up has confirmed (wjs::scan::BoardDraft::confirm(), so never a reading
// straight from the reader), plays it against a word set, and answers what a
// traced path is:
//
//   viable          a word that counts on this board: a jewel in the tray, the
//                   stars of its tier
//   realNotTodays   a real word that does not count here: the neutral cue, no
//                   word, nothing shown or read
//   notAWord        anything else: the path fades, no sound, no text
//
// On the standard board the word set is the English dictionary itself, so every
// real word of three letters or more counts there and `realNotTodays` never
// arises (docs/design/game.md: "the real, not today's cue below is the phonics
// board's"). The two-dictionary constructor is the set the design's Mode A names
// (the phase index), kept for the phase data that does not exist yet and for the
// tests that hold the middle state down.
//
// Help is one level in both modes (question 68): the start tile of the shortest
// viable word not yet found, nearest the top left, the same one until that word
// is found (wjs::start_tile_help). The word itself is never handed out, only the
// tile to light.
//
// The board is recorded to the store when the round is left (leave()), in the
// standard board's form: no phase, and `viable_total` rather than a list of
// words, because a real board holds tens of them and the store lists none
// (docs/design/dashboard.md, "The store"). The finds keep the day they were
// made and whether the start tile was shown for them. Recording is once per
// round and a round is recorded even with no find: the family may have played it
// on the table.
//
// No Qt, no pixels, no clock, no I/O: the board, the dictionaries and the store
// are held by reference or by value, and the day is the caller's (wjs::Day).

namespace wjs::app {

// What a traced path is on this board (the library's WordState, without the
// word for the two states that must never be shown or read).
enum class AssistWordKind {
    viable,
    realNotTodays,
    notAWord,
};

// A word found on the board: one jewel in the tray.
struct AssistFound {
    std::string word;  // lower case: the tray's text
    wjs::Tier tier = wjs::Tier::spark;
    int stars = 0;
    // The start tile had been shown for this word when it was found.
    bool assisted = false;
};

struct AssistTraceOutcome {
    AssistWordKind kind = AssistWordKind::notAWord;
    // Set for a viable word, and only for one: the other two carry no word on
    // purpose.
    AssistFound found;
    // A viable word that was on the tray already: no new jewel.
    bool alreadyFound = false;
};

// An earlier board, for the panel's history (newest first): its letters and the
// words found on it. The store keeps the last twenty boards of both modes, and
// it does not say which mode played one (a board with no phase is a standard
// board), so this is every earlier board on the device: the same list the family
// sees in Solo Play.
struct AssistHistoryEntry {
    std::size_t rows = 0;
    std::size_t columns = 0;
    std::vector<std::string> tiles;
    std::vector<AssistFound> found;
};

class AssistGame {
public:
    // The standard board's word set: the English dictionary as both the words
    // that count and the words that tell a real word from a string of letters.
    // No phase, no block list (docs/design/mvp-library.md).
    AssistGame(wjs::Board board, const wjs::Dictionary& dictionary, wjs::Store& store,
               std::function<wjs::Day()> today);

    // The design's Mode A word set: `dictionary` tells a real word from a string
    // of letters and `active` is what counts on this board (a phase index, when
    // the repository has one). The words that count are a subset of the
    // dictionary's, as they are for the library's WordSets.
    AssistGame(wjs::Board board, const wjs::Dictionary& dictionary, const wjs::Dictionary& active,
               wjs::Store& store, std::function<wjs::Day()> today);

    AssistGame(const AssistGame&) = delete;
    AssistGame& operator=(const AssistGame&) = delete;
    AssistGame(AssistGame&&) = delete;
    AssistGame& operator=(AssistGame&&) = delete;
    ~AssistGame() = default;

    [[nodiscard]] const wjs::Board& board() const noexcept { return board_; }

    // The words of the active set the board holds, each once: what "nothing for
    // you this time" is counted from. Empty is an ordinary board, not an error.
    [[nodiscard]] const std::vector<wjs::FoundWord>& viable() const noexcept { return viable_; }
    [[nodiscard]] std::size_t viableCount() const noexcept { return viable_.size(); }

    // A traced or tapped path (the check that it is a path is wjs::validate_path's
    // inside classify_path). `alreadyFound` keeps nothing from being earned
    // twice; the tray's entry is the first find's.
    [[nodiscard]] AssistTraceOutcome submit(std::span<const wjs::Position> path);

    // "Show me a start": the tile of the shortest viable word not yet found,
    // nearest the top left (wjs::start_tile_help). Nothing once none is left.
    // Asking again lights the same tile, and finding another word does not move
    // it: the answer depends only on the words left.
    std::optional<wjs::Position> showStart();

    // The tile lit now, or nothing: a shown start is gone once its word is
    // found. Asking this lights nothing.
    [[nodiscard]] std::optional<wjs::Position> litTile() const;

    // The parent's help switch as the store has it now (Settings::help). The
    // screens do not offer the start tile when it is off.
    [[nodiscard]] bool helpAllowed() const;

    // The words found on this board, in the order found.
    [[nodiscard]] const std::vector<AssistFound>& found() const noexcept { return found_; }
    [[nodiscard]] int stars() const noexcept;

    // The store's earlier boards, newest first, with their finds: the panel's
    // history. The board in play is not in it until leave() records it.
    [[nodiscard]] std::vector<AssistHistoryEntry> history() const;

    // The round is left: the board is recorded to the store, once. Safe to call
    // more than once, and safe with no find: the family played it on the table.
    // A board that is not a 3x3 to 6x6 square cannot be kept in the store's form
    // and is not recorded (the screen only confirms such boards).
    void leave();

    [[nodiscard]] bool recorded() const noexcept { return recorded_; }

    // The board could not be kept: the store refused it, or its file could not be
    // written. False until the round is left, and for a board the store has no
    // form for (that is not a failure, the screen never confirms one). The family
    // is told so that they know the history lacks this board.
    [[nodiscard]] bool saveFailed() const noexcept { return save_failed_; }

private:
    [[nodiscard]] wjs::WordSets words() const noexcept;
    // The tiles as Board::parse reads them: spaces between tiles, '/' between rows.
    [[nodiscard]] std::string tiles_text() const;
    // The tray's entry for a word, or nullptr.
    [[nodiscard]] const AssistFound* tray_word(std::string_view word) const;

    wjs::Board board_;
    const wjs::Dictionary* dictionary_;
    const wjs::Dictionary* active_;
    wjs::Store* store_;
    std::function<wjs::Day()> today_;
    std::vector<wjs::FoundWord> viable_;
    // The words found, each once, upper-case: what start_tile_help reads.
    wjs::FoundWords found_set_;
    std::vector<AssistFound> found_;
    // The word whose start tile was shown and has not been found, upper-case.
    std::optional<std::string> lit_word_;
    bool recorded_ = false;
    bool save_failed_ = false;
};

}  // namespace wjs::app
