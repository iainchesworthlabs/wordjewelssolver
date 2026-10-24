// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/store.hpp"

namespace fs = std::filesystem;

namespace {

// ---- helpers ----------------------------------------------------------------

// The checksum line of a store file, worked out here from the format's
// description (FNV-1a over the lines before it) and not by calling the library:
// the tests pin the format, and a file written by hand has to be sealed.
std::string seal(const std::string& body) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : body) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    constexpr std::string_view digits = "0123456789abcdef";
    std::string hex(16, '0');
    for (int i = 15; i >= 0; --i) {
        hex[static_cast<std::size_t>(i)] = digits[hash & 0xF];
        hash >>= 4;
    }
    return body + "end " + hex + "\n";
}

using wjs::test::TempDirectory;

using wjs::test::slurp;
using wjs::test::spit;

std::set<std::string> names_in(const fs::path& directory) {
    std::set<std::string> names;
    for (const auto& entry : fs::directory_iterator(directory)) {
        names.insert(entry.path().filename().string());
    }
    return names;
}

constexpr wjs::Day monday = 20738;  // 2026-10-12

// A 3x3 board whose first three tiles are the letters of `variant`, so that
// each variant is a board of its own (a snapshot is the same board as the last
// when its tiles are the same).
std::string tiles3(int variant = 0) {
    const auto letter = [](int v) { return static_cast<char>('A' + v % 26); };
    std::string tiles;
    tiles += letter(variant);
    tiles += ' ';
    tiles += letter(variant / 26);
    tiles += ' ';
    tiles += letter(variant / 676);
    tiles += "/P I N/T A P";
    return tiles;
}

constexpr std::string_view tiles4 = "S A T P/I N S A/T P I N/A T S P";

wjs::FoundSnapshot found(std::string word, int stars, bool assisted = false,
                         wjs::Day day = monday) {
    return {std::move(word), assisted, day, stars};
}

// A 3x3 standard board: holds `viable_total` words, lists none, has these finds.
wjs::BoardSnapshot board3(std::vector<wjs::FoundSnapshot> finds = {}, int viable_total = 100,
                          int variant = 0) {
    wjs::BoardSnapshot s;
    s.size = 3;
    s.tiles = tiles3(variant);
    s.viable_total = viable_total;
    s.finds = std::move(finds);
    return s;
}

// A 3x3 board that is cleared by its finds.
wjs::BoardSnapshot cleared3(std::vector<wjs::FoundSnapshot> finds, int variant = 0) {
    const int total = static_cast<int>(finds.size());
    return board3(std::move(finds), total, variant);
}

// A 3x3 phonics board of phase A that lists the words it holds.
wjs::BoardSnapshot phonics3(std::vector<std::string> viable, std::vector<wjs::FoundSnapshot> finds,
                            int variant = 0) {
    wjs::BoardSnapshot s;
    s.phase = "A";
    s.size = 3;
    s.tiles = tiles3(variant);
    s.viable = std::move(viable);
    s.finds = std::move(finds);
    return s;
}

wjs::SavedSession playing_session() {
    wjs::SavedSession session;
    session.state = wjs::SavedState::playing;
    session.size = 3;
    session.seed = 4242;
    session.board = phonics3({"sat", "pin", "tap"}, {found("sat", 1)});
    session.board->viable_total = 3;  // as the store keeps them
    session.board->tiles = "A,A,A/P,I,N/T,A,P";
    session.lit = "pin";
    return session;
}

// A store with some of everything in it.
wjs::StoreData busy_data() {
    wjs::Store store = wjs::Store::in_memory();
    wjs::Settings settings;
    settings.help = wjs::HelpLevel::off;
    settings.board = wjs::BoardMode::phonics;
    settings.phase = "B";
    settings.focus_sounds = {"ai", "ee"};
    settings.board_size = 4;
    settings.text_size_percent = 125;
    settings.colour_scheme = wjs::ColourScheme::dark;
    settings.solo_clock = wjs::SoloClock::count_down;
    settings.solo_clock_minutes = 15;
    store.update_settings(settings);

    auto a = phonics3({"sat", "pin", "tap"}, {found("sat", 1), found("pin", 1, true)});
    REQUIRE(store.record(a, monday).stored);
    a.finds.push_back(found("tap", 1, false, monday + 1));
    REQUIRE(store.record(a, monday + 1).stored);  // cleared, a best

    wjs::BoardSnapshot four;
    four.size = 4;
    four.tiles = std::string(tiles4);
    four.viable_total = 40;
    four.finds = {found("stain", 5, false, monday + 2), found("pain", 2, true, monday + 2)};
    REQUIRE(store.record(four, monday + 2).stored);

    REQUIRE(store.save_session(playing_session()));
    return store.data();
}

}  // namespace

// ---------------------------------------------------------------------------
// The format

TEST_CASE("an empty store is written and read back as it was", "[store]") {
    const wjs::StoreData empty;

    const std::string text = wjs::serialize_store(empty);
    const auto parsed = wjs::parse_store(text);

    REQUIRE(parsed.has_value());
    CHECK(parsed->data == empty);
    CHECK(parsed->from_version == wjs::store_format_version);
}

TEST_CASE("a store with something of everything is written and read back as it was", "[store]") {
    const wjs::StoreData data = busy_data();
    REQUIRE_FALSE(data.finds.empty());
    REQUIRE_FALSE(data.boards.empty());
    REQUIRE_FALSE(data.vault.empty());
    REQUIRE_FALSE(data.days.empty());
    REQUIRE_FALSE(data.best.empty());
    REQUIRE(data.session.has_value());

    const auto parsed = wjs::parse_store(wjs::serialize_store(data));

    REQUIRE(parsed.has_value());
    CHECK(parsed->data == data);
}

TEST_CASE("the same data is the same bytes", "[store]") {
    const wjs::StoreData data = busy_data();
    CHECK(wjs::serialize_store(data) == wjs::serialize_store(data));
}

namespace {

// The data of the golden file below, written out field by field.
wjs::StoreData golden_data() {
    wjs::StoreData data;
    data.settings.help = wjs::HelpLevel::off;
    data.settings.board = wjs::BoardMode::phonics;
    data.settings.phase = "B";
    data.settings.focus_sounds = {"ai", "ee"};
    data.settings.board_size = 4;
    data.settings.colour_scheme = wjs::ColourScheme::dark;
    data.settings.solo_clock = wjs::SoloClock::count_down;
    data.settings.solo_clock_minutes = 15;
    data.settings.text_size_percent = 125;
    data.stars = 12;
    data.next_board = 4;
    data.best = {{3, 5}, {4, 9}};
    data.vault = {{"pin", 20738, 20739, 3, 2}, {"sat", 20738, 20738, 1, 1}};
    data.finds = {{"sat", 1, 3, "A", 20738, false, 2}, {"pin", 3, 3, "A", 20739, true, 2}};
    data.days = {{20738, 0, 1}, {20739, 1, 3}};
    data.boards = {{2, 20738, 3, "A", "S,A,T/P,I,N/T,A,P", 3, {"sat", "pin", "tap"}, 2, 4, false},
                   {3, 20739, 4, "", "S,A,T,P/I,N,S,A/T,P,I,N/A,T,S,P", 40, {}, 0, 0, false}};
    wjs::SavedSession session;
    session.state = wjs::SavedState::playing;
    session.size = 3;
    session.seed = 77;
    wjs::BoardSnapshot board;
    board.phase = "A";
    board.size = 3;
    board.tiles = "S,A,T/P,I,N/T,A,P";
    board.viable = {"sat", "pin", "tap"};
    board.viable_total = 3;
    board.finds = {{"sat", false, 20739, 1}};
    session.board = board;
    session.lit = "pin";
    data.session = session;
    return data;
}

// The version 3 file of golden_data(), as it is on disk: every key spelled out
// here, so that renaming one in the writer or the reader breaks this case
// (and a user's file with it) instead of passing a round trip. The checksum is
// worked out by seal(), above, from the format's description.
const std::string golden_body =
    "wjs-store 3\n"
    "settings help=off board=phonics phase=B focus=ai,ee size=4 sound_it_out=full "
    "celebration=full sound=1 vibration=1 shake=1 motion=system colour=dark timer=down "
    "timer_minutes=15 text=125 font=- language=-\n"
    "total stars=12 next_board=4\n"
    "best 3 5\n"
    "best 4 9\n"
    "word pin 20738 20739 3 2\n"
    "word sat 20738 20738 1 1\n"
    "find sat 1 3 A 20738 0 2\n"
    "find pin 3 3 A 20739 1 2\n"
    "day 20738 0 1\n"
    "day 20739 1 3\n"
    "board 2 20738 3 A 0 2 4 3 S,A,T/P,I,N/T,A,P sat,pin,tap\n"
    "board 3 20739 4 - 0 0 0 40 S,A,T,P/I,N,S,A/T,P,I,N/A,T,S,P -\n"
    "session state=playing size=3 seed=77 lit=pin\n"
    "sboard A 3 S,A,T/P,I,N/T,A,P 3 sat,pin,tap\n"
    "sfind sat 0 20739 1\n";

}  // namespace

TEST_CASE("the version 3 file of known data is these bytes", "[store][golden]") {
    CHECK(wjs::serialize_store(golden_data()) == seal(golden_body));
}

TEST_CASE("the version 3 file of known data is read as that data", "[store][golden]") {
    const auto parsed = wjs::parse_store(seal(golden_body));

    REQUIRE(parsed.has_value());
    CHECK(parsed->from_version == 3);
    CHECK(parsed->data == golden_data());
}

TEST_CASE("the version 2 file of known data is migrated and read as that data",
          "[store][golden][migration]") {
    // The same lines with the version 2 header: the migration changes none of
    // them, as the language is empty.
    std::string body = golden_body;
    body.replace(0, std::string{"wjs-store 3"}.size(), "wjs-store 2");

    const auto parsed = wjs::parse_store(seal(body));

    REQUIRE(parsed.has_value());
    CHECK(parsed->from_version == 2);
    CHECK(parsed->data == golden_data());
}

