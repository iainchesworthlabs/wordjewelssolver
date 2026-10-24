// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "wjs/dashboard.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/store.hpp"

// TEST DATA. A phase written for these tests with placeholder sounds: not a
// phase of the application, not content for a child. The words are ones the
// product brief itself names, with a made-up sound of one word ("z") to have a
// sound that few words use. The real phase files have to be written and
// reviewed by a person qualified to do it.

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
        REQUIRE(dictionary.insert(word));
    }
    return dictionary;
}

wjs::PhaseSet phases() {
    const auto graphemes = [](std::string_view text, std::string_view name) {
        std::istringstream in{std::string(text)};
        auto parsed = wjs::parse_graphemes(in, name);
        REQUIRE(parsed.has_value());
        return *parsed;
    };
    const auto words = [](std::string_view text, std::string_view name) {
        std::istringstream in{std::string(text)};
        auto parsed = wjs::parse_words(in, name);
        REQUIRE(parsed.has_value());
        return *parsed;
    };
    auto built =
        wjs::PhaseSet::build({graphemes(graphemes_a, "a.g"), graphemes(graphemes_b, "b.g")},
                             {words(words_a, "a.w"), words(words_b, "b.w")}, english());
    REQUIRE(built.has_value());
    return std::move(*built);
}

const wjs::PhaseSet& the_phases() {
    static const wjs::PhaseSet set = phases();
    return set;
}

const wjs::PhaseIndex& phase_a() {
    return *the_phases().find("A");
}
const wjs::PhaseIndex& phase_b() {
    return *the_phases().find("B");
}

constexpr wjs::Day friday = 20742;  // 2026-10-16
constexpr wjs::Day monday = 20738;  // 2026-10-12, the Monday of that week

// Each board is a board of its own: the store takes a snapshot for the same
// board as the last when the tiles are the same, so every call gets other tiles.
std::string unique_tiles() {
    static int next = 0;
    const int v = next++;
    const auto letter = [](int n) { return static_cast<char>('A' + n % 26); };
    std::string tiles;
    tiles += letter(v);
    tiles += ' ';
    tiles += letter(v / 26);
    tiles += ' ';
    tiles += letter(v / 676);
    tiles += "/P I N/T A P";
    return tiles;
}

// A 3x3 board. A phonics board (a phase named) lists its words, and is cleared
// when they are all found; a standard board lists none, and `cleared` says
// whether the finds are all it held.
wjs::BoardSnapshot board(std::string phase, std::vector<std::string> viable,
                         std::vector<wjs::FoundSnapshot> finds, bool cleared = false) {
    wjs::BoardSnapshot s;
    s.size = 3;
    s.tiles = unique_tiles();
    s.phase = std::move(phase);
    s.viable = std::move(viable);
    s.finds = std::move(finds);
    if (s.phase.empty()) {
        s.viable_total = static_cast<int>(s.finds.size()) + (cleared ? 0 : 1);
    }
    return s;
}

wjs::FoundSnapshot found(std::string word, int stars, bool assisted = false) {
    return {std::move(word), assisted, 0, stars};
}

// Records a board with every find made on `day`.
wjs::RecordOutcome put(wjs::Store& store, wjs::BoardSnapshot snapshot, wjs::Day day) {
    for (wjs::FoundSnapshot& find : snapshot.finds) {
        find.day = day;
    }
    return store.record(snapshot, day);
}

