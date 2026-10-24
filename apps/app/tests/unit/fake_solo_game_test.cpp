// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "fake_solo_game.hpp"
#include "wjs/board.hpp"
#include "wjs/path.hpp"

namespace {

using wjs::Position;
using wjs::app::FakeSoloGame;
using wjs::app::SoloState;
using wjs::app::WordKind;
using wjs::app::WordTier;

// The first board of a new device (seed 1) is the 3x3 turned a quarter:
//
//     E N S
//     O A I
//     M P T
//
// SIT runs down the right-hand column and SAT, TAP, PAT and NIP are as drawn in
// each helper below.
using Path = std::vector<Position>;
const Path sit{{0, 2}, {1, 2}, {2, 2}};
const Path sat{{0, 2}, {1, 1}, {2, 2}};
const Path tap{{2, 2}, {1, 1}, {2, 1}};
const Path pat{{2, 1}, {1, 1}, {2, 2}};
const Path nip{{0, 1}, {1, 2}, {2, 1}};

FakeSoloGame playing() {
    FakeSoloGame game;
    REQUIRE(game.shake());
    game.settled();
    REQUIRE(game.state() == SoloState::playing);
    return game;
}

}  // namespace

TEST_CASE("the fake game starts in the box with the 3x3 chosen", "[app][fake]") {
    FakeSoloGame game;

    CHECK(game.state() == SoloState::box);
    CHECK(game.size() == 3);
    REQUIRE(game.sizes().size() == 2);
    CHECK(game.sizes()[0] == 3);
    CHECK(game.sizes()[1] == 4);
    CHECK(game.board().empty());
    CHECK(game.found().empty());
    CHECK(game.history().empty());
}

TEST_CASE("the board is dealt by the shake, before anything moves", "[app][fake]") {
    FakeSoloGame game;

    REQUIRE(game.shake());

    CHECK(game.state() == SoloState::shaking);
    REQUIRE(game.board().tiles.size() == 9);
    CHECK(game.board().rows == 3);
    CHECK(game.board().columns == 3);
    CHECK(game.board().tiles[0] == "E");
    CHECK(game.board().tile({0, 2}) == "S");
    // The fake has no dice: the scene draws the tile's letter on the face that is
    // up rather than tumble a die it was not given.
    CHECK(game.board().faces.empty());
    CHECK(game.board().up.empty());

    // The shake only applies in the box.
    CHECK_FALSE(game.shake());
    game.settled();
    CHECK(game.state() == SoloState::playing);
    CHECK_FALSE(game.shake());
}

TEST_CASE("the size is chosen in the box only, and only 3 or 4", "[app][fake]") {
    FakeSoloGame game;

    game.chooseSize(4);
    CHECK(game.size() == 4);
    game.chooseSize(5);
    CHECK(game.size() == 4);

    REQUIRE(game.shake());
    CHECK(game.board().tiles.size() == 16);
    game.chooseSize(3);
    CHECK(game.size() == 4);
}

TEST_CASE("the 4x4 board has dead jewels that no word uses", "[app][fake]") {
    FakeSoloGame game;
    game.chooseSize(4);
    REQUIRE(game.shake());
    game.settled();

    const auto& tiles = game.board().tiles;
    CHECK(std::ranges::count(tiles, std::string{}) == 2);
}

TEST_CASE("a board is the same for the same run and differs from the next shake", "[app][fake]") {
    FakeSoloGame a;
    FakeSoloGame b;
    REQUIRE(a.shake());
    REQUIRE(b.shake());
    CHECK(a.board().tiles == b.board().tiles);
    CHECK(a.board().seed == b.board().seed);

    a.settled();
    a.lidOn();
    REQUIRE(a.shake());
    CHECK(a.board().seed != b.board().seed);
    CHECK(a.board().tiles != b.board().tiles);
}