TEST_CASE("the Chinese of a version 2 file is zh_CN", "[store][migration][language]") {
    const auto parsed = wjs::parse_store(
        seal("wjs-store 2\nsettings help=1 board=standard phase=A focus=- size=3 sound_it_out=full "
             "celebration=full sound=1 vibration=1 shake=1 motion=system colour=system text=100 "
             "font=- language=zh\ntotal stars=0 next_board=1\n"));

    REQUIRE(parsed.has_value());
    CHECK(parsed->from_version == 2);
    CHECK(parsed->data.settings.language == "zh_CN");
}

TEST_CASE("a language the migration does not touch stays as it was",
          "[store][migration][language]") {
    for (const std::string_view code : {"ja", "he", "-"}) {
        const auto parsed = wjs::parse_store(
            seal("wjs-store 2\nsettings help=1 board=standard phase=A focus=- size=3 "
                 "sound_it_out=full celebration=full sound=1 vibration=1 shake=1 motion=system "
                 "colour=system text=100 font=- language=" +
                 std::string{code} + "\ntotal stars=0 next_board=1\n"));

        REQUIRE(parsed.has_value());
        CHECK(parsed->data.settings.language == (code == "-" ? "" : std::string{code}));
    }
}

TEST_CASE("a language is matched without regard to case and is kept in its own",
          "[settings][language]") {
    CHECK(wjs::canonical_language("zh_cn") == "zh_CN");
    CHECK(wjs::canonical_language("ZH_CN") == "zh_CN");
    CHECK(wjs::canonical_language("EN") == "en");
    CHECK(wjs::canonical_language("zh").empty());
    CHECK(wjs::canonical_language("").empty());

    wjs::Settings settings;
    settings.language = "Zh_cN";
    CHECK(settings.normalized().language == "zh_CN");
    settings.language = "zh";
    CHECK(settings.normalized().language.empty());
}

TEST_CASE("the file says its version on the first line and ends with a checksum", "[store]") {
    const std::string text = wjs::serialize_store(wjs::StoreData{});

    CHECK(text.starts_with("wjs-store 3\n"));
    const auto last_line = text.substr(text.rfind('\n', text.size() - 2) + 1);
    CHECK(last_line.starts_with("end "));
    CHECK(last_line.size() == 4 + 16 + 1);
}

TEST_CASE("the file has only the lines the design says the store holds", "[store][privacy]") {
    // Per find: the word, the size, the phase, the day, the start tile. Per board:
    // tiles, size, how many words. Plus stars, the best, the stickers, the days,
    // the settings and the game in progress. Nothing else: no duration, no time of
    // day, no identity.
    const std::set<std::string> allowed = {"wjs-store", "settings", "total", "best",
                                           "word",      "find",     "day",   "board",
                                           "session",   "sboard",   "sfind", "end"};
    const std::string text = wjs::serialize_store(busy_data());

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string tag = line.substr(0, line.find(' '));
        INFO(line);
        CHECK(allowed.contains(tag));
    }
}

TEST_CASE("the file holds no time of day", "[store][privacy]") {
    // A time is written with a colon, or as a count of seconds or milliseconds:
    // ten or more digits. Days are five digits here, and nothing is longer.
    const std::string text = wjs::serialize_store(busy_data());

    CHECK(text.find(':') == std::string::npos);
    std::size_t run = 0;
    std::size_t longest = 0;
    for (const char c : text) {
        run = (c >= '0' && c <= '9') ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    CHECK(longest < 10);
}

TEST_CASE("text that is not a store is refused, whatever it is", "[store]") {
    for (const std::string_view text :
         {"", "\n", "hello", "wjs-store", "wjs-store 1", "wjs-store 1\n", "wjs-store 1\nend\n",
          "wjs-store one\nend 0000000000000000\n", "wjs-store 0\nend 0000000000000000\n",
          "wjs-store -1\nend 0000000000000000\n", "{\"json\": true}", "PK\x03\x04 zip"}) {
        INFO(text);
        const auto parsed = wjs::parse_store(text);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error() == wjs::StoreError::corrupt);
    }
}

TEST_CASE("a file cut short anywhere is refused", "[store]") {
    const std::string text = wjs::serialize_store(busy_data());

    // Cut anywhere but the last newline: a file that lacks only that is whole.
    for (std::size_t length = 0; length + 1 < text.size(); ++length) {
        const auto parsed = wjs::parse_store(std::string_view{text}.substr(0, length));
        INFO("cut at " << length);
        REQUIRE_FALSE(parsed.has_value());
    }
}

TEST_CASE("a file with any one byte changed is refused", "[store]") {
    const std::string text = wjs::serialize_store(busy_data());
    const auto original = wjs::parse_store(text);
    REQUIRE(original.has_value());

    int refused = 0;
    for (std::size_t at = 0; at < text.size(); ++at) {
        std::string damaged = text;
        damaged[at] = static_cast<char>(damaged[at] ^ 0x01);
        const auto parsed = wjs::parse_store(damaged);
        INFO("byte " << at);
        if (!parsed.has_value()) {
            ++refused;
            continue;
        }
        // A change that was read must have been read as the same data.
        CHECK(parsed->data == original->data);
    }
    CHECK(refused > static_cast<int>(text.size()) - 3);
}

TEST_CASE("a file whose line endings were changed to CRLF is still read", "[store]") {
    const wjs::StoreData data = busy_data();
    std::string text = wjs::serialize_store(data);
    std::string crlf;
    for (const char c : text) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }

    const auto parsed = wjs::parse_store(crlf);

    REQUIRE(parsed.has_value());
    CHECK(parsed->data == data);
}

TEST_CASE("a file bigger than the store can be is refused before it is read", "[store]") {
    const std::string big(wjs::max_store_bytes + 1, 'x');

    const auto parsed = wjs::parse_store(big);

    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == wjs::StoreError::too_large);
}

TEST_CASE("a file from a later version is told apart and not read", "[store]") {
    // Not even its checksum is held to this version's: the rest may be anything.
    const auto parsed = wjs::parse_store("wjs-store 4\nsome new line {}\nend nonsense\n");

    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == wjs::StoreError::newer_version);
}

TEST_CASE("a line this version does not know is damage", "[store]") {
    const std::string text = seal("wjs-store 1\nvisit sat 1 2 3\n");

    const auto parsed = wjs::parse_store(text);

    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == wjs::StoreError::corrupt);
}

TEST_CASE("each record is checked, not only the file", "[store]") {
    struct Bad {
        std::string name;
        std::string line;
    };
    const std::vector<Bad> bad = {
        {"a word with a capital", "word Sat 1 1 1 1"},
        {"a word that is too short", "word at 1 1 1 1"},
        {"a word with a digit", "word sa7 1 1 1 1"},
        {"a word too long", "word aaaaaaaaaaaaaaaaaaaaa 1 1 1 1"},
        {"last day before first", "word sat 5 4 1 1"},
        {"more unassisted than found", "word sat 1 1 1 2"},
        {"a word line with a field missing", "word sat 1 1 1"},
        {"a find with negative stars", "find sat -1 3 - 1 0 1"},
        {"a find with too many stars", "find sat 99 3 - 1 0 1"},
        {"a find with a size of 0", "find sat 1 0 - 1 0 1"},
        {"a find with a flag of 2", "find sat 1 3 - 1 2 1"},
        {"a find with a bad phase", "find sat 1 3 A/B 1 0 1"},
        {"a board that is not square", "board 1 1 3 - 0 0 0 0 S,A,T/P,I,N -"},
        {"a board that is not a board", "board 1 1 3 - 0 0 0 0 S,A,1/P,I,N/T,A,P -"},
        {"a board with serial 0", "board 0 1 3 - 0 0 0 0 S,A,T/P,I,N/T,A,P -"},
        {"a board listing words that are not words", "board 1 1 3 A 0 0 0 0 S,A,T/P,I,N/T,A,P SAT"},
        {"a best of a size 0", "best 0 3"},
        {"a day with a letter in it", "day 1x 1 1"},
        {"a total with no next board", "total stars=3"},
        {"a session with an unknown state", "session state=asleep size=3 seed=1 lit=-"},
        {"a session of size 0", "session state=box size=0 seed=1 lit=-"},
        {"a session with a seed that is not a number", "session state=box size=3 seed=x lit=-"},
        {"a session with a field missing", "session state=box size=3 seed=1"},
        {"a board of a session before its session", "sboard A 3 S,A,T/P,I,N/T,A,P 1 sat"},
        {"a find of a session before its board", "sfind sat 0 1 1"},
    };
    for (const Bad& b : bad) {
        const std::string text = seal("wjs-store 1\ntotal stars=0 next_board=9\n" + b.line + "\n");
        INFO(b.name);
        const auto parsed = wjs::parse_store(text);
        CHECK_FALSE(parsed.has_value());
    }
}

