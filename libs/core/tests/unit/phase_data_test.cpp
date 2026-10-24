// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/solver.hpp"

// TEST DATA. Every word, grapheme, sound and clip below was written for these
// tests and is not content for a child: the sounds are placeholders (`/t1/`),
// the words are the ones the product brief itself names, and two made-up
// strings stand in for words that do not decode. None of it is, or stands in
// for, a phase of the application; the real phase files have to be written and
// reviewed by a person qualified to do it.

namespace {

constexpr std::string_view no_review = "# phase: A\n";

constexpr std::string_view review_line =
    "# reviewed-by: Test Person   reviewed-on: 2026-10-20   source: written in the test\n";

// Phase A's graphemes: single letters.
constexpr std::string_view graphemes_a =
    "a  /t1/  clip:t1\n"
    "i  /t2/  clip:t2\n"
    "n  /t3/  clip:t3\n"
    "p  /t4/  clip:t4\n"
    "s  /t5/  clip:t5\n"
    "t  /t6/  clip:t6\n";

// Phase B adds a two-letter grapheme.
constexpr std::string_view graphemes_b = "ai  /t7/  clip:t7\n";

constexpr std::string_view words_a =
    "nip  n i p\n"
    "pan  p a n\n"
    "pat  p a t\n"
    "pin  p i n\n"
    "sat  s a t\n"
    "sit  s i t\n"
    "tap  t a p\n";

constexpr std::string_view words_b =
    "pain  p ai n\n"
    "paint  p ai n t\n"
    "saint  s ai n t\n"
    "stain  s t ai n\n";

wjs::Dictionary english() {
    wjs::Dictionary dictionary;
    for (const std::string_view word :
         {"nip", "pan", "pat", "pin", "sat", "sit", "tap", "pain", "paint", "saint", "stain", "xyz",
          "qqq", "cake", "like", "snow", "cow"}) {
        REQUIRE(dictionary.insert(word));
    }
    return dictionary;
}

std::string file_text(std::string_view header, std::string_view body) {
    return std::string(header) + std::string(body);
}

wjs::WordsFile read_words(const std::string& text, std::string_view name = "words.txt") {
    std::istringstream in(text);
    auto parsed = wjs::parse_words(in, name);
    REQUIRE(parsed.has_value());
    return *parsed;
}

wjs::GraphemesFile read_graphemes(const std::string& text,
                                  std::string_view name = "graphemes.txt") {
    std::istringstream in(text);
    auto parsed = wjs::parse_graphemes(in, name);
    REQUIRE(parsed.has_value());
    return *parsed;
}

// The problems of a words file, as "line: message" so a case can look for the
// part it cares about.
std::vector<std::string> word_problems(const std::string& text) {
    std::istringstream in(text);
    const auto parsed = wjs::parse_words(in, "words.txt");
    REQUIRE_FALSE(parsed.has_value());
    std::vector<std::string> result;
    for (const auto& problem : parsed.error()) {
        result.push_back(std::to_string(problem.line) + ": " + problem.message);
    }
    return result;
}

std::vector<std::string> grapheme_problems(const std::string& text) {
    std::istringstream in(text);
    const auto parsed = wjs::parse_graphemes(in, "graphemes.txt");
    REQUIRE_FALSE(parsed.has_value());
    std::vector<std::string> result;
    for (const auto& problem : parsed.error()) {
        result.push_back(std::to_string(problem.line) + ": " + problem.message);
    }
    return result;
}

// The letters of each grapheme of a word, in order.
std::vector<std::string> letters_of(const wjs::PhaseWord& word) {
    std::vector<std::string> letters;
    for (const auto& grapheme : word.graphemes) {
        letters.push_back(grapheme.letters);
    }
    return letters;
}

bool mentions(const std::vector<std::string>& problems, std::string_view text) {
    return std::ranges::any_of(problems, [&](const std::string& problem) {
        return problem.find(text) != std::string::npos;
    });
}

// Test phases A and B, both unreviewed.
struct Files {
    std::vector<wjs::GraphemesFile> graphemes;
    std::vector<wjs::WordsFile> words;
};

Files phases_a_and_b() {
    Files files;
    files.graphemes.push_back(
        read_graphemes(file_text("# phase: A\n", graphemes_a), "a.graphemes"));
    files.graphemes.push_back(
        read_graphemes(file_text("# phase: B\n", graphemes_b), "b.graphemes"));
    files.words.push_back(read_words(file_text("# phase: A\n", words_a), "a.words"));
    files.words.push_back(read_words(file_text("# phase: B\n", words_b), "b.words"));
    return files;
}

std::vector<std::string> build_problems(const Files& files) {
    const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());
    REQUIRE_FALSE(built.has_value());
    std::vector<std::string> result;
    for (const auto& problem : built.error()) {
        result.push_back(problem.file + ":" + std::to_string(problem.line) + ": " +
                         problem.message);
    }
    return result;
}

}  // namespace