const wjs::SoundRow* row(const wjs::Dashboard& d, std::string_view letters) {
    const auto it =
        std::find_if(d.sounds.begin(), d.sounds.end(),
                     [letters](const wjs::SoundRow& r) { return r.letters == letters; });
    return it == d.sounds.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("a store with nothing in it makes an empty dashboard that says nothing", "[dashboard]") {
    const auto store = wjs::Store::in_memory();

    const auto d = wjs::build_dashboard(store.data(), nullptr, friday, 1);

    CHECK(d.empty);
    CHECK_FALSE(d.has_phase_data);
    CHECK(d.sounds.empty());
    CHECK(d.vault.empty());
    CHECK(d.best.empty());
    CHECK_FALSE(d.week.played());
}

TEST_CASE("a store with something in it is not empty", "[dashboard]") {
    auto store = wjs::Store::in_memory();
    put(store, board("", {}, {found("sat", 1)}), friday);

    CHECK_FALSE(wjs::build_dashboard(store.data(), nullptr, friday, 1).empty);
}

TEST_CASE("the sounds are the phase's graphemes, each once, and only with phase data",
          "[dashboard][sounds]") {
    const auto store = wjs::Store::in_memory();

    const auto none = wjs::build_dashboard(store.data(), nullptr, friday, 1);
    const auto a = wjs::build_dashboard(store.data(), &phase_a(), friday, 1);
    const auto b = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    CHECK(none.sounds.empty());
    CHECK(a.has_phase_data);
    CHECK(a.phase == "A");
    CHECK(a.sounds.size() == 8);
    CHECK(b.phase == "B");
    CHECK(b.sounds.size() == 10);  // phase A's eight, "ai", and "ow" once for its two sounds
    CHECK(row(b, "ow") != nullptr);
    CHECK(row(a, "ai") == nullptr);  // a sound outside the phase is not there
}

TEST_CASE("how many words of the phase use a sound", "[dashboard][sounds]") {
    const auto store = wjs::Store::in_memory();

    const auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    REQUIRE(row(d, "ai") != nullptr);
    CHECK(row(d, "ai")->words_in_phase == 4);  // pain, paint, saint, stain
    CHECK(row(d, "z")->words_in_phase == 1);   // zap
    CHECK(row(d, "ow")->words_in_phase == 1);  // cow
    CHECK(row(d, "a")->words_in_phase == 5);   // pan, pat, sat, tap, zap
}

TEST_CASE("a sound that has not come up is not met, and is not a blind spot",
          "[dashboard][sounds]") {
    auto store = wjs::Store::in_memory();
    put(store, board("A", {"sat", "pin"}, {found("sat", 1)}), friday);

    const auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    const wjs::SoundRow* ai = row(d, "ai");
    REQUIRE(ai != nullptr);
    CHECK(ai->boards_met == 0);
    CHECK(ai->boards_found == 0);
    CHECK(ai->words_found == 0);
    CHECK(ai->words_unassisted == 0);
    CHECK_FALSE(ai->often_found);
}

TEST_CASE("a sound is met on the boards a word with it was on", "[dashboard][sounds]") {
    auto store = wjs::Store::in_memory();
    put(store, board("B", {"pain", "sat"}, {found("pain", 2)}), friday);       // ai met, found
    put(store, board("B", {"paint", "pin"}, {found("pin", 1)}), friday);       // ai met, not found
    put(store, board("B", {"sat", "tap", "pin"}, {found("sat", 1)}), friday);  // ai not met
    put(store, board("B", {"stain"}, {}), friday);  // ai met, nothing found

    const auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    const wjs::SoundRow* ai = row(d, "ai");
    REQUIRE(ai != nullptr);
    CHECK(ai->boards_met == 3);
    CHECK(ai->boards_found == 1);
    // "Words with ai have been on 3 boards. 1 was found."
    const wjs::SoundRow* s = row(d, "s");
    CHECK(s->boards_met == 3);  // sat, stain are the only s words listed; pain/paint/pin are not
}

TEST_CASE("a board of the standard mode holds no sounds", "[dashboard][sounds]") {
    auto store = wjs::Store::in_memory();
    put(store, board("", {"pain", "paint"}, {found("pain", 2)}), friday);

    const auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    CHECK(row(d, "ai")->boards_met == 0);
    // But the word found is still a word found, wherever it was found.
    CHECK(row(d, "ai")->words_found == 1);
}

TEST_CASE("found is never more than met, so the pair always reads", "[dashboard][sounds]") {
    auto store = wjs::Store::in_memory();
    // A word found that the store did not list as on the board (a stale list).
    put(store, board("B", {"sat"}, {found("pain", 2)}), friday);

    const auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    CHECK(row(d, "ai")->boards_met == 0);
    CHECK(row(d, "ai")->boards_found == 0);
}

TEST_CASE("found words are counted once each however often they were found",
          "[dashboard][sounds]") {
    auto store = wjs::Store::in_memory();
    for (int i = 0; i < 5; ++i) {
        put(store, board("B", {"pain"}, {found("pain", 2)}), friday);
    }

    const auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    CHECK(row(d, "ai")->words_found == 1);
    CHECK(row(d, "ai")->words_unassisted == 1);
    CHECK(row(d, "ai")->boards_found == 5);
}

TEST_CASE("unassisted means the start tile was not shown for that word", "[dashboard][sounds]") {
    auto store = wjs::Store::in_memory();
    put(store, board("B", {"pain", "paint"}, {found("pain", 2, true), found("paint", 5, false)}),
        friday);

    const auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    CHECK(row(d, "ai")->words_found == 2);
    CHECK(row(d, "ai")->words_unassisted == 1);
}

TEST_CASE(
    "a word found once without the start tile counts as unassisted even if it was also found with "
    "it",
    "[dashboard][sounds]") {
    auto store = wjs::Store::in_memory();
    put(store, board("B", {"pain"}, {found("pain", 2, true)}), friday);
    put(store, board("B", {"pain"}, {found("pain", 2, false)}), friday);

    const auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    CHECK(row(d, "ai")->words_unassisted == 1);
}

TEST_CASE("often found is three or more distinct words found unassisted", "[dashboard][sounds]") {
    auto store = wjs::Store::in_memory();
    put(store, board("B", {"pain", "paint"}, {found("pain", 2), found("paint", 5)}), friday);

    auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);
    CHECK(row(d, "ai")->words_unassisted == 2);
    CHECK_FALSE(row(d, "ai")->often_found);

    // The same word again is not a third word.
    put(store, board("B", {"pain"}, {found("pain", 2)}), friday);
    d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);
    CHECK_FALSE(row(d, "ai")->often_found);

    // Three, but one of them only with the start tile: still two.
    put(store, board("B", {"saint"}, {found("saint", 5, true)}), friday);
    d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);
    CHECK(row(d, "ai")->words_found == 3);
    CHECK(row(d, "ai")->words_unassisted == 2);
    CHECK_FALSE(row(d, "ai")->often_found);

    // A third found on its own.
    put(store, board("B", {"stain"}, {found("stain", 5)}), friday);
    d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);
    CHECK(row(d, "ai")->words_unassisted == 3);
    CHECK(row(d, "ai")->often_found);
}