TEST_CASE("a game in progress is held to the same rules as the rest", "[store][resume]") {
    const std::string head = "wjs-store 1\ntotal stars=0 next_board=1\n";
    const std::string playing = "session state=playing size=3 seed=7 lit=-\n";
    const std::string board = "sboard A 3 S,A,T/P,I,N/T,A,P 2 sat,pin\n";
    // A standard board's saved game: no phase, no list, the count of its words.
    const std::string standard_board = "sboard - 3 S,A,T/P,I,N/T,A,P 30 -\n";

    // The whole is read.
    const auto good = wjs::parse_store(seal(head + playing + board + "sfind sat 0 20738 1\n"));
    REQUIRE(good.has_value());
    REQUIRE(good->data.session.has_value());
    CHECK(good->data.session->state == wjs::SavedState::playing);
    CHECK(good->data.session->seed == 7);
    REQUIRE(good->data.session->board.has_value());
    CHECK(good->data.session->board->finds.size() == 1);

    // A standard board's saved game is read too: by its count, and no list.
    const auto standard = wjs::parse_store(seal(head + playing + standard_board));
    REQUIRE(standard.has_value());
    REQUIRE(standard->data.session->board.has_value());
    CHECK(standard->data.session->board->phase.empty());
    CHECK(standard->data.session->board->viable.empty());
    CHECK(standard->data.session->board->viable_total == 30);

    const std::vector<std::pair<std::string, std::string>> bad = {
        {"a playing session without its board", head + playing},
        {"two sessions", head + playing + board + playing},
        {"two boards", head + playing + board + board},
        {"a box with a board", head + "session state=box size=3 seed=7 lit=-\n" + board},
        {"a lit word that is not on the board",
         head + "session state=playing size=3 seed=7 lit=tap\n" + board},
        {"a lit word while resting",
         head + "session state=resting size=3 seed=7 lit=sat\n" + board},
        {"a size that is not the board's",
         head + "session state=playing size=4 seed=7 lit=-\n" + board},
        {"a board whose list is not all it held",
         head + playing + "sboard A 3 S,A,T/P,I,N/T,A,P 3 sat,pin\n"},
        {"a phonics board with no list", head + playing + "sboard A 3 S,A,T/P,I,N/T,A,P 2 -\n"},
        {"a standard board with no count", head + playing + "sboard - 3 S,A,T/P,I,N/T,A,P 0 -\n"},
        {"a standard board that lists words",
         head + playing + "sboard - 3 S,A,T/P,I,N/T,A,P 30 sat\n"},
        {"a find with a bad flag", head + playing + board + "sfind sat 2 20738 1\n"},
    };
    for (const auto& [name, text] : bad) {
        INFO(name);
        CHECK_FALSE(wjs::parse_store(seal(text)).has_value());
    }
}

TEST_CASE("a repeated settings or total line is damage", "[store]") {
    CHECK_FALSE(
        wjs::parse_store(seal("wjs-store 1\nsettings help=1\nsettings help=0\n")).has_value());
    CHECK_FALSE(wjs::parse_store(seal("wjs-store 1\ntotal stars=0 next_board=1\n"
                                      "total stars=0 next_board=1\n"))
                    .has_value());
}

TEST_CASE("lists out of order, or past their bounds, are damage", "[store]") {
    const std::string vault_out_of_order =
        seal("wjs-store 1\ntotal stars=0 next_board=1\nword tap 1 1 1 1\nword sat 1 1 1 1\n");
    CHECK_FALSE(wjs::parse_store(vault_out_of_order).has_value());

    std::string too_many = "wjs-store 1\ntotal stars=0 next_board=1\n";
    for (std::size_t i = 0; i <= wjs::max_stored_days; ++i) {
        too_many += "day " + std::to_string(100 + i) + " 0 0\n";
    }
    CHECK_FALSE(wjs::parse_store(seal(too_many)).has_value());

    const std::string board_past_next = seal(
        "wjs-store 1\ntotal stars=0 next_board=2\n"
        "board 2 1 3 - 0 0 0 0 S,A,T/P,I,N/T,A,P -\n");
    CHECK_FALSE(wjs::parse_store(board_past_next).has_value());
}

TEST_CASE("a setting this version does not know leaves that setting at its default", "[store]") {
    const std::string text = seal(
        "wjs-store 1\nsettings help=0 board=cubist size=9 flavour=mint text=125 colour=purple\n"
        "total stars=0 next_board=1\n");

    const auto parsed = wjs::parse_store(text);

    REQUIRE(parsed.has_value());
    const wjs::Settings& s = parsed->data.settings;
    CHECK(s.help == wjs::HelpLevel::off);
    CHECK(s.board == wjs::BoardMode::standard);
    CHECK(s.board_size == 3);
    CHECK(s.text_size_percent == 125);
    CHECK(s.colour_scheme == wjs::ColourScheme::system);
}

TEST_CASE("a language is written and read by its code, and old files say the system's",
          "[store][settings][language]") {
    for (const std::string_view code : wjs::languages) {
        wjs::Store store = wjs::Store::in_memory();
        wjs::Settings settings;
        settings.language = std::string{code};
        store.update_settings(settings);

        const std::string text = wjs::serialize_store(store.data());
        CHECK(text.find("language=" + std::string{code}) != std::string::npos);
        const auto parsed = wjs::parse_store(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->data.settings.language == code);
    }

    // A file from before the key: the setting is the system's, empty.
    const auto older = wjs::parse_store(seal(
        "wjs-store 2\nsettings help=1 board=standard phase=A focus=- size=3 sound_it_out=full "
        "celebration=full sound=1 vibration=1 shake=1 motion=system colour=system text=100 font=-\n"
        "total stars=0 next_board=1\n"));
    REQUIRE(older.has_value());
    CHECK(older->data.settings.language.empty());
}

TEST_CASE("a colour scheme is written and read by its name", "[store][settings]") {
    for (const auto scheme :
         {wjs::ColourScheme::system, wjs::ColourScheme::light, wjs::ColourScheme::dark}) {
        wjs::Store store = wjs::Store::in_memory();
        wjs::Settings settings;
        settings.colour_scheme = scheme;
        store.update_settings(settings);

        const std::string text = wjs::serialize_store(store.data());
        const auto parsed = wjs::parse_store(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->data.settings.colour_scheme == scheme);
    }
}

TEST_CASE("a board's clock is written and read by its name and minutes", "[store][settings]") {
    for (const auto clock :
         {wjs::SoloClock::off, wjs::SoloClock::count_up, wjs::SoloClock::count_down}) {
        wjs::Store store = wjs::Store::in_memory();
        wjs::Settings settings;
        settings.solo_clock = clock;
        settings.solo_clock_minutes = 10;
        store.update_settings(settings);

        const std::string text = wjs::serialize_store(store.data());
        const auto parsed = wjs::parse_store(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->data.settings.solo_clock == clock);
        CHECK(parsed->data.settings.solo_clock_minutes == 10);
    }

    // A file from before the keys: the clock is off, the minutes the default.
    const auto older = wjs::parse_store(seal(
        "wjs-store 2\nsettings help=1 board=standard phase=A focus=- size=3 sound_it_out=full "
        "celebration=full sound=1 vibration=1 shake=1 motion=system colour=system text=100 font=- "
        "language=-\n"
        "total stars=0 next_board=1\n"));
    REQUIRE(older.has_value());
    CHECK(older->data.settings.solo_clock == wjs::SoloClock::off);
    CHECK(older->data.settings.solo_clock_minutes == wjs::default_solo_clock_minutes);
}

TEST_CASE("the help level is written and read by its name", "[store][settings]") {
    for (const auto level :
         {wjs::HelpLevel::off, wjs::HelpLevel::start, wjs::HelpLevel::two_letters}) {
        wjs::Store store = wjs::Store::in_memory();
        wjs::Settings settings;
        settings.help = level;
        store.update_settings(settings);

        const std::string text = wjs::serialize_store(store.data());
        const auto parsed = wjs::parse_store(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->data.settings.help == level);
    }

    // A file from before the levels: 1 was the full help, 0 was none.
    const auto on = wjs::parse_store(seal(
        "wjs-store 2\nsettings help=1 board=standard phase=A focus=- size=3 sound_it_out=full "
        "celebration=full sound=1 vibration=1 shake=1 motion=system colour=system text=100 font=- "
        "language=-\n"
        "total stars=0 next_board=1\n"));
    REQUIRE(on.has_value());
    CHECK(on->data.settings.help == wjs::HelpLevel::two_letters);

    const auto off = wjs::parse_store(seal(
        "wjs-store 2\nsettings help=0 board=standard phase=A focus=- size=3 sound_it_out=full "
        "celebration=full sound=1 vibration=1 shake=1 motion=system colour=system text=100 font=- "
        "language=-\n"
        "total stars=0 next_board=1\n"));
    REQUIRE(off.has_value());
    CHECK(off->data.settings.help == wjs::HelpLevel::off);
}

// ---------------------------------------------------------------------------
// Migration

namespace {

// A pretend older format: a version 1 file whose find lines were called "saw".
// The step renames them. This is the mechanism a real step would use.
std::vector<wjs::MigrationStep> rename_saw_to_find() {
    // Built by push_back, not from a braced list: the list copies each step, and
    // GCC at -O3 takes the copy of a std::function from a temporary for a use of
    // something uninitialised.
    std::vector<wjs::MigrationStep> steps;
    steps.push_back(wjs::MigrationStep{1, [](wjs::RawStore& raw) {
                                           for (wjs::RawLine& line : raw.lines) {
                                               if (line.tag == "saw") {
                                                   line.tag = "find";
                                               }
                                           }
                                           return true;
                                       }});
    return steps;
}

const std::string old_file =
    seal("wjs-store 1\ntotal stars=1 next_board=2\nsaw sat 1 3 - 20738 0 1\n");

}  // namespace

TEST_CASE("an older file is migrated up a step at a time", "[store][migration]") {
    const auto steps = rename_saw_to_find();

    const auto parsed = wjs::parse_store(old_file, steps, 2);

    REQUIRE(parsed.has_value());
    CHECK(parsed->from_version == 1);
    REQUIRE(parsed->data.finds.size() == 1);
    CHECK(parsed->data.finds.front().word == "sat");
    CHECK(parsed->data.stars == 1);
}

TEST_CASE("without its migration an older file is damage this program cannot read",
          "[store][migration]") {
    const auto parsed = wjs::parse_store(old_file, {}, 2);

    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == wjs::StoreError::no_migration);
}

TEST_CASE("a migration that refuses the file is told apart", "[store][migration]") {
    std::vector<wjs::MigrationStep> steps;
    steps.push_back(wjs::MigrationStep{1, [](wjs::RawStore&) { return false; }});

    const auto parsed = wjs::parse_store(old_file, steps, 2);

    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == wjs::StoreError::migration_failed);
}