TEST_CASE("a words file is read as words split into graphemes", "[phase]") {
    const auto file = read_words(file_text(no_review, words_b));

    REQUIRE(file.words.size() == 4);
    CHECK(file.header.phase == "A");
    CHECK(file.words[0].word == "pain");
    CHECK(letters_of(file.words[0]) == std::vector<std::string>{"p", "ai", "n"});
    CHECK_FALSE(file.words[0].tricky);
    CHECK(file.words[3].word == "stain");
    CHECK(letters_of(file.words[3]) == std::vector<std::string>{"s", "t", "ai", "n"});
    // Where each grapheme's first letter is in the word.
    CHECK(file.words[3].graphemes[2].first == 2);
    CHECK_FALSE(file.words[3].graphemes[2].split());
    CHECK(file.words[0].line == 2);
}

TEST_CASE("a graphemes file is read as letters, a sound and a clip", "[phase]") {
    const auto file = read_graphemes(file_text("# phase: B\n", graphemes_b));

    REQUIRE(file.graphemes.size() == 1);
    CHECK(file.header.phase == "B");
    CHECK(file.graphemes[0].letters == "ai");
    CHECK(file.graphemes[0].sound == "/t7/");
    CHECK(file.graphemes[0].clip == "t7");
}

TEST_CASE("a sound may be written in any characters between the slashes", "[phase]") {
    // Real sounds are written in a phonetic alphabet, so the reader must pass
    // bytes it does not understand through unchanged.
    const auto file = read_graphemes("# phase: A\nai  /\xC9\x99\xC9\xAA/  clip:ai\n");

    REQUIRE(file.graphemes.size() == 1);
    CHECK(file.graphemes[0].sound == "/\xC9\x99\xC9\xAA/");
}

TEST_CASE("a word marked tricky has no split", "[phase]") {
    const auto file = read_words("# phase: A\nxyz  tricky\n");

    REQUIRE(file.words.size() == 1);
    CHECK(file.words[0].tricky);
    CHECK(file.words[0].graphemes.empty());
}

TEST_CASE("blank lines, comments, tabs, a byte-order mark and carriage returns are tolerated",
          "[phase]") {
    const auto file = read_words("\xEF\xBB\xBF# phase: A\r\n\r\n# a comment\r\nnip\tn  i\tp\r\n");

    CHECK(file.header.phase == "A");
    REQUIRE(file.words.size() == 1);
    CHECK(letters_of(file.words[0]) == std::vector<std::string>{"n", "i", "p"});
}

TEST_CASE("the review header is read, whatever the layout of its line", "[phase][review]") {
    SECTION("all three on one line, as in the format") {
        const auto file = read_words(file_text("# phase: A\n", review_line) + "nip  n i p\n");

        CHECK(file.header.reviewed_by == "Test Person");
        CHECK(file.header.reviewed_on == "2026-10-20");
        CHECK(file.header.source == "written in the test");
        CHECK(file.header.reviewed());
    }

    SECTION("one to a line") {
        const auto file = read_words(
            "# phase: A\n# reviewed-by: Test Person\n"
            "# reviewed-on: 2028-02-29\n# source: x\nnip  n i p\n");

        CHECK(file.header.reviewed());  // 2028 is a leap year
    }

    SECTION("a file with no header is test data") {
        const auto file = read_words(file_text(no_review, words_a));

        CHECK_FALSE(file.header.reviewed());
        CHECK(file.header.reviewed_by.empty());
    }

    SECTION("a header still holding the template's placeholders is test data") {
        const auto file = read_words(
            "# phase: A\n# reviewed-by: <name>   reviewed-on: <date>   source: <where>\n"
            "nip  n i p\n");

        CHECK_FALSE(file.header.reviewed());
    }

    SECTION("an ordinary comment is not a header") {
        const auto file = read_words("# phase: A\n# a list of words\nnip  n i p\n");

        CHECK_FALSE(file.header.reviewed());
    }
}