TEST_CASE("a viable word is a jewel with the stars of its tier", "[app][fake]") {
    auto game = playing();

    const auto outcome = game.submit(sit);

    CHECK(outcome.kind == WordKind::viable);
    CHECK(outcome.found.word == "sit");
    CHECK(outcome.found.tier == WordTier::spark);
    CHECK(outcome.found.stars == 1);
    CHECK_FALSE(outcome.alreadyFound);
    CHECK_FALSE(outcome.cleared);
    REQUIRE(game.found().size() == 1);
    CHECK(game.found()[0].word == "sit");
    CHECK(game.state() == SoloState::playing);
}

TEST_CASE("finding a word again adds no jewel", "[app][fake]") {
    auto game = playing();
    game.submit(sit);

    const auto again = game.submit(sit);

    CHECK(again.kind == WordKind::viable);
    CHECK(again.alreadyFound);
    CHECK(game.found().size() == 1);
}

TEST_CASE("a real word that is not today's is answered with no word", "[app][fake]") {
    auto game = playing();
    // TIP: T, I, P down the right-hand column and across.
    const Path tip{{2, 2}, {1, 2}, {2, 1}};

    const auto outcome = game.submit(tip);

    CHECK(outcome.kind == WordKind::realNotTodays);
    // Nothing in the answer can show or read a word that is not one of today's.
    CHECK(outcome.found.word.empty());
    CHECK(outcome.found.stars == 0);
    CHECK(game.found().empty());
}

TEST_CASE("anything else is not a word, and a real word is never called that", "[app][fake]") {
    auto game = playing();

    // EN: not a word.
    CHECK(game.submit(Path{{0, 0}, {0, 1}}).kind == WordKind::notAWord);
    // One tile, and a path that does not touch: nothing.
    CHECK(game.submit(Path{{0, 0}}).kind == WordKind::notAWord);
    CHECK(game.submit(Path{{0, 0}, {2, 2}}).kind == WordKind::notAWord);
    CHECK(game.found().empty());
    // A real word of the fake's list is not "not a word".
    CHECK(game.submit(Path{{2, 2}, {1, 2}, {2, 1}}).kind != WordKind::notAWord);
}

TEST_CASE("nothing is a word outside playing", "[app][fake]") {
    FakeSoloGame game;
    CHECK(game.submit(sit).kind == WordKind::notAWord);

    REQUIRE(game.shake());
    CHECK(game.submit(sit).kind == WordKind::notAWord);
    CHECK(game.found().empty());
}

TEST_CASE("the board clears with the last word, once, and then rests", "[app][fake]") {
    auto game = playing();

    for (const auto& path : {sit, sat, tap, pat}) {
        const auto outcome = game.submit(path);
        CHECK(outcome.kind == WordKind::viable);
        CHECK_FALSE(outcome.cleared);
    }
    CHECK(game.state() == SoloState::playing);

    const auto last = game.submit(nip);
    CHECK(last.cleared);
    // The first board cleared beats a best of nothing.
    CHECK(last.newBest);
    CHECK(game.state() == SoloState::cleared);
    CHECK(game.found().size() == 5);

    game.celebrated();
    CHECK(game.state() == SoloState::resting);
    // Resting is saved: the board and its words.
    REQUIRE(game.history().size() == 1);
    CHECK(game.history()[0].found.size() == 5);

    game.again();
    CHECK(game.state() == SoloState::box);
}