TEST_CASE("migrations run in order, each on what the one before made", "[store][migration]") {
    // 1 -> 2 renames; 2 -> 3 adds a line. A file of version 1 gets both.
    std::vector<wjs::MigrationStep> steps = rename_saw_to_find();
    steps.push_back(wjs::MigrationStep{2, [](wjs::RawStore& raw) {
                                           raw.lines.push_back({"day", {"20738", "0", "7"}});
                                           return true;
                                       }});

    const auto parsed = wjs::parse_store(old_file, steps, 3);

    REQUIRE(parsed.has_value());
    CHECK(parsed->data.finds.size() == 1);
    REQUIRE(parsed->data.days.size() == 1);
    CHECK(parsed->data.days.front().stars == 7);
}

TEST_CASE("the program's own version has no gaps in its migrations", "[store][migration]") {
    // From 1 to store_format_version there is one step for each version: when
    // a version 4 is added, its step is too.
    const auto steps = wjs::builtin_migrations();
    for (int from = 1; from < wjs::store_format_version; ++from) {
        CHECK(std::any_of(steps.begin(), steps.end(),
                          [from](const wjs::MigrationStep& s) { return s.from == from; }));
    }
    CHECK(steps.size() == static_cast<std::size_t>(wjs::store_format_version - 1));
}

// ---------------------------------------------------------------------------
// Recording

TEST_CASE("a find earns its stars once however often the snapshot is sent", "[store][record]") {
    auto store = wjs::Store::in_memory();

    const auto first = store.record(board3({found("sat", 1)}), monday);
    REQUIRE(first.stored);
    CHECK(first.stars_added == 1);
    CHECK(first.new_words == std::vector<std::string>{"sat"});
    CHECK(first.board != 0);

    const auto again = store.record(board3({found("sat", 1)}), monday);
    CHECK(again.stored);
    CHECK(again.board == first.board);
    CHECK(again.stars_added == 0);
    CHECK(again.new_words.empty());

    const auto more = store.record(board3({found("sat", 1), found("tap", 2)}), monday);
    CHECK(more.board == first.board);
    CHECK(more.stars_added == 2);
    CHECK(more.new_words == std::vector<std::string>{"tap"});

    CHECK(store.data().stars == 3);
    CHECK(store.data().finds.size() == 2);
    CHECK(store.data().boards.size() == 1);
}

TEST_CASE("a board with other tiles is another board", "[store][record]") {
    auto store = wjs::Store::in_memory();

    const auto one = store.record(board3({found("sat", 1)}, 100, 0), monday);
    const auto two = store.record(board3({found("sat", 1)}, 100, 1), monday);

    CHECK(one.board != two.board);
    CHECK(store.data().boards.size() == 2);
    CHECK(store.data().stars == 2);
}

TEST_CASE("the same tiles dealt again, with fewer finds, are a new board", "[store][record]") {
    auto store = wjs::Store::in_memory();
    const auto first = store.record(board3({found("sat", 1), found("tap", 1)}), monday);

    const auto again = store.record(board3({found("sat", 1)}), monday);

    CHECK(again.board != first.board);
    CHECK(store.data().boards.size() == 2);
}

TEST_CASE("the same tiles dealt again, with other finds, are a new board and lose none",
          "[store][record]") {
    auto store = wjs::Store::in_memory();
    const auto first = store.record(board3({found("sat", 1), found("tap", 2)}), monday);
    REQUIRE(first.stored);

    // A small phase can deal the very same tiles again. The second game finds
    // three other words: more finds than the first board had, but not the same
    // ones, so it is not the first board sent again.
    const auto second =
        store.record(board3({found("nip", 1), found("pan", 1), found("pin", 3)}, 100), monday + 1);

    REQUIRE(second.stored);
    CHECK(second.board != first.board);
    CHECK(second.stars_added == 5);
    CHECK(second.new_words == std::vector<std::string>{"nip", "pan", "pin"});
    CHECK(store.data().boards.size() == 2);
    CHECK(store.data().finds.size() == 5);
    CHECK(store.data().stars == 8);
    CHECK(wjs::finds_on_board(store.data(), second.board).size() == 3);
    CHECK(wjs::finds_on_board(store.data(), first.board).size() == 2);
}

TEST_CASE("the same word found on another day is not the same find sent again", "[store][record]") {
    auto store = wjs::Store::in_memory();
    const auto first = store.record(board3({found("sat", 1)}), monday);

    // The same tiles and the same first word, a day later: a second game.
    const auto second = store.record(
        board3({found("sat", 1, false, monday + 1), found("tap", 1, false, monday + 1)}),
        monday + 1);

    CHECK(second.board != first.board);
    CHECK(second.stars_added == 2);
    CHECK(store.data().finds.size() == 3);
}

TEST_CASE("a board whose oldest finds were dropped is still the same board when sent again",
          "[store][record][bounds]") {
    auto store = wjs::Store::in_memory();
    // One board with more finds than the store keeps: the oldest of them go.
    std::vector<wjs::FoundSnapshot> finds;
    for (std::size_t i = 0; i < wjs::max_stored_finds + 5; ++i) {
        std::string word = "aaa";
        word[0] = static_cast<char>('a' + i % 26);
        word[1] = static_cast<char>('a' + (i / 26) % 26);
        word[2] = static_cast<char>('a' + (i / 676) % 26);
        finds.push_back(found(word, 1));
    }
    const auto first = store.record(board3(finds, 100000), monday);
    REQUIRE(first.stored);
    REQUIRE(store.data().finds.size() == wjs::max_stored_finds);

    // The snapshot is sent again with one find more: the same board, one star.
    finds.push_back(found("zzz", 1));
    const auto again = store.record(board3(finds, 100000), monday);

    CHECK(again.board == first.board);
    CHECK(again.stars_added == 1);
    CHECK(store.data().boards.size() == 1);
}

TEST_CASE("a board of another phase or size is another board even with the same tiles",
          "[store][record]") {
    auto store = wjs::Store::in_memory();
    auto standard = board3({found("sat", 1)});
    auto phonics = phonics3({"sat"}, {found("sat", 1)});
    phonics.tiles = standard.tiles;

    const auto one = store.record(standard, monday);
    const auto two = store.record(phonics, monday);

    CHECK(one.board != two.board);
}

TEST_CASE("a find records the word, size, phase, day and whether the start tile was used",
          "[store][record]") {
    auto store = wjs::Store::in_memory();
    auto snap = phonics3({"sat", "pin", "tap"},
                         {found("sat", 1, false, monday + 3), found("pin", 1, true, monday + 3)});

    const auto outcome = store.record(snap, monday + 3);

    const auto& finds = store.data().finds;
    REQUIRE(finds.size() == 2);
    CHECK(finds[0].word == "sat");
    CHECK(finds[0].board_size == 3);
    CHECK(finds[0].phase == "A");
    CHECK(finds[0].day == monday + 3);
    CHECK_FALSE(finds[0].start_tile_used);
    CHECK(finds[0].board == outcome.board);
    CHECK(finds[1].start_tile_used);
}

TEST_CASE("the finds of one board are listed in the order they were found", "[store][record]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(store.record(board3({found("tap", 1), found("sat", 1)}, 100, 1), monday).stored);
    const auto second = store.record(board3({found("pin", 1), found("nip", 1)}, 100, 2), monday);

    const auto finds = wjs::finds_on_board(store.data(), second.board);

    REQUIRE(finds.size() == 2);
    CHECK(finds[0].word == "pin");
    CHECK(finds[1].word == "nip");
}

TEST_CASE("a word is lower cased and a word the store cannot keep refuses the snapshot",
          "[store][record]") {
    auto store = wjs::Store::in_memory();

    CHECK(store.record(board3({found("SAT", 1)}), monday).stored);
    CHECK(store.data().finds.front().word == "sat");

    for (const char* word : {"at", "s4t", "sa t", "", "aaaaaaaaaaaaaaaaaaaaa"}) {
        auto before = store.data();
        const auto outcome = store.record(board3({found("sat", 1), found(word, 1)}), monday);
        INFO(word);
        CHECK_FALSE(outcome.stored);
        CHECK(store.data() == before);
    }
}

TEST_CASE("a board that is not size by size, or not a board, is refused whole", "[store][record]") {
    auto store = wjs::Store::in_memory();

    auto not_square = board3({found("sat", 1)});
    not_square.tiles = "S A T/P I N";
    auto wrong_size = board3({found("sat", 1)});
    wrong_size.size = 4;
    auto not_letters = board3({found("sat", 1)});
    not_letters.tiles = "S A 7/P I N/T A P";
    auto bad_phase = board3({found("sat", 1)});
    bad_phase.phase = "not valid";
    auto negative = board3({found("sat", 1)});
    negative.viable_total = -1;
    auto bad_list = phonics3({"sat", "x"}, {found("sat", 1)});

    for (const auto& snap : {not_square, wrong_size, not_letters, bad_phase, negative, bad_list}) {
        const auto outcome = store.record(snap, monday);
        CHECK_FALSE(outcome.stored);
        CHECK(outcome.board == 0);
    }
    CHECK(store.data() == wjs::StoreData{});
}

TEST_CASE("the board is kept as its tiles, one way to write them", "[store][record]") {
    auto store = wjs::Store::in_memory();
    auto snap = board3();
    snap.tiles = "s a t\np i n\nt a qu";  // another way to write a 3x3 board

    REQUIRE(store.record(snap, monday).stored);

    CHECK(store.data().boards.front().tiles == "S,A,T/P,I,N/T,A,QU");
}

