// SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_data.hpp"

#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "wjs/dictionary.hpp"

namespace wjs::app::demo {
namespace {

constexpr std::string_view graphemes_a =
    "# phase: A\n"
    "a  /t1/  clip:t1\n"
    "c  /t8/  clip:t8\n"
    "i  /t2/  clip:t2\n"
    "n  /t3/  clip:t3\n"
    "p  /t4/  clip:t4\n"
    "s  /t5/  clip:t5\n"
    "t  /t6/  clip:t6\n"
    "z  /t9/  clip:t9\n";

constexpr std::string_view graphemes_b =
    "# phase: B\n"
    "ai  /t7/  clip:t7\n"
    "ow  /ta/  clip:ta\n"
    "ow  /tb/  clip:tb\n";

constexpr std::string_view words_a =
    "# phase: A\n"
    "nip  n i p\n"
    "pan  p a n\n"
    "pat  p a t\n"
    "pin  p i n\n"
    "sat  s a t\n"
    "sit  s i t\n"
    "tap  t a p\n"
    "zap  z a p\n";

constexpr std::string_view words_b =
    "# phase: B\n"
    "cow  c ow:1\n"
    "pain  p ai n\n"
    "paint  p ai n t\n"
    "saint  s ai n t\n"
    "stain  s t ai n\n";

wjs::Dictionary english() {
    wjs::Dictionary dictionary;
    for (const std::string_view word : {"nip", "pan", "pat", "pin", "sat", "sit", "tap", "zap",
                                        "cow", "pain", "paint", "saint", "stain"}) {
        static_cast<void>(dictionary.insert(word));
    }
    return dictionary;
}

wjs::FoundSnapshot found(std::string word, int stars, bool assisted, wjs::Day day) {
    return {std::move(word), assisted, day, stars};
}

// A 3x3 board of its own, so that the store takes it as a new board.
std::string tiles(int variant) {
    const auto letter = [](int n) { return static_cast<char>('A' + n % 26); };
    return std::string{letter(variant)} + " " + letter(variant / 26) + " T/P I N/T A P";
}

}  // namespace

wjs::PhaseSet phases() {
    const auto read_graphemes = [](std::string_view text, std::string_view name) {
        std::istringstream in{std::string(text)};
        auto parsed = wjs::parse_graphemes(in, name);
        return std::move(*parsed);
    };
    const auto read_words = [](std::string_view text, std::string_view name) {
        std::istringstream in{std::string(text)};
        auto parsed = wjs::parse_words(in, name);
        return std::move(*parsed);
    };
    auto built = wjs::PhaseSet::build(
        {read_graphemes(graphemes_a, "a.g"), read_graphemes(graphemes_b, "b.g")},
        {read_words(words_a, "a.w"), read_words(words_b, "b.w")}, english());
    return std::move(*built);
}

wjs::Store empty_store() {
    return wjs::Store::in_memory();
}

wjs::Store busy_store(wjs::Day today) {
    wjs::Store store = wjs::Store::in_memory();
    int variant = 0;

    const auto phonics = [&](std::vector<std::string> viable,
                             std::vector<wjs::FoundSnapshot> finds) {
        wjs::BoardSnapshot board;
        board.phase = "B";
        board.size = 3;
        board.tiles = tiles(variant++);
        board.viable = std::move(viable);
        board.finds = std::move(finds);
        static_cast<void>(
            store.record(board, board.finds.empty() ? today : board.finds.back().day));
    };

    // Last week: a few boards of phase B.
    const wjs::Day last_week = today - 9;
    phonics({"pain", "sat", "pin"},
            {found("pain", 2, true, last_week), found("sat", 1, false, last_week),
             found("pin", 1, false, last_week)});
    phonics({"paint", "tap", "nip"},
            {found("tap", 1, false, last_week + 1), found("nip", 1, false, last_week + 1)});

    // This week.
    phonics({"pain", "stain", "sit"},
            {found("stain", 5, false, today - 2), found("sit", 1, false, today - 2),
             found("pain", 2, false, today - 2)});
    phonics({"saint", "pat", "zap"},
            {found("pat", 1, false, today - 1), found("zap", 1, true, today - 1)});
    phonics({"paint", "pan", "tap"},
            {found("paint", 5, false, today), found("pan", 1, false, today)});

    // Standard boards.
    wjs::BoardSnapshot standard;
    standard.size = 4;
    standard.tiles = "S A T P/I N S A/T P I N/A T S P";
    standard.viable_total = 60;
    standard.finds = {found("stain", 5, false, today), found("pins", 1, false, today),
                      found("tans", 1, false, today)};
    static_cast<void>(store.record(standard, today));

    wjs::Settings settings;
    settings.board = wjs::BoardMode::phonics;
    settings.phase = "B";
    settings.focus_sounds = {"ai", "z"};
    store.update_settings(settings);
    return store;
}

}  // namespace wjs::app::demo