TEST_CASE("a review that is half given or not a date is a problem, not a pass", "[phase][review]") {
    SECTION("a reviewer and no date") {
        const auto problems = word_problems("# phase: A\n# reviewed-by: Test Person\nnip  n i p\n");
        CHECK(mentions(problems, "both reviewed-by and reviewed-on"));
    }

    SECTION("a date and no reviewer") {
        const auto problems = word_problems("# phase: A\n# reviewed-on: 2026-10-20\nnip  n i p\n");
        CHECK(mentions(problems, "both reviewed-by and reviewed-on"));
    }

    SECTION("a date that is not a date") {
        const auto problems = word_problems(
            "# phase: A\n# reviewed-by: Test Person   reviewed-on: last week\nnip  n i p\n");
        CHECK(mentions(problems, "YYYY-MM-DD"));
    }

    SECTION("a day the month does not have") {
        const auto problems = word_problems(
            "# phase: A\n# reviewed-by: Test Person   reviewed-on: 2026-02-29\nnip  n i p\n");
        CHECK(mentions(problems, "YYYY-MM-DD"));
    }

    SECTION("a month that does not exist") {
        const auto problems = word_problems(
            "# phase: A\n# reviewed-by: Test Person   reviewed-on: 2026-13-01\nnip  n i p\n");
        CHECK(mentions(problems, "YYYY-MM-DD"));
    }

    SECTION("a reviewer and a date and no source") {
        const auto problems = word_problems(
            "# phase: A\n# reviewed-by: Test Person   reviewed-on: 2026-10-20\nnip  n i p\n");
        CHECK(mentions(problems, "says where its list came from"));
    }

    SECTION("a source still in angle brackets is no source") {
        const auto problems = word_problems(
            "# phase: A\n# reviewed-by: Test Person   reviewed-on: 2026-10-20   source: <where>\n"
            "nip  n i p\n");
        CHECK(mentions(problems, "says where its list came from"));
    }
}

TEST_CASE("test data needs no source", "[phase][review]") {
    const auto file = read_words("# phase: A\n# source: a list written in the test\nnip  n i p\n");

    CHECK_FALSE(file.header.reviewed());
    CHECK(file.header.source == "a list written in the test");
}

TEST_CASE("a file has to say which phase it is", "[phase]") {
    SECTION("no phase line") {
        CHECK(mentions(word_problems("nip  n i p\n"), "no '# phase: <name>' header"));
    }

    SECTION("a phase that is not a name") {
        CHECK(mentions(word_problems("# phase: A B\nnip  n i p\n"), "letters and digits"));
    }

    SECTION("a phase given twice") {
        CHECK(mentions(word_problems("# phase: A\n# phase: B\nnip  n i p\n"), "given twice"));
    }
}

TEST_CASE("a bad line in a words file is reported with its line", "[phase]") {
    SECTION("a word that is not lowercase letters") {
        const auto problems = word_problems("# phase: A\nNip  n i p\n");
        CHECK(mentions(problems, "2: 'Nip' is not a lowercase a-z word"));
    }

    SECTION("a word too short to be counted") {
        const auto problems = word_problems("# phase: A\nit  i t\n");
        CHECK(mentions(problems, "fewer than 3 letters"));
    }

    SECTION("a word with nothing after it") {
        const auto problems = word_problems("# phase: A\nnip\n");
        CHECK(mentions(problems, "has no graphemes"));
    }

    SECTION("a grapheme that is not lowercase letters") {
        // A split digraph is written as its two halves; the joined shape a
        // person would reach for is refused by name, with the way to write it.
        const auto problems = word_problems("# phase: A\ntap  t a_e p\n");
        CHECK(mentions(problems, "'a_e'"));
        CHECK(mentions(problems, "halves of a split digraph"));
    }

    SECTION("graphemes that do not spell the word") {
        const auto problems = word_problems("# phase: A\nnip  n i t\n");
        CHECK(mentions(problems, "2: the graphemes of 'nip' spell 'nit'"));
    }

    SECTION("tricky with a split as well") {
        const auto problems = word_problems("# phase: A\nxyz  x tricky\n");
        CHECK(mentions(problems, "stands alone"));
    }

    SECTION("a word twice") {
        const auto problems = word_problems("# phase: A\nnip  n i p\nnip  n i p\n");
        CHECK(mentions(problems, "3: 'nip' is listed twice (first on line 2)"));
    }

    SECTION("words out of order") {
        const auto problems = word_problems("# phase: A\nsit  s i t\nnip  n i p\n");
        CHECK(mentions(problems, "3: 'nip' is out of order (after 'sit')"));
    }

    SECTION("every problem is reported, not the first") {
        const auto problems = word_problems("# phase: A\nNip  n i p\nit  i t\nsat\n");
        CHECK(problems.size() == 3);
    }
}