TEST_CASE("a word met for the first time is told once, and the vault counts the finds",
          "[store][record]") {
    auto store = wjs::Store::in_memory();

    REQUIRE(store.record(board3({found("sat", 1)}, 100, 1), monday).stored);
    const auto later =
        store.record(board3({found("sat", 1, true, monday + 2)}, 100, 2), monday + 2);

    CHECK(later.new_words.empty());
    REQUIRE(store.data().vault.size() == 1);
    const wjs::VaultWord& word = store.data().vault.front();
    CHECK(word.word == "sat");
    CHECK(word.first_day == monday);
    CHECK(word.last_day == monday + 2);
    CHECK(word.found == 2);
    CHECK(word.unassisted == 1);
}

TEST_CASE("the vault is kept by word", "[store][record]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(
        store.record(board3({found("tap", 1), found("sat", 1), found("pin", 1)}), monday).stored);

    std::vector<std::string> words;
    for (const auto& w : store.data().vault) {
        words.push_back(w.word);
    }
    CHECK(words == std::vector<std::string>{"pin", "sat", "tap"});
}

TEST_CASE("stars are totalled, per board and per day", "[store][record]") {
    auto store = wjs::Store::in_memory();

    REQUIRE(store.record(board3({found("sat", 1), found("stain", 5)}, 100, 1), monday).stored);
    REQUIRE(store.record(board3({found("pin", 2, false, monday + 1)}, 100, 2), monday + 1).stored);

    CHECK(store.data().stars == 8);
    REQUIRE(store.data().days.size() == 2);
    CHECK(store.data().days[0].day == monday);
    CHECK(store.data().days[0].stars == 6);
    CHECK(store.data().days[1].stars == 2);
    CHECK(store.data().boards.front().stars == 6);
}

TEST_CASE("each find is tallied on the day it was found", "[store][record]") {
    auto store = wjs::Store::in_memory();

    // A board played across midnight: one find on each day.
    REQUIRE(
        store
            .record(board3({found("sat", 1, false, monday), found("tap", 2, false, monday + 1)}),
                    monday + 1)
            .stored);

    REQUIRE(store.data().days.size() == 2);
    CHECK(store.data().days[0].stars == 1);
    CHECK(store.data().days[1].stars == 2);
    CHECK(store.data().boards.front().day == monday + 1);
}

TEST_CASE("a board is cleared when every word it held is found, counted on its day, once",
          "[store][record]") {
    auto store = wjs::Store::in_memory();

    REQUIRE(store.record(board3({found("sat", 1)}, 2), monday).stored);
    CHECK_FALSE(store.data().boards.front().cleared);

    REQUIRE(store.record(board3({found("sat", 1), found("tap", 1)}, 2), monday).stored);
    REQUIRE(store.record(board3({found("sat", 1), found("tap", 1)}, 2), monday).stored);

    CHECK(store.data().boards.front().cleared);
    REQUIRE(store.data().days.size() == 1);
    CHECK(store.data().days.front().boards_cleared == 1);
}

TEST_CASE("a board that lists its words is cleared when they are all found", "[store][record]") {
    auto store = wjs::Store::in_memory();

    REQUIRE(store.record(phonics3({"sat", "pin"}, {found("sat", 1)}), monday).stored);
    CHECK_FALSE(store.data().boards.front().cleared);
    REQUIRE(
        store.record(phonics3({"sat", "pin"}, {found("sat", 1), found("pin", 1)}), monday).stored);

    CHECK(store.data().boards.front().cleared);
}

TEST_CASE("a board that held no word is never cleared", "[store][record]") {
    auto store = wjs::Store::in_memory();

    REQUIRE(store.record(board3({}, 0), monday).stored);

    CHECK_FALSE(store.data().boards.front().cleared);
    CHECK(store.data().days.empty());
}

TEST_CASE("the first cleared board sets a best and has nothing to beat", "[store][record]") {
    auto store = wjs::Store::in_memory();

    const auto outcome = store.record(cleared3({found("sat", 1), found("tap", 2)}), monday);

    CHECK_FALSE(outcome.new_best);
    CHECK(store.best_stars(3) == 3);
    CHECK(store.best_stars(4) == 0);
}

TEST_CASE("a cleared board with more stars than the best is a new best, once", "[store][record]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(store.record(cleared3({found("sat", 1)}, 0), monday).stored);  // best 1

    const auto better = store.record(cleared3({found("sat", 1), found("tap", 2)}, 1), monday);
    CHECK(better.new_best);
    CHECK(store.best_stars(3) == 3);

    const auto same = store.record(cleared3({found("sat", 1), found("tap", 2)}, 2), monday);
    CHECK_FALSE(same.new_best);  // a tie says nothing
    CHECK(store.best_stars(3) == 3);

    const auto worse = store.record(cleared3({found("sat", 1)}, 3), monday);
    CHECK_FALSE(worse.new_best);  // a game that does not beat it says nothing
    CHECK(store.best_stars(3) == 3);
}

TEST_CASE("a new best is told in the moment the board is cleared and not before",
          "[store][record]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(store.record(cleared3({found("sat", 1)}, 0), monday).stored);

    const auto mid = store.record(board3({found("sat", 1), found("tap", 2)}, 3, 1), monday);
    CHECK_FALSE(mid.new_best);
    CHECK(store.best_stars(3) == 1);  // an unfinished board sets no best

    const auto last =
        store.record(board3({found("sat", 1), found("tap", 2), found("pin", 1)}, 3, 1), monday);
    CHECK(last.new_best);
    CHECK(store.best_stars(3) == 4);
}

TEST_CASE("each board size has its own best", "[store][record]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(store.record(cleared3({found("sat", 1)}), monday).stored);
    wjs::BoardSnapshot four;
    four.size = 4;
    four.tiles = std::string(tiles4);
    four.viable_total = 1;
    four.finds = {found("stain", 5)};

    const auto outcome = store.record(four, monday);

    CHECK_FALSE(outcome.new_best);  // the 4x4 had no best of its own to beat
    CHECK(store.best_stars(3) == 1);
    CHECK(store.best_stars(4) == 5);
}

TEST_CASE("a phonics board lists the words it held, a standard board does not", "[store][record]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(store.record(phonics3({"SAT", "pin", "tap"}, {found("sat", 1)}, 1), monday).stored);
    auto standard = board3({found("pin", 1)}, 100, 2);
    standard.viable = {"sat", "pin"};  // whatever the caller sends, a standard board lists none
    REQUIRE(store.record(standard, monday).stored);

    REQUIRE(store.data().boards.size() == 2);
    CHECK(store.data().boards[0].viable == std::vector<std::string>{"sat", "pin", "tap"});
    CHECK(store.data().boards[0].viable_total == 3);
    CHECK(store.data().boards[1].viable.empty());
    CHECK(store.data().boards[1].viable_total == 100);
}

TEST_CASE("a board that holds more words than are listed lists none", "[store][record]") {
    auto store = wjs::Store::in_memory();
    std::vector<std::string> many;
    for (std::size_t i = 0; i <= wjs::max_listed_viable_words; ++i) {
        many.push_back("sat");
    }

    REQUIRE(store.record(phonics3(many, {}), monday).stored);

    CHECK(store.data().boards.front().viable.empty());
    CHECK(store.data().boards.front().viable_total == static_cast<int>(many.size()));
}

TEST_CASE("only the last twenty boards are kept, and the oldest goes", "[store][bounds]") {
    auto store = wjs::Store::in_memory();
    std::uint32_t first_board = 0;
    for (int i = 0; i < 25; ++i) {
        const auto outcome =
            store.record(board3({found("sat", 1, false, monday + i)}, 100, i), monday + i);
        if (i == 0) {
            first_board = outcome.board;
        }
    }

    REQUIRE(store.data().boards.size() == wjs::max_stored_boards);
    CHECK(store.data().boards.front().serial == first_board + 5);
    CHECK(store.data().boards.back().serial == first_board + 24);
}

TEST_CASE("only the last finds are kept, and the oldest goes", "[store][bounds]") {
    auto store = wjs::Store::in_memory();
    // Many boards of many finds: more than the bound.
    for (int b = 0; b < 12; ++b) {
        std::vector<wjs::FoundSnapshot> all;
        for (int w = 0; w < 40; ++w) {
            all.push_back(found("w" + std::string(1, static_cast<char>('a' + w % 26)) +
                                    std::string(1, static_cast<char>('a' + b)) +
                                    std::string(1, static_cast<char>('a' + w / 26)),
                                1, false, monday + b));
        }
        REQUIRE(store.record(board3(all, 1000, b), monday + b).stored);
    }

    CHECK(store.data().finds.size() == wjs::max_stored_finds);
    CHECK(store.data().stars == 12 * 40);  // the total is not the list
}

TEST_CASE("the day tallies are the last five weeks", "[store][bounds]") {
    auto store = wjs::Store::in_memory();
    for (int i = 0; i < 50; ++i) {
        REQUIRE(
            store.record(board3({found("sat", 1, false, monday + i)}, 100, i), monday + i).stored);
    }

    REQUIRE(store.data().days.size() == wjs::max_stored_days);
    CHECK(store.data().days.front().day == monday + 50 - static_cast<int>(wjs::max_stored_days));
    CHECK(store.data().days.back().day == monday + 49);
}

TEST_CASE("a day long before the others, with no room for it, is not tallied", "[store][bounds]") {
    auto store = wjs::Store::in_memory();
    for (int i = 0; i < 40; ++i) {
        REQUIRE(store
                    .record(board3({found("sat", 1, false, monday + 100 + i)}, 100, i),
                            monday + 100 + i)
                    .stored);
    }
    const auto before = store.data().days;

    REQUIRE(store
                .record(board3({found("tap", 1, false, monday)}, 100, 99),
                        monday)  // a clock that went far back
                .stored);

    CHECK(store.data().days == before);
    CHECK(store.data().stars == 41);  // the stars are still the child's
}