TEST_CASE("a word that is not in the phase has no sound to count", "[dashboard][sounds]") {
    auto store = wjs::Store::in_memory();
    put(store, board("B", {"stain"}, {found("stain", 5), found("xylophone", 5)}), friday);

    const auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    CHECK(row(d, "ai")->words_found == 1);
}

TEST_CASE("a ticked sound is marked, and few words use it", "[dashboard][sounds]") {
    auto store = wjs::Store::in_memory();
    wjs::Settings settings;
    settings.focus_sounds = {"ai", "z"};
    store.update_settings(settings);

    const auto d = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    CHECK(row(d, "ai")->focus);
    CHECK_FALSE(row(d, "ai")->few_words);  // four words use it
    CHECK(row(d, "z")->focus);
    CHECK(row(d, "z")->few_words);  // one does: seeding falls back to the phase
    CHECK_FALSE(row(d, "s")->focus);
    CHECK_FALSE(row(d, "s")->few_words);  // only a ticked sound is said to have few
}

TEST_CASE("a ticked sound has few words below three, and three is enough", "[dashboard][focus]") {
    // In phase A: z is in one word (zap), s in two (sat, sit), n in three (nip,
    // pan, pin), a in five, and c, taught in the phase, in none yet.
    auto store = wjs::Store::in_memory();
    wjs::Settings settings;
    settings.focus_sounds = {"a", "c", "n", "s", "z"};
    store.update_settings(settings);

    const auto d = wjs::build_dashboard(store.data(), &phase_a(), friday, 1);

    REQUIRE(row(d, "z") != nullptr);
    CHECK(row(d, "z")->words_in_phase == 1);
    CHECK(row(d, "z")->few_words);
    CHECK(row(d, "s")->words_in_phase == 2);
    CHECK(row(d, "s")->few_words);
    CHECK(row(d, "c")->words_in_phase == 0);
    CHECK(row(d, "c")->few_words);
    CHECK(row(d, "n")->words_in_phase == 3);
    CHECK_FALSE(row(d, "n")->few_words);
    CHECK(row(d, "a")->words_in_phase == 5);
    CHECK_FALSE(row(d, "a")->few_words);
    CHECK(wjs::few_words_threshold == 3);
}