TEST_CASE("a bad line in a graphemes file is reported with its line", "[phase]") {
    SECTION("the wrong number of fields") {
        CHECK(mentions(grapheme_problems("# phase: A\nai  /t7/\n"), "2: a grapheme line is"));
    }

    SECTION("a sound without slashes") {
        CHECK(mentions(grapheme_problems("# phase: A\nai  t7  clip:t7\n"),
                       "not a sound written between slashes"));
    }

    SECTION("a sound with nothing in it") {
        CHECK(mentions(grapheme_problems("# phase: A\nai  //  clip:t7\n"),
                       "not a sound written between slashes"));
    }

    SECTION("a clip without its prefix") {
        CHECK(mentions(grapheme_problems("# phase: A\nai  /t7/  t7\n"), "'clip:<id>'"));
    }

    SECTION("a grapheme with the same sound twice") {
        const auto problems =
            grapheme_problems("# phase: A\nai  /t7/  clip:t7\nai  /t7/  clip:t8\n");
        CHECK(mentions(problems, "3: 'ai' with the sound /t7/ is listed twice (first on line 2)"));
    }

    SECTION("a split digraph with letters either side of nothing") {
        CHECK(mentions(grapheme_problems("# phase: A\n_e  /t7/  clip:t7\n"), "underscore between"));
        CHECK(mentions(grapheme_problems("# phase: A\na_  /t7/  clip:t7\n"), "underscore between"));
    }

    SECTION("graphemes out of order") {
        const auto problems = grapheme_problems("# phase: A\nn  /t3/  clip:t3\na  /t1/  clip:t1\n");
        CHECK(mentions(problems, "3: 'a' is out of order (after 'n')"));
    }

    SECTION("two graphemes may share a clip") {
        const auto file = read_graphemes("# phase: A\nai  /t7/  clip:t7\nay  /t7/  clip:t7\n");
        CHECK(file.graphemes.size() == 2);
    }

    SECTION("one grapheme may be listed again for another sound") {
        const auto file = read_graphemes("# phase: A\now  /t8/  clip:t8\now  /t9/  clip:t9\n");
        REQUIRE(file.graphemes.size() == 2);
        CHECK(file.graphemes[0].sound == "/t8/");
        CHECK(file.graphemes[1].sound == "/t9/");
    }

    SECTION("a split digraph is a grapheme with an underscore") {
        const auto file = read_graphemes("# phase: A\na_e  /t7/  clip:t7\n");
        REQUIRE(file.graphemes.size() == 1);
        CHECK(file.graphemes[0].split());
    }
}

TEST_CASE("a problem prints as file, line and message", "[phase]") {
    CHECK(wjs::to_string({"words.txt", 4, "no good"}) == "words.txt:4: no good");
    CHECK(wjs::to_string({"words.txt", 0, "no good"}) == "words.txt: no good");
}

TEST_CASE("a phase set is cumulative: a later phase holds the earlier phases' words", "[phase]") {
    const auto files = phases_a_and_b();

    const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());

    REQUIRE(built.has_value());
    REQUIRE(built->phases().size() == 2);

    const auto* a = built->find("A");
    const auto* b = built->find("b");  // case-insensitive
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    CHECK(a->words().size() == 7);
    CHECK(a->graphemes().size() == 6);
    CHECK(a->find_word("pain") == nullptr);
    CHECK(a->find_grapheme("ai") == nullptr);

    CHECK(b->words().size() == 11);
    CHECK(b->graphemes().size() == 7);
    REQUIRE(b->find_word("SIT") != nullptr);  // a phase A word, found in B, in either case
    REQUIRE(b->find_word("pain") != nullptr);
    CHECK(letters_of(*b->find_word("pain")) == std::vector<std::string>{"p", "ai", "n"});
    CHECK(b->find_grapheme("ai")->clip == "t7");

    CHECK(built->find("C") == nullptr);
}

