// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The boards every budget is measured on: the same boards on every machine,
// compiler and standard library, so that a count is a count of one thing.

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/dice.hpp"
#include "wjs/dice_sets.hpp"
#include "wjs/grapheme_binder.hpp"
#include "wjs/phase_data.hpp"

namespace wjs::perf {

// The random boards: sizes 3x3 to 6x6, kBoardsPerSize of each. Board `index`
// of a size is drawn from std::mt19937 seeded 7919 * index + size, one draw
// for each tile in row-major order, taken modulo (never through
// std::uniform_int_distribution, whose algorithm is each standard library's
// own, so that the same seed is a different board on MSVC, libstdc++ and
// libc++). The letter is chosen by those draws against ENABLE's letter counts,
// so that the boards hold the letters English does in the proportions it does.
// It is a test corpus, not a set of dice.
inline constexpr std::array<std::size_t, 4> kSizes = {3, 4, 5, 6};
inline constexpr unsigned kBoardsPerSize = 200;

[[nodiscard]] constexpr unsigned corpus_seed(std::size_t size, unsigned index) noexcept {
    return 7919U * index + static_cast<unsigned>(size);
}

[[nodiscard]] wjs::Board random_board(std::size_t size, unsigned index);

// Boards built to hold many words (letters chosen for yield; not dice sets):
// where a search costs the most, and where the result is largest.
struct RichBoard {
    std::string_view name;
    std::size_t size;
    std::string_view text;
};

inline constexpr std::array<RichBoard, 4> kRichBoards = {{
    {"4x4 rich", 4, "S E R S/P A T G/L I N E/S E R S"},
    {"5x5 rich", 5, "S E R S E/P A T G L/L I N E S/S E R S A/T E N I L"},
    {"6x6 rich", 6, "S E R S E A/P A T G L R/L I N E S T/S E R S A I/T E N I L E/R A S T E S"},
    {"6x6 mix", 6, "S T A R E S/E A R T S E/R E S A T R/A S T E R A/T R E S A S/S A R T E E"},
}};

[[nodiscard]] wjs::Board rich_board(const RichBoard& rich);

// Boards no word can be traced on (every tile the same letter): the search's
// cost where it finds nothing.
inline constexpr std::array<RichBoard, 2> kEmptyBoards = {{
    {"4x4 all E", 4, "E E E E/E E E E/E E E E/E E E E"},
    {"6x6 all A", 6, "A A A A A A/A A A A A A/A A A A A A/A A A A A A/A A A A A A/A A A A A A"},
}};

// ---------------------------------------------------------------------------
// The standard board: the project's dice
// ---------------------------------------------------------------------------

// The dice of a size (data/dice/<n>x<n>.txt, the shipped files), loaded once.
// The process ends if a file cannot be read or holds the wrong number of dice:
// the tier has nothing to say without them.
[[nodiscard]] const wjs::DiceSet& dice_set(std::size_t size);

// All four sets, as the game session takes them (wjs::DiceSets), from the same
// files and loaded once.
[[nodiscard]] const wjs::DiceSets& dice_sets();

// Board `index` of a size is a roll of those dice with the seed
// corpus_seed(size, index), the same seeds as the random boards: kBoardsPerSize
// of each size, 3x3 to 6x6.
[[nodiscard]] wjs::Board dice_board(std::size_t size, unsigned index);

// ---------------------------------------------------------------------------
// The phonics board: a test phase, seeds and paths
// ---------------------------------------------------------------------------

// Seeds of each size, seed `index` being corpus_seed(size, index). Fewer than
// the random boards because a seeding is up to twenty solves and a guided fill
// is up to a hundred more.
inline constexpr unsigned kSeedsPerSize = 100;

// TEST DATA, as every phase in this repository is. The phase the seeding is
// measured on is the "proxy index" of docs/testing.md ("Seeding: what was
// measured"): the words of three or more letters in ENABLE that are spelled
// only with S, A, T, P, I and N, each letter a grapheme of its own. The words
// are found by walking the dictionary's trie, so it is the same words for the
// same word list, and the case that holds the count of them (kPhonicsWords in
// budgets.hpp) is what says a changed list changed the corpus.
[[nodiscard]] const wjs::PhaseIndex& phonics_phase();

// The same phase as the two files a data directory holds for it
// (phase-a.graphemes.txt and phase-a.words.txt), so that loading a directory is
// measured on the phase the seeding is.
struct PhaseTexts {
    std::string graphemes;
    std::string words;
};
[[nodiscard]] const PhaseTexts& phonics_phase_texts();

// The sizes the game session plays on the PHONICS board: the first release's 3x3
// and 4x4 (wjs::BoardSize). The seeds are corpus_seed(size, index) for the first
// kSeedsPerSize indexes, the same seeds the seeding rows use. The STANDARD
// board's session is played at every size of kSizes, 3x3 to 6x6, on the same
// seeds.
inline constexpr std::array<std::size_t, 2> kSessionSizes = {3, 4};

// A path of nine tiles for the binder, as the letters of its tiles in order,
// separated by spaces.
struct BindPath {
    std::string_view name;
    std::string_view tiles;
};

// None of the tiles starts a multi-letter grapheme, so each is compared with
// every grapheme the binder holds; every tile binds into a block of two or
// three; and a mix of both.
inline constexpr std::array<BindPath, 3> kBindPaths = {{
    {"none", "S A T P I N S A T"},
    {"all", "C H A I R S H E E"},
    {"mixed", "S H I P R A I N S"},
}};

[[nodiscard]] std::vector<std::string> bind_tiles(const BindPath& path);

// TEST DATA: graphemes of a phase, with placeholder sounds. Thirty-one of more
// than one letter (digraphs and trigraphs of the kind phases three to five
// teach, so that the binder holds about as many as a real phase would), six
// single letters, and two split digraphs, which the binder leaves out.
[[nodiscard]] const std::vector<wjs::Grapheme>& bind_graphemes();

// A binder over those graphemes.
[[nodiscard]] const wjs::GraphemeBinder& phonics_binder();

}  // namespace wjs::perf