TEST_CASE("a second clear that does not beat the best says nothing", "[app][fake]") {
    FakeSoloGame game;
    for (int round = 0; round < 2; ++round) {
        REQUIRE(game.shake());
        game.settled();
        // The boards turn with the seed: find each word by solving it again from
        // the board, which is what a child does by looking.
        wjs::app::TraceOutcome last;
        for (const auto& word : FakeSoloGame::todaysWords()) {
            // Find a path for the word on the board in play.
            const auto& board = game.board();
            std::vector<Position> found;
            std::vector<Position> path;
            std::vector<bool> used(board.tiles.size(), false);
            const auto walk = [&](auto&& self, Position at, std::size_t next) -> bool {
                if (board.tile(at) != std::string(1, word[next])) {
                    return false;
                }
                path.push_back(at);
                used[at.row * board.columns + at.column] = true;
                if (next + 1 == word.size()) {
                    return true;
                }
                for (std::size_t row = 0; row < board.rows; ++row) {
                    for (std::size_t column = 0; column < board.columns; ++column) {
                        const Position to{row, column};
                        if (!used[row * board.columns + column] && wjs::are_neighbours(at, to) &&
                            self(self, to, next + 1)) {
                            return true;
                        }
                    }
                }
                path.pop_back();
                used[at.row * board.columns + at.column] = false;
                return false;
            };
            for (std::size_t row = 0; row < board.rows && found.empty(); ++row) {
                for (std::size_t column = 0; column < board.columns && found.empty(); ++column) {
                    path.clear();
                    std::ranges::fill(used, false);
                    if (walk(walk, Position{row, column}, 0)) {
                        found = path;
                    }
                }
            }
            REQUIRE(found.size() == word.size());
            last = game.submit(found);
            REQUIRE(last.kind == WordKind::viable);
        }
        CHECK(last.cleared);
        CHECK(last.newBest == (round == 0));
        game.celebrated();
        game.again();
    }
}

TEST_CASE("the start tile is the shortest word's, nearest the top left, and stays until found",
          "[app][fake]") {
    auto game = playing();

    CHECK_FALSE(game.litTile());
    // NIP starts at N, row 0 column 1: nearest the top left of the five.
    const auto first = game.startTile();
    REQUIRE(first);
    CHECK(*first == Position{0, 1});
    CHECK(game.litTile() == first);
    // The same press gives the same answer.
    CHECK(game.startTile() == first);

    // Finding another word leaves it lit.
    game.submit(sit);
    CHECK(game.litTile() == first);

    // Finding its word puts it out, and nothing lights by itself.
    game.submit(nip);
    CHECK_FALSE(game.litTile());

    // The next press names the next: SAT and SIT both start at S; SAT sorts first.
    const auto next = game.startTile();
    REQUIRE(next);
    CHECK(*next == Position{0, 2});
}

TEST_CASE("the parent can turn the start tile off", "[app][fake]") {
    auto game = playing();
    game.setHelpAllowed(false);

    CHECK_FALSE(game.helpAllowed());
    CHECK_FALSE(game.startTile());
    CHECK_FALSE(game.litTile());
}

TEST_CASE("putting the lid back on saves a board in play and leaves a shake unsaved",
          "[app][fake]") {
    FakeSoloGame game;
    REQUIRE(game.shake());
    game.settled();
    game.submit(sit);
    game.lidOn();

    CHECK(game.state() == SoloState::box);
    REQUIRE(game.history().size() == 1);
    CHECK(game.history()[0].found.size() == 1);
    CHECK(game.history()[0].board.tiles.size() == 9);

    // The lid back on in the middle of a shake: the board was never seen.
    REQUIRE(game.shake());
    game.lidOn();
    CHECK(game.state() == SoloState::box);
    CHECK(game.history().size() == 1);
}

TEST_CASE("the history keeps the last twenty boards, newest first", "[app][fake]") {
    FakeSoloGame game;
    for (int i = 0; i < 22; ++i) {
        REQUIRE(game.shake());
        game.settled();
        game.lidOn();
    }

    const auto history = game.history();
    REQUIRE(history.size() == 20);
    CHECK(history.front().board.seed == 22);
    CHECK(history.back().board.seed == 3);
}

TEST_CASE("tiles that make one sound are one block", "[app][fake]") {
    auto game = playing();

    // A then I side by side: the fake's one test grapheme, "ai".
    const auto blocks = game.blocks(Path{{1, 1}, {1, 2}});

    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0].tiles == 2);
    CHECK(blocks[0].grapheme == "ai");
    // A tile on its own is a block of one.
    CHECK(game.blocks(Path{{0, 2}, {1, 2}}).size() == 2);
}