TEST_CASE("phases are in the order of their names: shorter first, then alphabetical", "[phase]") {
    // Written in the wrong order on purpose; "10" is after "2" and not before.
    std::vector<wjs::GraphemesFile> graphemes;
    for (const std::string_view name : {"10", "B", "2", "AA", "A"}) {
        graphemes.push_back(read_graphemes(
            "# phase: " + std::string(name) + "\nx  /t" + std::string(name) + "/  clip:c\n",
            std::string(name) + ".graphemes"));
    }

    const auto built = wjs::PhaseSet::build(graphemes, {}, english());

    REQUIRE(built.has_value());
    std::vector<std::string> order;
    for (const wjs::PhaseIndex& index : built->phases()) {
        order.push_back(index.phase());
    }
    CHECK(order == std::vector<std::string>{"2", "A", "B", "10", "AA"});
}

TEST_CASE("a phase's alphabet is the letters of its graphemes and words", "[phase]") {
    const auto files = phases_a_and_b();
    const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());
    REQUIRE(built.has_value());

    CHECK(built->find("A")->alphabet() == "AINPST");
    CHECK(built->find("B")->alphabet() == "AINPST");
}

TEST_CASE("a phase's words are a dictionary a board can be solved against", "[phase][solver]") {
    const auto files = phases_a_and_b();
    const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());
    REQUIRE(built.has_value());
    const auto board = wjs::Board::parse("S A T/P I N/T N A");
    REQUIRE(board.has_value());

    const auto in_a = wjs::solve(*board, built->find("A")->dictionary());
    const auto in_b = wjs::solve(*board, built->find("B")->dictionary());

    // Phase B's index has every word of phase A and the words that need "ai":
    // SAINT is spelt here (S, A, I, N, T), but "ai" is not a phase A grapheme.
    CHECK(std::ranges::none_of(in_a, [](const wjs::FoundWord& w) { return w.word == "SAINT"; }));
    CHECK(std::ranges::any_of(in_b, [](const wjs::FoundWord& w) { return w.word == "SAINT"; }));
    CHECK(in_b.size() > in_a.size());
}

TEST_CASE("a word must be in the dictionary", "[phase]") {
    auto files = phases_a_and_b();
    files.words[0] = read_words("# phase: A\nnip  n i p\nzap  z a p\n", "a.words");

    CHECK(mentions(build_problems(files), "a.words:3: 'zap' is not in the dictionary"));
}

TEST_CASE("a word may use only graphemes of its phase or an earlier one", "[phase]") {
    SECTION("a later phase's grapheme in an earlier phase's word") {
        auto files = phases_a_and_b();
        files.words[0] = read_words("# phase: A\npain  p ai n\n", "a.words");

        const auto problems = build_problems(files);

        CHECK(mentions(problems, "grapheme 'ai' of 'pain' is not in phase A or an earlier phase"));
    }

    SECTION("an earlier phase's grapheme in a later phase's word") {
        auto files = phases_a_and_b();
        files.words[1] = read_words("# phase: B\nnip  n i p\n", "b.words");
        files.words[0] = read_words("# phase: A\npan  p a n\n", "a.words");

        const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());

        CHECK(built.has_value());
    }

    SECTION("a grapheme nobody defines") {
        auto files = phases_a_and_b();
        files.words[1] = read_words("# phase: B\nsaint  s ai n t\nstain  s t a i n\n", "b.words");
        files.graphemes[0] = read_graphemes("# phase: A\na  /t1/  clip:t1\n", "a.graphemes");

        CHECK(mentions(build_problems(files), "grapheme 'i' of 'stain'"));
    }
}

TEST_CASE("a word may be listed once across all the phases", "[phase]") {
    auto files = phases_a_and_b();
    files.words[1] = read_words("# phase: B\nnip  n i p\npain  p ai n\n", "b.words");

    CHECK(mentions(build_problems(files), "b.words:2: 'nip' is already listed (a.words:2)"));
}

TEST_CASE("a grapheme may be defined once across all the phases", "[phase]") {
    auto files = phases_a_and_b();
    files.graphemes[1] =
        read_graphemes("# phase: B\nai  /t7/  clip:t7\nn  /t3/  clip:t9\n", "b.graphemes");

    CHECK(mentions(build_problems(files),
                   "b.graphemes:3: 'n' with the sound /t3/ is already defined (a.graphemes:4)"));
}

TEST_CASE("a phase has at most one file of each kind", "[phase]") {
    auto files = phases_a_and_b();
    files.words.push_back(read_words("# phase: A\nzzz  z z z\n", "a2.words"));

    CHECK(mentions(build_problems(files), "phase A has a second words file"));
}

