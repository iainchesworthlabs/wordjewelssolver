// SPDX-License-Identifier: GPL-3.0-or-later
#include "fake_solo_game.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>
#include <utility>

#include "wjs/path.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/scoring.hpp"

namespace wjs::app {
namespace {

// TEST DATA (see the header): today's words are the five the design names.
constexpr std::array<std::string_view, 5> todays_words{"NIP", "PAT", "SAT", "SIT", "TAP"};

// Real words that are not today's: a traced one gets the soft tick.
constexpr std::array<std::string_view, 10> other_real_words{"ANT", "NAP", "PAN", "PIN", "PIT",
                                                            "SAP", "SIP", "TAN", "TIN", "TIP"};

// One grapheme of two letters, so the blocks of a path show a shared bar where A
// and I sit side by side.
std::vector<Grapheme> test_graphemes() {
    Grapheme grapheme;
    grapheme.letters = "ai";
    grapheme.sound = "/test/";
    grapheme.clip = "test";
    return {grapheme};
}

// The two boards. Row-major; "" is a dead jewel. Every one of today's words can
// be traced on each, and no other of today's can: the fillers make none.
constexpr std::array<std::string_view, 9> board_3x3{"S", "I", "T", "N", "A", "P", "E", "O", "M"};
constexpr std::array<std::string_view, 16> board_4x4{"S", "I", "T", "E", "N", "A", "P", "O",
                                                     "L", "M", "U", "D", "",  "B", "G", ""};

// A quarter turn clockwise, n times.
std::vector<std::string> turned(std::span<const std::string_view> tiles, std::size_t side,
                                std::size_t quarter_turns) {
    std::vector<std::string> grid(tiles.begin(), tiles.end());
    for (std::size_t turn = 0; turn < quarter_turns % 4; ++turn) {
        std::vector<std::string> next(grid.size());
        for (std::size_t row = 0; row < side; ++row) {
            for (std::size_t column = 0; column < side; ++column) {
                next[column * side + (side - 1 - row)] = grid[row * side + column];
            }
        }
        grid = std::move(next);
    }
    return grid;
}

// The library's board for a scene board: a dead jewel is a letter that no word
// here uses.
Board library_board(const SoloBoard& board) {
    std::string text;
    for (std::size_t row = 0; row < board.rows; ++row) {
        for (std::size_t column = 0; column < board.columns; ++column) {
            const std::string& tile = board.tiles[row * board.columns + column];
            text += tile.empty() ? std::string{"J"} : tile;
            text += column + 1 < board.columns ? ' ' : '/';
        }
    }
    return Board::parse(text).value();
}

std::string lower(std::string word) {
    std::ranges::transform(word, word.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return word;
}

WordTier tier_of(std::size_t letters) {
    if (letters <= 3) {
        return WordTier::spark;
    }
    return letters == 4 ? WordTier::power : WordTier::mega;
}

int stars_of(WordTier tier) {
    switch (tier) {
        case WordTier::spark:
            return 1;
        case WordTier::power:
            return 2;
        case WordTier::mega:
            return 5;
    }
    return 1;
}

std::size_t size_index(std::size_t size) {
    return size == 4 ? 1 : 0;
}

}  // namespace

FakeSoloGame::FakeSoloGame() : binder_(test_graphemes()) {
    for (const auto word : todays_words) {
        static_cast<void>(todays_.insert(word));
        static_cast<void>(real_.insert(word));
    }
    for (const auto word : other_real_words) {
        static_cast<void>(real_.insert(word));
    }
}

std::vector<std::string> FakeSoloGame::todaysWords() {
    return {todays_words.begin(), todays_words.end()};
}

void FakeSoloGame::chooseSize(std::size_t size) {
    if (state_ != SoloState::box || (size != 3 && size != 4)) {
        return;
    }
    size_ = size;
}

bool FakeSoloGame::shake() {
    if (state_ != SoloState::box) {
        return false;
    }
    board_ = {};
    board_.rows = size_;
    board_.columns = size_;
    board_.seed = next_seed_++;
    board_.tiles =
        size_ == 3 ? turned(board_3x3, 3, board_.seed) : turned(board_4x4, 4, board_.seed);

    viable_ = wjs::solve(library_board(board_), todays_);
    found_.clear();
    stars_ = 0;
    saved_ = false;
    lit_word_.clear();
    state_ = SoloState::shaking;
    return true;
}

void FakeSoloGame::settled() {
    if (state_ == SoloState::shaking || state_ == SoloState::dropping) {
        state_ = SoloState::playing;
    }
}

TraceOutcome FakeSoloGame::submit(std::span<const Position> path) {
    TraceOutcome outcome;
    if (state_ != SoloState::playing || path.size() < 2) {
        return outcome;
    }
    const auto spelled = wjs::validate_path(library_board(board_), path);
    if (!spelled) {
        return outcome;
    }
    const std::string& word = *spelled;

    if (todays_.contains(word)) {
        outcome.kind = WordKind::viable;
        outcome.found.word = lower(word);
        outcome.found.tier = tier_of(word.size());
        outcome.found.stars = stars_of(outcome.found.tier);
        outcome.found.points = wjs::word_points(word);
        const bool again = std::ranges::any_of(
            found_, [&](const SoloFound& entry) { return entry.word == outcome.found.word; });
        outcome.alreadyFound = again;
        if (!again) {
            found_.push_back(outcome.found);
            stars_ += outcome.found.stars;
        }
        const bool all = std::ranges::all_of(viable_, [&](const wjs::FoundWord& viable) {
            return std::ranges::any_of(
                found_, [&](const SoloFound& entry) { return entry.word == lower(viable.word); });
        });
        if (all) {
            outcome.cleared = true;
            int& best = best_[size_index(size_)];
            outcome.newBest = stars_ > best;
            best = std::max(best, stars_);
            state_ = SoloState::cleared;
            save();
        }
    } else if (real_.contains(word)) {
        // Real, not today's: the kind and no word.
        outcome.kind = WordKind::realNotTodays;
    }
    return outcome;
}

std::vector<GraphemeBinder::Block> FakeSoloGame::blocks(std::span<const Position> path) const {
    if (board_.empty()) {
        return {};
    }
    return binder_.bind(library_board(board_), path);
}

void FakeSoloGame::celebrated() {
    if (state_ == SoloState::cleared) {
        state_ = SoloState::resting;
    }
}

std::optional<Position> FakeSoloGame::startTile() {
    if (!help_allowed_ || state_ != SoloState::playing) {
        return std::nullopt;
    }
    // The shortest word not yet found; of those the one whose first tile is
    // nearest the top left (squared distance, then the upper tile, then the left
    // one), then the word that sorts first.
    const wjs::FoundWord* best = nullptr;
    const auto key = [](const wjs::FoundWord& word) {
        const Position first = word.path.front();
        return std::tuple{word.word.size(), first.row * first.row + first.column * first.column,
                          first.row, first.column, word.word};
    };
    for (const auto& viable : viable_) {
        const bool done = std::ranges::any_of(
            found_, [&](const SoloFound& entry) { return entry.word == lower(viable.word); });
        if (done || viable.path.empty()) {
            continue;
        }
        if (best == nullptr || key(viable) < key(*best)) {
            best = &viable;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    lit_word_ = best->word;
    return best->path.front();
}

std::optional<Position> FakeSoloGame::litTile() const {
    if (lit_word_.empty() || state_ != SoloState::playing) {
        return std::nullopt;
    }
    const auto viable = std::ranges::find(viable_, lit_word_, &wjs::FoundWord::word);
    const bool done = std::ranges::any_of(
        found_, [&](const SoloFound& entry) { return entry.word == lower(lit_word_); });
    if (viable == viable_.end() || viable->path.empty() || done) {
        return std::nullopt;
    }
    return viable->path.front();
}

std::vector<Position> FakeSoloGame::helpPath() const {
    if (lit_word_.empty() || state_ != SoloState::playing) {
        return {};
    }
    const auto viable = std::ranges::find(viable_, lit_word_, &wjs::FoundWord::word);
    const bool done = std::ranges::any_of(
        found_, [&](const SoloFound& entry) { return entry.word == lower(lit_word_); });
    if (viable == viable_.end() || done) {
        return {};
    }
    return viable->path;
}

std::size_t FakeSoloGame::wordsWithStart(std::span<const Position> prefix) const {
    std::size_t count = 0;
    for (const wjs::FoundWord& viable : viable_) {
        const bool done = std::ranges::any_of(
            found_, [&](const SoloFound& entry) { return entry.word == lower(viable.word); });
        if (done || viable.path.size() < prefix.size()) {
            continue;
        }
        bool begins = true;
        for (std::size_t i = 0; i < prefix.size(); ++i) {
            if (viable.path[i] != prefix[i]) {
                begins = false;
                break;
            }
        }
        if (begins) {
            ++count;
        }
    }
    return count;
}

void FakeSoloGame::dismissHelp() {
    lit_word_.clear();
}

void FakeSoloGame::save() {
    if (saved_ || board_.empty()) {
        return;
    }
    saved_ = true;
    history_.insert(history_.begin(), SoloHistoryEntry{board_, found_});
    constexpr std::size_t kept = 20;
    if (history_.size() > kept) {
        history_.resize(kept);
    }
}

void FakeSoloGame::lidOn() {
    if (state_ == SoloState::playing || state_ == SoloState::shaking ||
        state_ == SoloState::dropping) {
        if (state_ == SoloState::playing) {
            save();
        }
        state_ = SoloState::box;
    }
}

void FakeSoloGame::again() {
    if (state_ == SoloState::resting) {
        state_ = SoloState::box;
    }
}

}  // namespace wjs::app