TEST_CASE("the sounds a parent can tick are the phase's, each once", "[dashboard][focus]") {
    const auto choices = wjs::focus_sound_choices(phase_b());

    CHECK(choices.size() == 10);
    CHECK(std::count(choices.begin(), choices.end(), "ow") == 1);
    CHECK(std::find(choices.begin(), choices.end(), "ai") != choices.end());
    const auto in_a = wjs::focus_sound_choices(phase_a());
    CHECK(std::find(in_a.begin(), in_a.end(), "ai") == in_a.end());
}

TEST_CASE("a sound outside the phase cannot stay ticked", "[dashboard][focus]") {
    wjs::Settings settings;
    settings.focus_sounds = {"ai", "ee", "s", "zz"};

    const auto in_a = wjs::restrict_focus_sounds(settings, phase_a());
    const auto in_b = wjs::restrict_focus_sounds(settings, phase_b());

    CHECK(in_a.focus_sounds == std::vector<std::string>{"s"});
    CHECK(in_b.focus_sounds == std::vector<std::string>{"ai", "s"});
}

TEST_CASE("this week is the calendar locale's week, boards and stars from the day tallies",
          "[dashboard][week]") {
    auto store = wjs::Store::in_memory();
    // A cleared board and 3 stars on the previous Sunday: not this week.
    put(store, board("", {}, {found("sat", 1), found("tap", 2)}, true), monday - 1);
    // This week: Monday, 2 cleared boards and 7 stars; Friday, a board not cleared.
    put(store, board("", {}, {found("pin", 1), found("stain", 5)}, true), monday);
    put(store, board("", {}, {found("pan", 1)}, true), monday);
    put(store, board("", {}, {found("nip", 1)}, false), friday);

    const auto monday_first = wjs::build_dashboard(store.data(), nullptr, friday, 1);
    CHECK(monday_first.week.first == monday);
    CHECK(monday_first.week.last == monday + 6);
    CHECK(monday_first.week.boards_cleared == 2);
    CHECK(monday_first.week.stars == 8);

    // Sunday-first: the week is Sunday the 11th to Saturday the 17th, so the
    // Sunday before is in it.
    const auto sunday_first = wjs::build_dashboard(store.data(), nullptr, friday, 7);
    CHECK(sunday_first.week.first == monday - 1);
    CHECK(sunday_first.week.boards_cleared == 3);
    CHECK(sunday_first.week.stars == 11);
}

TEST_CASE("the week runs seven days from its first, and both ends are in it", "[dashboard][week]") {
    auto store = wjs::Store::in_memory();
    put(store, board("", {}, {found("sat", 2)}, true), monday - 1);  // the Sunday before
    put(store, board("", {}, {found("sit", 3)}, true), monday);      // the first day
    put(store, board("", {}, {found("tap", 5)}, true), monday + 6);  // the seventh day
    put(store, board("", {}, {found("pan", 7)}, true), monday + 7);  // the next Monday

    const auto monday_first = wjs::build_dashboard(store.data(), nullptr, friday, 1);
    CHECK(monday_first.week.first == monday);
    CHECK(monday_first.week.last == monday + 6);
    CHECK(monday_first.week.stars == 3 + 5);
    CHECK(monday_first.week.boards_cleared == 2);

    // Saturday first: the week is the Saturday before to the Friday, the 16th.
    const auto saturday_first = wjs::build_dashboard(store.data(), nullptr, friday, 6);
    CHECK(saturday_first.week.first == monday - 2);
    CHECK(saturday_first.week.last == monday + 4);
    CHECK(saturday_first.week.stars == 2 + 3);

    // Sunday first: the Sunday before to the Saturday, the 17th.
    const auto sunday_first = wjs::build_dashboard(store.data(), nullptr, friday, 7);
    CHECK(sunday_first.week.last == monday + 5);
    CHECK(sunday_first.week.stars == 2 + 3);
    CHECK(sunday_first.week.boards_cleared == 2);
}