TEST_CASE("a tricky word is checked against the dictionary and nothing else", "[phase]") {
    auto files = phases_a_and_b();

    SECTION("in the dictionary") {
        files.words[0] = read_words("# phase: A\nnip  n i p\nxyz  tricky\n", "a.words");

        const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());

        REQUIRE(built.has_value());
        const auto* word = built->find("A")->find_word("xyz");
        REQUIRE(word != nullptr);
        CHECK(word->tricky);
        CHECK(built->find("A")->dictionary().contains("xyz"));
    }

    SECTION("not in the dictionary") {
        files.words[0] = read_words("# phase: A\nnip  n i p\nzzz  tricky\n", "a.words");

        CHECK(mentions(build_problems(files), "'zzz' is not in the dictionary"));
    }
}

TEST_CASE("a phase with only one of its files is a phase", "[phase]") {
    Files files;
    files.graphemes.push_back(read_graphemes(file_text("# phase: A\n", graphemes_a)));
    files.words.push_back(read_words(file_text("# phase: A\n", words_a)));
    // Phase B adds words and no grapheme.
    files.words.push_back(read_words("# phase: B\ntaps  t a p s\n"));
    auto dictionary = english();
    REQUIRE(dictionary.insert("taps"));

    const auto built = wjs::PhaseSet::build(files.graphemes, files.words, dictionary);

    REQUIRE(built.has_value());
    CHECK(built->find("B")->words().size() == 8);
    CHECK(built->find("B")->graphemes().size() == 6);
}

TEST_CASE("a set is reviewed only when every file is", "[phase][review]") {
    auto files = phases_a_and_b();
    const auto reviewed = [](std::string_view phase, std::string_view body) {
        return std::string("# phase: ") + std::string(phase) + "\n" + std::string(review_line) +
               std::string(body);
    };

    SECTION("none of the files") {
        const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());
        REQUIRE(built.has_value());
        CHECK_FALSE(built->reviewed());
        CHECK(built->unreviewed_files().size() == 4);
    }

    SECTION("all of them") {
        files.graphemes[0] = read_graphemes(reviewed("A", graphemes_a), "a.graphemes");
        files.graphemes[1] = read_graphemes(reviewed("B", graphemes_b), "b.graphemes");
        files.words[0] = read_words(reviewed("A", words_a), "a.words");
        files.words[1] = read_words(reviewed("B", words_b), "b.words");

        const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());

        REQUIRE(built.has_value());
        CHECK(built->reviewed());
        CHECK(built->unreviewed_files().empty());
    }

    SECTION("all but one, which is named") {
        files.graphemes[0] = read_graphemes(reviewed("A", graphemes_a), "a.graphemes");
        files.graphemes[1] = read_graphemes(reviewed("B", graphemes_b), "b.graphemes");
        files.words[0] = read_words(reviewed("A", words_a), "a.words");

        const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());

        REQUIRE(built.has_value());
        CHECK_FALSE(built->reviewed());
        CHECK(built->unreviewed_files() == std::vector<std::string>{"b.words"});
    }

    SECTION("no files at all is not a reviewed set") {
        const auto built = wjs::PhaseSet::build({}, {}, english());

        REQUIRE(built.has_value());
        CHECK_FALSE(built->reviewed());
        CHECK(built->phases().empty());
    }
}

namespace {

using wjs::test::TempDirectory;

}  // namespace

TEST_CASE("a directory of phase files is loaded by name", "[phase]") {
    const TempDirectory directory;
    directory.write("phase-a.graphemes.txt", file_text("# phase: A\n", graphemes_a));
    directory.write("phase-a.words.txt", file_text("# phase: A\n", words_a));
    directory.write("phase-B.graphemes.txt", file_text("# phase: B\n", graphemes_b));
    directory.write("phase-B.words.txt", file_text("# phase: B\n", words_b));
    directory.write("notes.md", "not a data file, and not .txt");

    const auto built = wjs::PhaseSet::load_directory(directory.path(), english());

    REQUIRE(built.has_value());
    REQUIRE(built->phases().size() == 2);
    CHECK(built->find("B")->words().size() == 11);
    CHECK_FALSE(built->reviewed());
    CHECK(built->unreviewed_files().size() == 4);
}

