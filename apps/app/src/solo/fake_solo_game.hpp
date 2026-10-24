// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "solo_game.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/grapheme_binder.hpp"
#include "wjs/solver.hpp"

// A stand-in for the library's game session, for the tests (a game that never
// surprises). It follows the flow of docs/design/game.md and nothing else.
//
// Its words are TEST DATA, not a phase and not a proposal: today's are the five
// early-reader words the design names, a few more are real but not today's, and
// the boards are two fixed grids turned by the seed. No phonics list is
// published by it. The real game is RealSoloGame (real_solo_game.hpp); which of the
// two is played is decided in solo_game_factory.cpp.

namespace wjs::app {

class FakeSoloGame final : public SoloGame {
public:
    FakeSoloGame();

    [[nodiscard]] SoloState state() const override { return state_; }

    [[nodiscard]] std::span<const std::size_t> sizes() const override { return sizes_; }
    [[nodiscard]] std::size_t size() const override { return size_; }
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

    [[nodiscard]] std::vector<SoloHistoryEntry> history() const override { return history_; }

    // For tests: the parent's switch for the help, and the words the fake calls
    // today's (upper-case).
    void setHelpAllowed(bool allowed) { help_allowed_ = allowed; }
    [[nodiscard]] static std::vector<std::string> todaysWords();

private:
    void save();

    SoloState state_ = SoloState::box;
    std::array<std::size_t, 2> sizes_{3, 4};
    std::size_t size_ = 3;
    bool help_allowed_ = true;

    SoloBoard board_;
    std::vector<wjs::FoundWord> viable_;
    std::vector<SoloFound> found_;
    int stars_ = 0;
    bool saved_ = false;
    std::string lit_word_;  // upper-case, empty when no tile is lit

    Dictionary todays_;
    Dictionary real_;
    GraphemeBinder binder_;

    std::uint32_t next_seed_ = 1;
    std::array<int, 2> best_{0, 0};
    std::vector<SoloHistoryEntry> history_;
};

}  // namespace wjs::app