TEST_CASE("the vault is bounded, and gives up the word met longest ago, never the new one",
          "[store][bounds]") {
    auto store = wjs::Store::in_memory();
    // 1,001 distinct words of three letters: aaa, aab, ... one board each.
    std::vector<std::string> words;
    for (char a = 'a'; a <= 'z' && words.size() < wjs::max_vault_words + 1; ++a) {
        for (char b = 'a'; b <= 'z' && words.size() < wjs::max_vault_words + 1; ++b) {
            for (char c = 'a'; c <= 'z' && words.size() < wjs::max_vault_words + 1; ++c) {
                words.push_back({a, b, c});
            }
        }
    }
    REQUIRE(words.size() == wjs::max_vault_words + 1);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const wjs::Day day = monday + static_cast<int>(i / 100);
        REQUIRE(
            store.record(board3({found(words[i], 1, false, day)}, 100, static_cast<int>(i)), day)
                .stored);
    }

    CHECK(store.data().vault.size() == wjs::max_vault_words);
    const auto has = [&store](const std::string& w) {
        return std::any_of(store.data().vault.begin(), store.data().vault.end(),
                           [&w](const wjs::VaultWord& v) { return v.word == w; });
    };
    CHECK(has(words.back()));
    CHECK_FALSE(has(words.front()));  // the word met longest ago
}

TEST_CASE("settings are normalised on the way in", "[store][settings]") {
    auto store = wjs::Store::in_memory();
    wjs::Settings s;
    s.board_size = 9;
    s.text_size_percent = 130;

    store.update_settings(s);

    CHECK(store.settings().board_size == 3);
    CHECK(store.settings().text_size_percent == 100);
}

TEST_CASE("the first reset clears boards and finds and leaves stars and stickers",
          "[store][reset]") {
    auto store = wjs::Store::in_memory();
    auto settings = wjs::Settings{};
    settings.help = wjs::HelpLevel::off;
    store.update_settings(settings);
    REQUIRE(store.record(cleared3({found("sat", 1), found("stain", 5)}), monday).stored);

    store.reset_boards_and_finds();

    CHECK(store.data().boards.empty());
    CHECK(store.data().finds.empty());
    CHECK(store.data().days.empty());
    CHECK(store.data().stars == 6);
    CHECK(store.best_stars(3) == 6);
    CHECK(store.data().vault.size() == 2);
    CHECK(store.settings().help == wjs::HelpLevel::off);
}

TEST_CASE("the second reset clears stars, the best and the stickers", "[store][reset]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(store.record(cleared3({found("sat", 1)}), monday).stored);
    store.reset_boards_and_finds();

    store.reset_stars_and_stickers();

    CHECK(store.data().stars == 0);
    CHECK(store.data().best.empty());
    CHECK(store.data().vault.empty());
}

TEST_CASE("a reset keeps the settings and never reuses a board's number", "[store][reset]") {
    auto store = wjs::Store::in_memory();
    wjs::Settings s;
    s.vibration = false;
    store.update_settings(s);
    const auto first = store.record(board3({found("sat", 1)}), monday);

    store.reset_boards_and_finds();
    store.reset_stars_and_stickers();
    const auto second = store.record(board3({found("sat", 1)}), monday);

    CHECK_FALSE(store.settings().vibration);
    CHECK(second.board > first.board);
}

TEST_CASE("the first reset also forgets a game in progress", "[store][reset][resume]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(store.save_session(playing_session()));

    store.reset_boards_and_finds();

    CHECK_FALSE(store.session().has_value());
}

// ---------------------------------------------------------------------------
// A game in progress

TEST_CASE("a game in progress is kept and given back", "[store][resume]") {
    auto store = wjs::Store::in_memory();

    REQUIRE(store.save_session(playing_session()));

    REQUIRE(store.session().has_value());
    CHECK(*store.session() == playing_session());
}

TEST_CASE("a game in progress is written in lower case and in the store's form",
          "[store][resume]") {
    auto store = wjs::Store::in_memory();
    auto session = playing_session();
    session.board->viable = {"SAT", "PIN", "TAP"};
    session.board->tiles = "s a t\np i n\nt a p";
    session.board->finds = {{"SAT", false, monday, 1}};
    session.lit = "PIN";

    REQUIRE(store.save_session(session));

    const auto& kept = *store.session();
    CHECK(kept.board->viable == std::vector<std::string>{"sat", "pin", "tap"});
    CHECK(kept.board->tiles == "S,A,T/P,I,N/T,A,P");
    CHECK(kept.board->finds.front().word == "sat");
    CHECK(kept.lit == std::optional<std::string>{"pin"});
}

TEST_CASE("the lid on is a game with no board", "[store][resume]") {
    auto store = wjs::Store::in_memory();
    wjs::SavedSession box;
    box.state = wjs::SavedState::box;
    box.size = 4;
    box.seed = 11;

    REQUIRE(store.save_session(box));
    CHECK(store.session()->size == 4);
    CHECK(store.session()->seed == 11);
    CHECK_FALSE(store.session()->board.has_value());
}

TEST_CASE("resting is a game with a board and nothing lit", "[store][resume]") {
    auto store = wjs::Store::in_memory();
    auto resting = playing_session();
    resting.state = wjs::SavedState::resting;
    resting.lit.reset();
    resting.board->finds = {found("sat", 1), found("pin", 1), found("tap", 1)};

    CHECK(store.save_session(resting));
}

TEST_CASE("a game that cannot be brought back whole is not kept", "[store][resume]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(store.save_session(playing_session()));
    const auto before = store.data();

    wjs::SavedSession box_with_board = playing_session();
    box_with_board.state = wjs::SavedState::box;
    box_with_board.lit.reset();

    wjs::SavedSession playing_without_board = playing_session();
    playing_without_board.board.reset();

    wjs::SavedSession wrong_size = playing_session();
    wrong_size.size = 4;

    wjs::SavedSession standard_without_count = playing_session();
    standard_without_count.board->phase.clear();  // a standard board lists none
    standard_without_count.board->viable.clear();
    standard_without_count.board->viable_total = 0;  // and says no count

    wjs::SavedSession unlisted = playing_session();
    unlisted.board->viable_total = 10;  // the list is not all the board held

    wjs::SavedSession too_many = playing_session();
    too_many.board->viable.assign(wjs::max_listed_viable_words + 1, "sat");

    wjs::SavedSession lit_off_board = playing_session();
    lit_off_board.lit = "nip";

    wjs::SavedSession lit_when_resting = playing_session();
    lit_when_resting.state = wjs::SavedState::resting;

    wjs::SavedSession bad_word = playing_session();
    bad_word.board->finds.push_back(found("s4t", 1));

    wjs::SavedSession bad_size = playing_session();
    bad_size.size = 0;
    bad_size.board->size = 0;

    for (const auto& session :
         {box_with_board, playing_without_board, wrong_size, standard_without_count, unlisted,
          too_many, lit_off_board, lit_when_resting, bad_word, bad_size}) {
        CHECK_FALSE(store.save_session(session));
        CHECK(store.data() == before);
    }
}

TEST_CASE("a standard game in progress is kept by the count of its words", "[store][resume]") {
    auto store = wjs::Store::in_memory();
    wjs::SavedSession session;
    session.state = wjs::SavedState::playing;
    session.size = 3;
    session.seed = 4242;
    session.board = board3({found("sat", 1)}, 30);
    session.board->tiles = "S,A,T/P,I,N/T,A,P";

    REQUIRE(store.save_session(session));
    REQUIRE(store.session().has_value());
    // As the store keeps a standard board: no phase, no list, the count.
    CHECK(store.session()->board->phase.empty());
    CHECK(store.session()->board->viable.empty());
    CHECK(store.session()->board->viable_total == 30);
    CHECK(store.session()->board->tiles == "S,A,T/P,I,N/T,A,P");

    // Written as the format's standard sboard (a "-" list), and read back whole.
    const std::string text = wjs::serialize_store(store.data());
    CHECK(text.find("sboard - 3 S,A,T/P,I,N/T,A,P 30 -\n") != std::string::npos);
    const auto parsed = wjs::parse_store(text);
    REQUIRE(parsed.has_value());
    CHECK(parsed->data == store.data());

    // A lit word is kept without the list: the session checks it on restore.
    session.lit = "sat";
    auto with_lit = wjs::Store::in_memory();
    REQUIRE(with_lit.save_session(session));
    CHECK(with_lit.session()->lit == std::optional<std::string>{"sat"});
}

TEST_CASE("none clears the game in progress", "[store][resume]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(store.save_session(playing_session()));

    CHECK(store.save_session(std::nullopt));

    CHECK_FALSE(store.session().has_value());
}

TEST_CASE("recording boards does not disturb the game in progress", "[store][resume]") {
    auto store = wjs::Store::in_memory();
    REQUIRE(store.save_session(playing_session()));

    REQUIRE(store.record(board3({found("sat", 1)}), monday).stored);

    CHECK(*store.session() == playing_session());
}

// ---------------------------------------------------------------------------
// The file

TEST_CASE("a store in memory is never saved", "[store][file]") {
    auto store = wjs::Store::in_memory();

    CHECK_FALSE(store.persistent());
    const auto saved = store.save();
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error() == wjs::SaveError::read_only);
}

TEST_CASE("the default path is under the base, in a directory of the program's name",
          "[store][file]") {
    const auto path = wjs::default_store_path(fs::path{"base"});

    CHECK(path == fs::path{"base"} / "WordJewelsSolver" / "store.wjs");
}

TEST_CASE("opening where there is no file gives a new store and makes nothing yet",
          "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "nested" / "store.wjs";

    auto opened = wjs::Store::open(file);

    CHECK(opened.status == wjs::LoadStatus::created);
    CHECK(opened.store.persistent());
    CHECK(opened.store.data() == wjs::StoreData{});
    CHECK_FALSE(fs::exists(file.parent_path()));
}

TEST_CASE("a change is on disk at once, in a directory made for it", "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "nested" / "store.wjs";
    auto opened = wjs::Store::open(file);

    REQUIRE(opened.store.record(board3({found("sat", 1)}), monday).stored);

    REQUIRE(fs::exists(file));
    CHECK(wjs::parse_store(slurp(file)).has_value());
    CHECK_FALSE(opened.store.last_save_error().has_value());
}