TEST_CASE("a directory with a file that does not belong is refused", "[phase]") {
    const TempDirectory directory;
    directory.write("phase-a.words.txt", file_text("# phase: A\n", words_a));
    directory.write("phase-a.graphemes.txt", file_text("# phase: A\n", graphemes_a));

    SECTION("a .txt file of another name") {
        directory.write("extra.txt", "nip\n");

        const auto built = wjs::PhaseSet::load_directory(directory.path(), english());

        REQUIRE_FALSE(built.has_value());
        CHECK(mentions({wjs::to_string(built.error().front())}, "extra.txt"));
    }

    SECTION("a header that names another phase than the file name") {
        directory.write("phase-a.words.txt", file_text("# phase: B\n", words_a));

        const auto built = wjs::PhaseSet::load_directory(directory.path(), english());

        REQUIRE_FALSE(built.has_value());
        CHECK(mentions({wjs::to_string(built.error().front())}, "its header says phase B"));
    }

    SECTION("a file with a problem in it") {
        directory.write("phase-a.words.txt", "# phase: A\nnip  n i t\n");

        const auto built = wjs::PhaseSet::load_directory(directory.path(), english());

        REQUIRE_FALSE(built.has_value());
        CHECK(mentions({wjs::to_string(built.error().front())}, "spell 'nit'"));
    }
}

TEST_CASE("a directory that does not exist is a problem", "[phase]") {
    const TempDirectory directory;

    const auto built = wjs::PhaseSet::load_directory(directory.path() / "missing", english());

    REQUIRE_FALSE(built.has_value());
    CHECK(mentions({built.error().front().message}, "no such directory"));
}

TEST_CASE("a split digraph is written as two halves around what is between", "[phase][split]") {
    const auto file = read_words("# phase: A\ncake  c a_ k _e\n");

    REQUIRE(file.words.size() == 1);
    const auto& graphemes = file.words[0].graphemes;
    REQUIRE(graphemes.size() == 3);
    CHECK(letters_of(file.words[0]) == std::vector<std::string>{"c", "a_e", "k"});

    // Its first half is the second letter, its second half the fourth.
    CHECK(graphemes[1].split());
    CHECK(graphemes[1].first == 1);
    CHECK(graphemes[1].second == 3);
    CHECK(graphemes[2].first == 2);
    CHECK_FALSE(graphemes[0].split());
}

TEST_CASE("a split digraph may have a digraph between its halves", "[phase][split]") {
    const auto file = read_words("# phase: A\nlike  l i_ k _e\n");

    CHECK(letters_of(file.words[0]) == std::vector<std::string>{"l", "i_e", "k"});
    CHECK(file.words[0].graphemes[1].second == 3);
}

TEST_CASE("a split digraph that is not written properly is a problem", "[phase][split]") {
    SECTION("opened and never closed") {
        CHECK(mentions(word_problems("# phase: A\ncake  c a_ k e\n"), "opened and not closed"));
    }

    SECTION("closed and never opened") {
        CHECK(mentions(word_problems("# phase: A\ncake  c a k _e\n"),
                       "closes a split digraph that was not opened"));
    }

    SECTION("with nothing between the halves") {
        CHECK(mentions(word_problems("# phase: A\ncake  c a_ _e k\n"), "nothing between them"));
    }

    SECTION("inside another") {
        CHECK(mentions(word_problems("# phase: A\ncake  c a_ k i_ _e\n"), "inside another"));
    }

    SECTION("with halves that do not spell the word") {
        CHECK(mentions(word_problems("# phase: A\ncake  c a_ k _i\n"),
                       "the graphemes of 'cake' spell 'caki'"));
    }

    SECTION("with a number on the second half") {
        CHECK(mentions(word_problems("# phase: A\ncake  c a_ k _e:2\n"), "is not a grapheme"));
    }
}

TEST_CASE("a split digraph is a grapheme of its phase like any other", "[phase][split]") {
    Files files;
    files.graphemes.push_back(
        read_graphemes("# phase: A\na_e  /t7/  clip:t7\nc  /t1/  clip:t1\nk  /t2/  clip:t2\n"));
    // Sorted by letters: '_' comes before 'c', and "a_e" before "c".
    files.words.push_back(read_words("# phase: A\ncake  c a_ k _e\n"));

    const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());

    REQUIRE(built.has_value());
    const auto& index = *built->find("A");
    const auto* cake = index.find_word("cake");
    REQUIRE(cake != nullptr);
    const auto* sound = index.sound_of(cake->graphemes[1]);
    REQUIRE(sound != nullptr);
    CHECK(sound->letters == "a_e");
    CHECK(sound->sound == "/t7/");

    // The alphabet is letters; the underscore is not one.
    CHECK(index.alphabet() == "ACEK");
}

TEST_CASE("a split digraph that no phase defines is refused", "[phase][split]") {
    Files files;
    files.graphemes.push_back(read_graphemes("# phase: A\nc  /t1/  clip:t1\nk  /t2/  clip:t2\n"));
    files.words.push_back(read_words("# phase: A\ncake  c a_ k _e\n"));

    CHECK(mentions(build_problems(files), "grapheme 'a_e' of 'cake' is not in phase A"));
}

