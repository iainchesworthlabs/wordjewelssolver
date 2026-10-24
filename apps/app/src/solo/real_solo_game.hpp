// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "solo_game.hpp"
#include "wjs/calendar.hpp"
#include "wjs/game_session.hpp"
#include "wjs/grapheme_binder.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/store.hpp"

// The Solo Play port over the library's session (wjs/game_session.hpp): the
// screens' SoloGame, answered by the rules instead of by the stand-in. It keeps
// no rule of its own. It turns the session's names into the port's, and it is the
// one place that hands the session's records to the store.
//
// It plays whichever board the session was made for (SessionOptions::mode): the
// standard board, the default, a roll of the dice at 3x3 to 6x6 with no phase
// and no grapheme blocks; or the phonics board over a phase, at 3x3 and 4x4,
// whose graphemes the port binds into blocks. The mapping:
//
//   port                     session                      and the store
//   shake()                  shake(); an error stays in the box and answers false
//   settled()                still(), then landed()
//   submit(path)             trace(path, today)           record() after a find
//   blocks(path)             GraphemeBinder over the phase's graphemes; none on
//                            the standard board, which has no phase
//   chooseSize(side)         choose_size()                the size is the settings' board_size
//   celebrated()             rest()
//   lidOn()                  back()                       record() of the board left
//   again()                  again()
//   startTile(), litTile()   request_help(), lit_tile()
//   helpAllowed()            the parent's switch, as it was given to the session
//   board()                  board() and dead_tiles() ("" is a dead jewel; a
//                            standard board has none), and the seed of last_deal()
//   history()                the store's boards, newest first, and their finds
//
// What a trace answers: found is viable; found_again is viable and already found;
// real_not_todays and not_a_word keep their names, and carry no word; ignored (not
// playing, or under two tiles) is answered not a word with nothing changed. The
// word is upper-case in the session and lower-case in the tray. The stamp given to
// trace() is a wjs::Day, the day on the device's calendar, which is what the store
// keeps of when a find was made and nothing finer.
//
// What it asks of the store. After each find, and when a board is left with the
// lid, it records the session's record() (the store ignores what it has already
// got). Whether a board beat the best of its size, and whether a word is a sticker
// for the first time, are the store's answers (RecordOutcome), and become
// TraceOutcome::newBest and ::sticker.
//
// The game in progress. After every call that can change it, the store is brought to
// what the session would be restored from (Store::save_session): a board on the
// table with its finds and the lit tile, or, once it has cleared, the resting
// screen. A game in the box is no game in progress, so the store keeps none and what
// it had is cleared: nothing is on the table, the size the box offers is the one
// chosen last (below) and the next deal takes a seed of its own. A game that the
// store cannot keep whole is cleared too, never left as the game before it. Making a
// game over a session restored from the store (SoloGameSetup) changes nothing the
// store holds; making a new one clears what was left. The session is saved before the
// board it earned is recorded, so a store that stops between the two is never ahead
// of the game, and the board left with the lid is recorded before the session is
// cleared, so it is never behind. The store keeps a standard board by the count of
// its words (no list), as it does a phonics board's list.
//
// The size the child chooses on the box is written to the settings
// (Settings::board_size, "3x3, then the last used") as it is chosen, so the next run's
// box offers it. Nothing else of the settings is touched.
//
// Single-threaded. The phase (if there is one), the dice and the dictionary are held
// by reference, through the session, and the store too; all must outlive the game.

namespace wjs::app {

class RealSoloGame final : public SoloGame {
public:
    // A game over a session already made (a restored one, or one a test dealt
    // by hand). `help_allowed` is what the session was given in
    // SessionOptions::help, which the session does not give back. `phase` is
    // the phase the session was made over, or null on the standard board.
    // `today` is the device's calendar day, asked when a find is made and when
    // a board is left. The store's game in progress is brought to the session's
    // (see above).
    RealSoloGame(wjs::GameSession session, bool help_allowed, const wjs::PhaseIndex* phase,
                 wjs::Store& store, std::function<wjs::Day()> today);

    // A new session in the box with `options`, over `data` (the English
    // dictionary always; the dice for the standard board, the phase for the
    // phonics board).
    [[nodiscard]] static std::expected<RealSoloGame, wjs::SessionError> create(
        const wjs::SessionData& data, wjs::Store& store, const wjs::SessionOptions& options,
        std::function<wjs::Day()> today);

    [[nodiscard]] SoloState state() const override;

    [[nodiscard]] std::span<const std::size_t> sizes() const override {
        return {sizes_.data(), sizes_count_};
    }
    [[nodiscard]] std::size_t size() const override;
    void chooseSize(std::size_t size) override;

    bool shake() override;
    void settled() override;
    [[nodiscard]] const SoloBoard& board() const override { return board_; }

    TraceOutcome submit(std::span<const Position> path) override;
    [[nodiscard]] std::vector<GraphemeBinder::Block> blocks(
        std::span<const Position> path) const override;

    void celebrated() override;

    [[nodiscard]] bool helpAllowed() const override { return help_allowed_; }
    std::optional<Position> startTile() override;
    [[nodiscard]] std::optional<Position> litTile() const override;
    [[nodiscard]] std::vector<Position> helpPath() const override;
    [[nodiscard]] std::size_t wordsWithStart(std::span<const Position> prefix) const override;
    void dismissHelp() override;

    [[nodiscard]] const std::vector<SoloFound>& found() const override { return found_; }

    void lidOn() override;
    void again() override;

    [[nodiscard]] std::vector<SoloHistoryEntry> history() const override;

    // The store refused a board or the game in progress, or its file could not be
    // written, the last time the game asked it to keep something. Cleared by the
    // next write that the store accepts and writes.
    [[nodiscard]] bool saveFailed() const override { return save_failed_; }

    // The session underneath, for a test to read what the adapter did to it.
    [[nodiscard]] const wjs::GameSession& session() const noexcept { return session_; }

private:
    // The board on the table as the scene is given it; empty when the session has
    // none.
    [[nodiscard]] SoloBoard scene_board() const;
    void refresh_found();
    // Hands the board in play, or the last left, to the store.
    wjs::RecordOutcome record_board();
    // Brings the store's game in progress to the session's (or clears it).
    void save_session();
    // Writes the size to the settings, if it is not the one there.
    void remember_size(wjs::BoardSize size);
    // Takes the store's word for how the last write went: refused, or accepted and
    // then not written.
    void note_write(bool accepted);

    wjs::GameSession session_;
    bool help_allowed_;
    // The phase the session plays (null on the standard board), held for the
    // grapheme binder, the dead letter of its boards and the history's.
    const wjs::PhaseIndex* phase_;
    wjs::Store* store_;
    std::function<wjs::Day()> today_;

    // The phase's grapheme blocks; none on the standard board, which has no
    // phase to bind with.
    std::optional<wjs::GraphemeBinder> binder_;
    // The letter the phase leaves out, which the session writes on a dead jewel
    // in its records; "" on the standard board, which has no dead jewels.
    std::string dead_letter_;

    // The sizes the box offers: every size on the standard board, 3x3 and 4x4
    // on the phonics board (the session's choose_size holds to the same).
    std::array<std::size_t, 4> sizes_{3, 4, 5, 6};
    std::size_t sizes_count_ = 2;
    // As the fake keeps it: dealt by the shake, and kept after the lid goes on
    // again until the next shake, so the screens never read a board that is gone.
    SoloBoard board_;
    std::vector<SoloFound> found_;
    bool save_failed_ = false;
};

}  // namespace wjs::app