TEST_CASE("what was saved is what is opened next time", "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    wjs::StoreData saved;
    {
        auto opened = wjs::Store::open(file);
        REQUIRE(
            opened.store.record(cleared3({found("sat", 1), found("pin", 1, true)}), monday).stored);
        wjs::Settings s;
        s.help = wjs::HelpLevel::off;
        s.focus_sounds = {"ai"};
        opened.store.update_settings(s);
        REQUIRE(opened.store.save_session(playing_session()));
        saved = opened.store.data();
    }

    const auto reopened = wjs::Store::open(file);

    CHECK(reopened.status == wjs::LoadStatus::loaded);
    CHECK(reopened.store.data() == saved);
    CHECK(reopened.kept.empty());
    CHECK(*reopened.store.session() == playing_session());
}

TEST_CASE("the store writes one file and leaves nothing else behind", "[store][file][privacy]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    auto opened = wjs::Store::open(file);

    for (int i = 0; i < 10; ++i) {
        REQUIRE(opened.store.record(board3({found("sat", 1)}, i % 2 == 0 ? 1 : 100, i), monday + i)
                    .stored);
    }
    opened.store.update_settings(wjs::Settings{});
    REQUIRE(opened.store.save_session(playing_session()));
    opened.store.reset_boards_and_finds();

    // No temporary, no backup, no log: the directory holds the store file.
    CHECK(names_in(dir.path()) == std::set<std::string>{"store.wjs"});
}

TEST_CASE("a damaged file is set aside whole, and the program goes on with a new store",
          "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    const std::string damaged = "this is not a store file\n\x01\x02";
    spit(file, damaged);

    auto opened = wjs::Store::open(file);

    CHECK(opened.status == wjs::LoadStatus::unreadable);
    CHECK(opened.store.data() == wjs::StoreData{});
    REQUIRE_FALSE(opened.kept.empty());
    CHECK(slurp(opened.kept) == damaged);  // every byte kept
    CHECK_FALSE(fs::exists(file));         // and out of the way of the next save
    CHECK(opened.store.persistent());

    REQUIRE(opened.store.record(board3({found("sat", 1)}), monday).stored);
    CHECK(wjs::Store::open(file).status == wjs::LoadStatus::loaded);
    CHECK(slurp(opened.kept) == damaged);  // the new store did not touch it
}

TEST_CASE("a second damaged file does not overwrite the first one kept", "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    spit(file, "first damage");
    const auto one = wjs::Store::open(file);
    spit(file, "second damage");

    const auto two = wjs::Store::open(file);

    REQUIRE_FALSE(one.kept.empty());
    REQUIRE_FALSE(two.kept.empty());
    CHECK(one.kept != two.kept);
    CHECK(slurp(one.kept) == "first damage");
    CHECK(slurp(two.kept) == "second damage");
}

TEST_CASE("a file cut short by a power cut is kept, not read", "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    const std::string whole = wjs::serialize_store(busy_data());
    const std::string cut = whole.substr(0, whole.size() / 2);
    spit(file, cut);

    const auto opened = wjs::Store::open(file);

    CHECK(opened.status == wjs::LoadStatus::unreadable);
    CHECK(slurp(opened.kept) == cut);
}

TEST_CASE("a file bigger than the store can be is kept, not read", "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    const std::string big(wjs::max_store_bytes + 10, 'a');
    spit(file, big);

    const auto opened = wjs::Store::open(file);

    CHECK(opened.status == wjs::LoadStatus::unreadable);
    CHECK(fs::file_size(opened.kept) == big.size());
}

TEST_CASE("a file from a later version is left exactly as it is and is not written over",
          "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    const std::string later = "wjs-store 4\nfinds are now called visits\nend 0123456789abcdef\n";
    spit(file, later);

    auto opened = wjs::Store::open(file);
    CHECK(opened.status == wjs::LoadStatus::newer_version);
    CHECK_FALSE(opened.store.persistent());
    CHECK(opened.kept.empty());

    // The program goes on, in memory.
    const auto outcome = opened.store.record(board3({found("sat", 1)}), monday);
    opened.store.update_settings(wjs::Settings{});
    CHECK(outcome.stored);
    CHECK(opened.store.data().stars == 1);

    const auto saved = opened.store.save();
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error() == wjs::SaveError::read_only);
    CHECK(slurp(file) == later);
    CHECK(names_in(dir.path()) == std::set<std::string>{"store.wjs"});
}

TEST_CASE("an older file is read, kept as it was, and replaced by a newer one",
          "[store][file][migration]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    spit(file, old_file);
    const auto steps = rename_saw_to_find();

    // The same program, a version on: version 2 of the format reads version 1.
    auto opened = wjs::Store::open(file, steps, 2);

    CHECK(opened.status == wjs::LoadStatus::migrated);
    REQUIRE_FALSE(opened.kept.empty());
    CHECK(slurp(opened.kept) == old_file);
    CHECK(opened.kept.filename().string() == "store.wjs.v1");
    CHECK(opened.store.data().finds.size() == 1);

    opened.store.update_settings(wjs::Settings{});
    const std::string now = slurp(file);
    CHECK(now.starts_with("wjs-store 2\n"));
    CHECK(slurp(opened.kept) == old_file);  // the older file is still the older file
}

TEST_CASE("an older file with no way up is kept and a new store begins",
          "[store][file][migration]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    spit(file, old_file);

    const auto opened = wjs::Store::open(file, {}, 2);

    CHECK(opened.status == wjs::LoadStatus::unreadable);
    REQUIRE_FALSE(opened.kept.empty());
    CHECK(slurp(opened.kept) == old_file);
}

TEST_CASE("a version 1 file is migrated to version 2 and kept beside the new one",
          "[store][file][migration]") {
    // A real file of the format before this one: a saved phonics game, which is
    // all a version 1 file could hold (it refused a saved board with no list).
    // The built-in step rewrites nothing, because every line of it is already a
    // line version 2 reads, and the file is still kept as store.wjs.v1.
    const std::string old = seal(
        "wjs-store 1\n"
        "total stars=0 next_board=1\n"
        "session state=playing size=3 seed=7 lit=-\n"
        "sboard A 3 S,A,T/P,I,N/T,A,P 2 sat,pin\n"
        "sfind sat 0 20738 1\n");
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    spit(file, old);

    auto opened = wjs::Store::open(file);

    CHECK(opened.status == wjs::LoadStatus::migrated);
    REQUIRE_FALSE(opened.kept.empty());
    CHECK(opened.kept.filename().string() == "store.wjs.v1");
    CHECK(slurp(opened.kept) == old);
    REQUIRE(opened.store.session().has_value());
    CHECK(opened.store.session()->board->phase == "A");

    // The next save is the new version, and the older file is still the older file.
    opened.store.update_settings(wjs::Settings{});
    CHECK(slurp(file).starts_with("wjs-store 3\n"));
    CHECK(slurp(opened.kept) == old);
}

TEST_CASE("a file that cannot be opened is left alone and not written", "[store][file]") {
    const TempDirectory dir;
    // A directory where the file should be: it exists, and it cannot be read as a file.
    const fs::path file = dir.path() / "store.wjs";
    fs::create_directories(file);

    auto opened = wjs::Store::open(file);

    CHECK(opened.status == wjs::LoadStatus::cannot_read);
    CHECK_FALSE(opened.store.persistent());
    REQUIRE(opened.store.record(board3({found("sat", 1)}), monday).stored);
    CHECK(fs::is_directory(file));
}

TEST_CASE("an empty file is damage: set aside, and a new store begins", "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    spit(file, "");

    auto opened = wjs::Store::open(file);

    CHECK(opened.status == wjs::LoadStatus::unreadable);
    REQUIRE_FALSE(opened.kept.empty());
    CHECK(opened.kept.filename().string() == "store.wjs.unreadable");
    CHECK(slurp(opened.kept).empty());
    CHECK_FALSE(fs::exists(file));
    CHECK(opened.store.persistent());
    opened.store.update_settings(wjs::Settings{});
    CHECK(slurp(file).starts_with("wjs-store 3\n"));
}

TEST_CASE("a temporary file left by a stopped save is neither read nor kept", "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    const fs::path stale = dir.path() / "store.wjs.tmp";
    const std::string whole = wjs::serialize_store(busy_data());
    spit(file, whole);
    spit(stale, "half a fi");

    auto opened = wjs::Store::open(file);

    CHECK(opened.status == wjs::LoadStatus::loaded);
    CHECK(opened.store.data() == busy_data());
    CHECK(slurp(file) == whole);

    // The next save writes over it and leaves no temporary file behind.
    opened.store.update_settings(wjs::Settings{});
    CHECK(slurp(file).starts_with("wjs-store 3\n"));
    CHECK_FALSE(fs::exists(stale));
}

TEST_CASE("a damaged file with every name for its keeping taken is left where it is",
          "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    spit(file, "damaged");
    spit(dir.path() / "store.wjs.unreadable", "taken");
    for (int i = 1; i < 100; ++i) {
        spit(dir.path() / ("store.wjs.unreadable." + std::to_string(i)), "taken");
    }

    auto opened = wjs::Store::open(file);

    CHECK(opened.status == wjs::LoadStatus::unreadable);
    CHECK(opened.kept.empty());
    // Nothing could be moved, so nothing may be written over it.
    CHECK_FALSE(opened.store.persistent());
    opened.store.update_settings(wjs::Settings{});
    CHECK(slurp(file) == "damaged");
    CHECK(slurp(dir.path() / "store.wjs.unreadable") == "taken");
}

