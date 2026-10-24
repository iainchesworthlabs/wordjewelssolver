// SPDX-License-Identifier: GPL-3.0-or-later
#include "solo_game_factory.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace wjs::app {
namespace {

// The game of a model that was told of none: a box that cannot be shaken. The
// program installs the real game before the engine loads; this is what a model
// made without one (a tool, a preview) falls back to, so it is never null and
// never pretends to play. The tests' stand-in is FakeSoloGame, which no program
// plays and which is not linked into the application.
class NoSoloGame final : public SoloGame {
public:
    [[nodiscard]] SoloState state() const override { return SoloState::box; }
    [[nodiscard]] std::span<const std::size_t> sizes() const override { return sizes_; }
    [[nodiscard]] std::size_t size() const override { return sizes_.front(); }
    void chooseSize(std::size_t /*size*/) override {}
    bool shake() override { return false; }
    void settled() override {}
    [[nodiscard]] const SoloBoard& board() const override { return board_; }
    TraceOutcome submit(std::span<const Position> /*path*/) override { return {}; }
    [[nodiscard]] std::vector<GraphemeBinder::Block> blocks(
        std::span<const Position> /*path*/) const override {
        return {};
    }
    void celebrated() override {}
    [[nodiscard]] bool helpAllowed() const override { return false; }
    std::optional<Position> startTile() override { return std::nullopt; }
    [[nodiscard]] std::optional<Position> litTile() const override { return std::nullopt; }
    [[nodiscard]] std::vector<Position> helpPath() const override { return {}; }
    [[nodiscard]] std::size_t wordsWithStart(std::span<const Position> /*prefix*/) const override {
        return 0;
    }
    void dismissHelp() override {}
    [[nodiscard]] const std::vector<SoloFound>& found() const override { return found_; }
    void lidOn() override {}
    void again() override {}
    [[nodiscard]] std::vector<SoloHistoryEntry> history() const override { return {}; }

private:
    static constexpr std::array<std::size_t, 1> sizes_{3};
    SoloBoard board_;
    std::vector<SoloFound> found_;
};

SoloGameMaker& installed() {
    static SoloGameMaker maker;
    return maker;
}

}  // namespace

ScopedSoloGameMaker::ScopedSoloGameMaker(SoloGameMaker maker) {
    installed() = std::move(maker);
}

ScopedSoloGameMaker::~ScopedSoloGameMaker() {
    installed() = nullptr;
}

std::unique_ptr<SoloGame> make_solo_game() {
    if (const SoloGameMaker& maker = installed()) {
        if (std::unique_ptr<SoloGame> game = maker()) {
            return game;
        }
    }
    return std::make_unique<NoSoloGame>();
}

}  // namespace wjs::app