TEST_CASE("a week has been played if anything at all came of it", "[dashboard][week]") {
    CHECK_FALSE(wjs::WeekSummary{}.played());

    wjs::WeekSummary week;
    week.boards_cleared = 1;
    CHECK(week.played());
    week = {};
    week.stars = 1;
    CHECK(week.played());
    week = {};
    week.new_words = 1;
    CHECK(week.played());
    week = {};
    week.longest_word = "sat";
    CHECK(week.played());
}

TEST_CASE("this week counts the words met for the first time this week", "[dashboard][week]") {
    auto store = wjs::Store::in_memory();
    put(store, board("", {}, {found("sat", 1)}), monday - 3);  // before the week
    put(store, board("", {}, {found("sat", 1), found("tap", 1)}), monday + 1);

    const auto d = wjs::build_dashboard(store.data(), nullptr, friday, 1);

    CHECK(d.week.new_words == 1);  // tap; sat was met last week
}

TEST_CASE("the longest word of the week is the longest, the first alphabetically of equal",
          "[dashboard][week]") {
    auto store = wjs::Store::in_memory();
    put(store, board("", {}, {found("stain", 5), found("saint", 5), found("pain", 2)}), monday + 2);
    put(store, board("", {}, {found("paintings", 5)}), monday - 2);  // longer, last week

    const auto d = wjs::build_dashboard(store.data(), nullptr, friday, 1);

    CHECK(d.week.longest_word == "saint");
}

TEST_CASE("a word met before and found again this week can be the longest of the week",
          "[dashboard][week]") {
    auto store = wjs::Store::in_memory();
    put(store, board("", {}, {found("stain", 5)}), monday - 5);
    put(store, board("", {}, {found("stain", 5)}), monday + 1);

    const auto d = wjs::build_dashboard(store.data(), nullptr, friday, 1);

    CHECK(d.week.longest_word == "stain");
    CHECK(d.week.new_words == 0);
}

TEST_CASE("a week with no play says nothing of the week", "[dashboard][week]") {
    auto store = wjs::Store::in_memory();
    put(store, board("", {}, {found("sat", 1)}, true), monday - 20);

    const auto d = wjs::build_dashboard(store.data(), nullptr, friday, 1);

    CHECK_FALSE(d.empty);
    CHECK_FALSE(d.week.played());
    CHECK(d.week.boards_cleared == 0);
    CHECK(d.week.longest_word.empty());
}

TEST_CASE("the vault is the words found, by word, with the day each was first found",
          "[dashboard][vault]") {
    auto store = wjs::Store::in_memory();
    put(store, board("", {}, {found("tap", 1), found("sat", 1)}), monday);
    put(store, board("", {}, {found("pin", 1), found("sat", 1)}), friday);

    const auto d = wjs::build_dashboard(store.data(), nullptr, friday, 1);

    REQUIRE(d.vault.size() == 3);
    CHECK(d.vault[0].word == "pin");
    CHECK(d.vault[0].first_day == friday);
    CHECK(d.vault[1].word == "sat");
    CHECK(d.vault[1].first_day == monday);
    CHECK(d.vault[2].word == "tap");
}

TEST_CASE("the best is shown for each size that has one", "[dashboard][best]") {
    auto store = wjs::Store::in_memory();
    put(store, board("", {}, {found("sat", 1), found("tap", 2)}, true), monday);
    put(store, board("", {}, {found("pin", 1)}, false), monday);  // not cleared: no best

    const auto d = wjs::build_dashboard(store.data(), nullptr, friday, 1);

    REQUIRE(d.best.size() == 1);
    CHECK(d.best.front().size == 3);
    CHECK(d.best.front().stars == 3);
}

TEST_CASE("the dashboard is a function of the store and the day", "[dashboard]") {
    auto store = wjs::Store::in_memory();
    put(store, board("B", {"pain", "sat"}, {found("pain", 2)}), friday);

    const auto one = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);
    const auto two = wjs::build_dashboard(store.data(), &phase_b(), friday, 1);

    CHECK(one.week.stars == two.week.stars);
    REQUIRE(one.sounds.size() == two.sounds.size());
    for (std::size_t i = 0; i < one.sounds.size(); ++i) {
        CHECK(one.sounds[i].boards_met == two.sounds[i].boards_met);
        CHECK(one.sounds[i].letters == two.sounds[i].letters);
    }
}