TEST_CASE("an older file whose copy cannot be made is read and never replaced", "[store][file]") {
    const std::string old = seal(
        "wjs-store 1\n"
        "total stars=3 next_board=1\n");
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    spit(file, old);
    spit(dir.path() / "store.wjs.v1", "taken");
    for (int i = 1; i < 100; ++i) {
        spit(dir.path() / ("store.wjs.v1." + std::to_string(i)), "taken");
    }

    auto opened = wjs::Store::open(file);

    CHECK(opened.status == wjs::LoadStatus::migrated);
    CHECK(opened.kept.empty());
    CHECK(opened.store.data().stars == 3);   // it was understood
    CHECK_FALSE(opened.store.persistent());  // and will not be written over
    opened.store.update_settings(wjs::Settings{});
    CHECK(slurp(file) == old);
}

TEST_CASE("a file and its keeping are named for the file's own characters", "[store][file]") {
    // The temporary and kept names are the file's name with a suffix; a name
    // outside the legacy code page must stay the same name.
    const TempDirectory dir;
    const fs::path file = dir.path() / fs::path(u8"ストア.wjs");

    auto opened = wjs::Store::open(file);
    REQUIRE(opened.status == wjs::LoadStatus::created);
    opened.store.update_settings(wjs::Settings{});
    REQUIRE_FALSE(opened.store.last_save_error().has_value());
    CHECK(slurp(file).starts_with("wjs-store 3\n"));
    // No temporary file left. (Counted, not named: the name is outside the
    // legacy code page, and path::string() would throw for it.)
    CHECK(std::distance(fs::directory_iterator(dir.path()), fs::directory_iterator{}) == 1);

    spit(file, "damaged");
    const auto second = wjs::Store::open(file);
    CHECK(second.status == wjs::LoadStatus::unreadable);
    REQUIRE_FALSE(second.kept.empty());
    CHECK(second.kept.filename().u8string() == fs::path(u8"ストア.wjs.unreadable").u8string());
    CHECK(slurp(second.kept) == "damaged");
}

TEST_CASE("a save that cannot swap the file in fails, leaves nothing behind and says so",
          "[store][file]") {
    const TempDirectory dir;
    const fs::path file = dir.path() / "store.wjs";
    auto opened = wjs::Store::open(file);
    REQUIRE(opened.store.persistent());
    // A directory with something in it where the file goes: the rename cannot
    // replace it.
    fs::create_directories(file);
    spit(file / "inside", "x");

    const auto saved = opened.store.save();

    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error() == wjs::SaveError::cannot_write);
    CHECK_FALSE(fs::exists(dir.path() / "store.wjs.tmp"));
    CHECK(slurp(file / "inside") == "x");
}

TEST_CASE("a game in progress the disk would not take is not reported as kept",
          "[store][file][resume]") {
    const TempDirectory dir;
    const fs::path blocker = dir.path() / "blocked";
    spit(blocker, "i am a file");
    auto opened = wjs::Store::open(blocker / "store.wjs");
    if (!opened.store.persistent()) {
        SKIP("this system reports the blocked path as unreadable and never writes");
    }

    const bool kept = opened.store.save_session(playing_session());

    CHECK_FALSE(kept);
    REQUIRE(opened.store.last_save_error().has_value());
    CHECK(*opened.store.last_save_error() == wjs::SaveError::cannot_write);
    // In memory it is the game in progress all the same, for a retry.
    CHECK(opened.store.session().has_value());
}

TEST_CASE("a game in progress the store refuses is reported as not kept", "[store][resume]") {
    auto store = wjs::Store::in_memory();
    wjs::SavedSession bad = playing_session();
    bad.size = 4;  // the board is 3x3

    CHECK_FALSE(store.save_session(bad));
    CHECK_FALSE(store.session().has_value());
    CHECK(store.save_session(playing_session()));
    CHECK(store.save_session(std::nullopt));
}

TEST_CASE("a disk that refuses is noted and the game goes on", "[store][file]") {
    const TempDirectory dir;
    // The directory for the file is blocked by a plain file of the same name.
    const fs::path blocker = dir.path() / "blocked";
    spit(blocker, "i am a file");
    auto opened = wjs::Store::open(blocker / "store.wjs");

    const auto outcome = opened.store.record(board3({found("sat", 1)}), monday);

    CHECK(outcome.stored);
    CHECK(opened.store.data().stars == 1);
    if (opened.store.persistent()) {
        REQUIRE(opened.store.last_save_error().has_value());
        CHECK(*opened.store.last_save_error() == wjs::SaveError::cannot_write);
    } else {
        // A system that reports the path as unreadable rather than absent does
        // not try to write; the program's side is the same.
        CHECK(opened.status == wjs::LoadStatus::cannot_read);
    }
    CHECK(slurp(blocker) == "i am a file");
}

// ---------------------------------------------------------------------------
// Size

TEST_CASE("a store at every one of its bounds is smaller than the cap", "[store][bounds]") {
    // The worst case the bounds allow: the vault, the finds, the boards and the
    // days full, every word as long as a word may be, every board 6x6 with
    // three-letter tiles listing as many words as may be listed, and a game in
    // progress as large.
    wjs::StoreData worst;
    worst.stars = 1'000'000'000;
    worst.next_board = 4'000'000'000u;
    for (std::size_t i = 0; i < wjs::max_focus_sounds; ++i) {
        worst.settings.focus_sounds.push_back("sound" + std::to_string(100 + i));
    }
    worst.settings.school_font = std::string(wjs::max_font_name_length, 'f');
    for (int size = 3; size <= 6; ++size) {
        worst.best.push_back({size, 999'999});
    }
    for (std::size_t i = 0; i < wjs::max_vault_words; ++i) {
        std::string word(wjs::store_max_word_letters, 'a');
        word.replace(0, 4, std::to_string(1000 + i), 0, 4);
        for (char& c : word) {
            if (c >= '0' && c <= '9') {
                c = static_cast<char>('a' + (c - '0'));
            }
        }
        worst.vault.push_back({word, 2'000'000'000, 2'000'000'001, 4'000'000'000u, 4'000'000'000u});
    }
    std::sort(worst.vault.begin(), worst.vault.end(),
              [](const wjs::VaultWord& a, const wjs::VaultWord& b) { return a.word < b.word; });
    for (std::size_t i = 0; i < wjs::max_stored_finds; ++i) {
        worst.finds.push_back({std::string(wjs::store_max_word_letters, 'z'), wjs::max_find_stars,
                               6, std::string(wjs::max_name_length, 'P'), 2'000'000'000, true,
                               3'999'999'999u});
    }
    for (std::size_t i = 0; i < wjs::max_stored_days; ++i) {
        worst.days.push_back(
            {static_cast<wjs::Day>(2'000'000'000 + i), 4'000'000'000u, 4'000'000'000u});
    }
    std::string tiles;
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 6; ++column) {
            tiles += (column == 0 ? (row == 0 ? "" : "/") : ",");
            tiles += "QQQ";
        }
    }
    const std::vector<std::string> listed(wjs::max_listed_viable_words,
                                          std::string(wjs::store_max_word_letters, 'q'));
    for (std::size_t i = 0; i < wjs::max_stored_boards; ++i) {
        wjs::StoredBoard board;
        board.serial = static_cast<std::uint32_t>(i + 1);
        board.day = 2'000'000'000;
        board.size = 6;
        board.phase = std::string(wjs::max_name_length, 'P');
        board.tiles = tiles;
        board.viable_total = 100'000;
        board.viable = listed;
        board.found = 100'000;
        board.stars = 1'000'000;
        worst.boards.push_back(std::move(board));
    }
    wjs::SavedSession session;
    session.state = wjs::SavedState::playing;
    session.size = 6;
    session.seed = 4'000'000'000u;
    session.lit = std::string(wjs::store_max_word_letters, 'q');
    wjs::BoardSnapshot board;
    board.phase = std::string(wjs::max_name_length, 'P');
    board.size = 6;
    board.tiles = tiles;
    board.viable = listed;
    board.viable_total = static_cast<int>(listed.size());
    for (std::size_t i = 0; i < listed.size(); ++i) {
        board.finds.push_back({std::string(wjs::store_max_word_letters, 'q'), true, 2'000'000'000,
                               wjs::max_find_stars});
    }
    session.board = board;
    worst.session = session;

    const std::string text = wjs::serialize_store(worst);

    CHECK(text.size() < wjs::max_store_bytes);
    // The cap leaves headroom over the worst case, so that a field added to a
    // record does not make a legitimate store "damaged" in the same release; a
    // change that eats the headroom has to raise the cap on purpose
    // (max_store_bytes in store.hpp says how much is there now).
    CHECK(text.size() + 16 * 1024 <= wjs::max_store_bytes);
    // And the worst case is a store this version reads back whole.
    const auto parsed = wjs::parse_store(text);
    REQUIRE(parsed.has_value());
    CHECK(parsed->data.vault.size() == wjs::max_vault_words);
    CHECK(parsed->data.session.has_value());
}

TEST_CASE("a store of a busy few months is a few tens of kilobytes at most", "[store][bounds]") {
    // A 3x3 phonics board a day for 100 days: its three words found each time,
    // from a phase of 200 words.
    auto store = wjs::Store::in_memory();
    std::mt19937 random(7);
    std::vector<std::string> words;
    for (char a = 'a'; a <= 'z'; ++a) {
        for (char b : std::string("aeiou")) {
            for (char c : std::string("bdgmnprst")) {
                words.push_back({a, b, c});
            }
        }
    }
    for (int day = 0; day < 100; ++day) {
        std::vector<std::string> viable = {words[random() % words.size()],
                                           words[random() % words.size()],
                                           words[random() % words.size()]};
        std::vector<wjs::FoundSnapshot> finds;
        for (const auto& w : viable) {
            finds.push_back(found(w, 1, random() % 2 == 0, monday + day));
        }
        REQUIRE(store.record(phonics3(viable, finds, day), monday + day).stored);
    }

    const std::string text = wjs::serialize_store(store.data());

    CHECK(text.size() < 32 * 1024);
}
