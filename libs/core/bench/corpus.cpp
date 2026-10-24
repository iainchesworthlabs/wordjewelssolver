// SPDX-License-Identifier: GPL-3.0-or-later
#include "corpus.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <numeric>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <utility>

#include "measure.hpp"
#include "perf_build.hpp"
#include "wjs/dictionary.hpp"

namespace wjs::perf {
namespace {

// How many times each letter A to Z occurs in the words of data/dictionaries/
// en.txt (ENABLE, 172,823 words, 1,570,540 letters). A fixed table and not a
// count of the file, so that the corpus does not move if the word list does:
// a changed list is a changed dictionary.words budget and a person's decision.
constexpr std::array<std::uint64_t, 26> letter_counts = {
    118'845, 28'836,  64'179, 53'136, 180'795, 19'335,  42'424, 36'493, 141'464,
    2'497,   13'313,  83'427, 44'531, 107'521, 103'536, 46'141, 2'535,  111'063,
    149'454, 104'967, 51'299, 15'363, 11'689,  4'610,   25'637, 7'450};

constexpr std::uint64_t letter_total =
    std::accumulate(letter_counts.begin(), letter_counts.end(), std::uint64_t{0});

// The letter a draw selects: the draw is made a fraction of a million, scaled
// to the letter total, and the first letter whose running count passes it wins.
// Integer arithmetic throughout, so the same draw is the same letter everywhere.
[[nodiscard]] char letter_for(std::uint32_t draw) noexcept {
    const std::uint64_t scaled = (std::uint64_t{draw} % 1'000'000U) * letter_total / 1'000'000U;
    std::uint64_t running = 0;
    for (std::size_t i = 0; i < letter_counts.size(); ++i) {
        running += letter_counts[i];
        if (running > scaled) {
            return static_cast<char>('A' + i);
        }
    }
    return 'Z';
}

[[nodiscard]] wjs::Board parse_or_die(std::string_view text) {
    auto board = wjs::Board::parse(text);
    // A corpus board that does not parse is a mistake in this file, and every
    // case would fail on it; there is nothing to recover to.
    if (!board) {
        std::abort();
    }
    return *std::move(board);
}

}  // namespace

wjs::Board random_board(std::size_t size, unsigned index) {
    std::mt19937 rng(corpus_seed(size, index));
    std::string text;
    for (std::size_t row = 0; row < size; ++row) {
        if (row != 0) {
            text += '/';
        }
        for (std::size_t column = 0; column < size; ++column) {
            if (column != 0) {
                text += ' ';
            }
            text += letter_for(static_cast<std::uint32_t>(rng() % 1'000'000U));
        }
    }
    return parse_or_die(text);
}

wjs::Board rich_board(const RichBoard& rich) {
    return parse_or_die(rich.text);
}

// ---------------------------------------------------------------------------
// The standard board
// ---------------------------------------------------------------------------

const wjs::DiceSets& dice_sets() {
    static const wjs::DiceSets sets = [] {
        auto loaded =
            wjs::DiceSets::load_directory(std::filesystem::path(std::string(kDiceDirectory)));
        // A set that does not load, or is not a board of its name, is a mistake
        // in the data or this file, and every case would fail on it.
        if (!loaded) {
            std::abort();
        }
        return *std::move(loaded);
    }();
    return sets;
}

const wjs::DiceSet& dice_set(std::size_t size) {
    const wjs::DiceSet* set = dice_sets().find(size);
    if (set == nullptr) {
        std::abort();
    }
    return *set;
}

wjs::Board dice_board(std::size_t size, unsigned index) {
    auto board = wjs::roll(dice_set(size), corpus_seed(size, index));
    if (!board) {
        std::abort();
    }
    return *std::move(board);
}

// ---------------------------------------------------------------------------
// The phonics board
// ---------------------------------------------------------------------------

namespace {

// The letters of the test phase, in the order a graphemes file lists them.
constexpr std::string_view phase_letters = "AINPST";

[[nodiscard]] char to_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Every word of three or more letters the trie holds that is spelled with the
// phase's letters only, in byte order: a walk in letter order that reports a
// word before the words that extend it.
void collect_words(const wjs::Dictionary& dictionary, wjs::Dictionary::Cursor at,
                   std::string& spelled, std::vector<std::string>& words) {
    if (spelled.size() >= wjs::min_phase_word_letters && dictionary.is_word(at)) {
        words.push_back(spelled);
    }
    for (const char letter : phase_letters) {
        const wjs::Dictionary::Cursor next = dictionary.step(at, letter);
        if (next == wjs::Dictionary::no_node) {
            continue;
        }
        spelled.push_back(letter);
        collect_words(dictionary, next, spelled, words);
        spelled.pop_back();
    }
}

// A phase's graphemes file text, a line to each of the letters, with
// placeholder sounds.
[[nodiscard]] std::string graphemes_text(const std::vector<std::string>& letters) {
    std::string text = "# phase: A\n";
    for (std::size_t i = 0; i < letters.size(); ++i) {
        const std::string sound = "t" + std::to_string(i + 1);
        text += letters[i] + "  /" + sound + "/  clip:" + sound + "\n";
    }
    return text;
}

template <typename Parsed, typename Parse>
[[nodiscard]] Parsed parse_or_abort(const std::string& text, Parse parse) {
    std::istringstream in(text);
    auto parsed = parse(in, "perf corpus");
    if (!parsed) {
        std::abort();
    }
    return *std::move(parsed);
}

// The test phase's two files, as the text a directory would hold.
[[nodiscard]] PhaseTexts make_phase_texts() {
    std::vector<std::string> letters;
    for (const char letter : phase_letters) {
        letters.emplace_back(1, to_lower(letter));
    }

    std::vector<std::string> words;
    std::string spelled;
    collect_words(english(), wjs::Dictionary::root, spelled, words);
    // A line a word, lower-case as a words file is written: the word, then its
    // letters, each a grapheme.
    std::string words_text = "# phase: A\n";
    for (const std::string& word : words) {
        std::string lower;
        std::string split;
        for (const char letter : word) {
            lower += to_lower(letter);
            if (!split.empty()) {
                split += ' ';
            }
            split += to_lower(letter);
        }
        words_text += lower + "  " + split + "\n";
    }
    return {graphemes_text(letters), std::move(words_text)};
}

[[nodiscard]] wjs::PhaseSet build_phonics_phase() {
    const PhaseTexts& texts = phonics_phase_texts();
    const std::vector<wjs::GraphemesFile> graphemes = {
        parse_or_abort<wjs::GraphemesFile>(texts.graphemes, wjs::parse_graphemes)};
    const std::vector<wjs::WordsFile> files = {
        parse_or_abort<wjs::WordsFile>(texts.words, wjs::parse_words)};
    auto built = wjs::PhaseSet::build(graphemes, files, english());
    if (!built) {
        std::abort();
    }
    return *std::move(built);
}

// The graphemes of the binder's test set, in the order a graphemes file holds
// them (byte order of the letters). Singles and split digraphs are in it on
// purpose: the binder is given a phase's whole list and keeps the multi-letter
// ones.
constexpr std::array<std::string_view, 39> binder_letters = {
    "a",  "a_e", "ai",  "air", "ar",  "au", "aw", "ay", "ch", "ea", "ear", "ee",  "er",
    "ew", "i",   "i_e", "ie",  "igh", "ir", "n",  "ng", "oa", "oe", "oi",  "oo",  "or",
    "ou", "ow",  "oy",  "p",   "ph",  "s",  "sh", "t",  "th", "ue", "ur",  "ure", "wh"};

}  // namespace

const PhaseTexts& phonics_phase_texts() {
    static const PhaseTexts texts = make_phase_texts();
    return texts;
}

const wjs::PhaseIndex& phonics_phase() {
    static const wjs::PhaseSet set = build_phonics_phase();
    static const wjs::PhaseIndex* const index = set.find("A");
    return *index;
}

std::vector<std::string> bind_tiles(const BindPath& path) {
    std::vector<std::string> tiles;
    std::string_view rest = path.tiles;
    while (!rest.empty()) {
        const std::size_t space = rest.find(' ');
        tiles.emplace_back(rest.substr(0, space));
        rest = space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
    }
    return tiles;
}

const std::vector<wjs::Grapheme>& bind_graphemes() {
    static const std::vector<wjs::Grapheme> graphemes = [] {
        const std::vector<std::string> letters(binder_letters.begin(), binder_letters.end());
        auto file =
            parse_or_abort<wjs::GraphemesFile>(graphemes_text(letters), wjs::parse_graphemes);
        return file.graphemes;
    }();
    return graphemes;
}

const wjs::GraphemeBinder& phonics_binder() {
    static const wjs::GraphemeBinder binder{std::span<const wjs::Grapheme>(bind_graphemes())};
    return binder;
}

}  // namespace wjs::perf