TEST_CASE("a grapheme with two sounds is listed twice and a word picks one", "[phase][sounds]") {
    Files files;
    files.graphemes.push_back(read_graphemes(
        "# phase: A\nc  /t1/  clip:t1\nn  /t3/  clip:t3\now  /t8/  clip:t8\now  /t9/  clip:t9\n"
        "s  /t5/  clip:t5\nw  /t4/  clip:t4\n"));
    files.words.push_back(read_words("# phase: A\ncow  c ow:2\nsnow  s n ow:1\n"));

    const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());

    REQUIRE(built.has_value());
    const auto& index = *built->find("A");
    CHECK(index.sounds_of("ow").size() == 2);
    CHECK(index.sounds_of("OW").size() == 2);
    CHECK(index.sounds_of("zz").empty());

    const auto* cow = index.find_word("cow");
    const auto* snow = index.find_word("snow");
    REQUIRE(cow != nullptr);
    REQUIRE(snow != nullptr);
    CHECK(cow->graphemes[1].sound == 1);
    CHECK(index.sound_of(cow->graphemes[1])->sound == "/t9/");
    CHECK(index.sound_of(snow->graphemes[2])->sound == "/t8/");
    // A grapheme with one sound needs no number.
    CHECK(index.sound_of(cow->graphemes[0])->sound == "/t1/");
}

TEST_CASE("a word has to say which sound when a grapheme has several", "[phase][sounds]") {
    Files files;
    files.graphemes.push_back(
        read_graphemes("# phase: A\nc  /t1/  clip:t1\now  /t8/  clip:t8\now  /t9/  clip:t9\n"));

    SECTION("a number is missing") {
        files.words.push_back(read_words("# phase: A\ncow  c ow\n"));
        CHECK(mentions(build_problems(files), "has 2 sounds in phase A: say which"));
    }

    SECTION("a number is too big") {
        files.words.push_back(read_words("# phase: A\ncow  c ow:3\n"));
        CHECK(mentions(build_problems(files), "is numbered 3 and has only 2 sound(s)"));
    }

    SECTION("a number on a grapheme with one sound is fine when it is 1") {
        files.graphemes[0] = read_graphemes("# phase: A\nc  /t1/  clip:t1\now  /t8/  clip:t8\n");
        files.words.push_back(read_words("# phase: A\ncow  c:1 ow:1\n"));
        CHECK(wjs::PhaseSet::build(files.graphemes, files.words, english()).has_value());
    }

    SECTION("a number that is not a number") {
        CHECK(mentions(word_problems("# phase: A\ncow  c ow:0\n"), "is not a grapheme"));
        CHECK(mentions(word_problems("# phase: A\ncow  c ow:x\n"), "is not a grapheme"));
        CHECK(mentions(word_problems("# phase: A\ncow  c ow:\n"), "is not a grapheme"));
    }
}

TEST_CASE("a second sound may come in a later phase, and the earlier phase cannot use it",
          "[phase][sounds]") {
    Files files;
    files.graphemes.push_back(
        read_graphemes("# phase: A\nc  /t1/  clip:t1\now  /t8/  clip:t8\n", "a.graphemes"));
    files.graphemes.push_back(read_graphemes("# phase: B\now  /t9/  clip:t9\n", "b.graphemes"));

    SECTION("the later phase picks between them") {
        files.words.push_back(read_words("# phase: A\ncow  c ow:1\n", "a.words"));
        files.words.push_back(read_words("# phase: B\nsnow  s n ow:2\n", "b.words"));
        files.graphemes[0] = read_graphemes(
            "# phase: A\nc  /t1/  clip:t1\nn  /t3/  clip:t3\now  /t8/  clip:t8\n"
            "s  /t5/  clip:t5\n",
            "a.graphemes");

        const auto built = wjs::PhaseSet::build(files.graphemes, files.words, english());

        REQUIRE(built.has_value());
        CHECK(built->find("A")->sounds_of("ow").size() == 1);
        CHECK(built->find("B")->sounds_of("ow").size() == 2);
    }

    SECTION("the earlier phase does not have the second sound yet") {
        files.words.push_back(read_words("# phase: A\ncow  c ow:2\n", "a.words"));
        CHECK(mentions(build_problems(files), "is numbered 2 and has only 1 sound(s) in phase A"));
    }
}
