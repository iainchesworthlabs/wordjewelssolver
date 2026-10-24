// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "support/test_support.hpp"
#include "wjs/dictionary.hpp"

TEST_CASE("words are found in either case", "[dictionary]") {
    wjs::Dictionary dictionary;
    REQUIRE(dictionary.insert("Cat"));
    REQUIRE(dictionary.insert("DOG"));

    CHECK(dictionary.size() == 2U);
    CHECK(dictionary.contains("cat"));
    CHECK(dictionary.contains("CAT"));
    CHECK(dictionary.contains("dog"));
    CHECK_FALSE(dictionary.contains("cow"));
}

TEST_CASE("a prefix is a prefix but not a word", "[dictionary]") {
    wjs::Dictionary dictionary;
    REQUIRE(dictionary.insert("planet"));

    CHECK(dictionary.has_prefix("plan"));
    CHECK(dictionary.has_prefix(""));
    CHECK(dictionary.has_prefix("planet"));
    CHECK_FALSE(dictionary.has_prefix("planets"));
    CHECK_FALSE(dictionary.contains("plan"));
    CHECK(dictionary.contains("planet"));
}

TEST_CASE("something that is not a word of letters is refused", "[dictionary]") {
    wjs::Dictionary dictionary;

    CHECK_FALSE(dictionary.insert(""));
    CHECK_FALSE(dictionary.insert("don't"));
    CHECK_FALSE(dictionary.insert("co-op"));
    CHECK_FALSE(dictionary.insert("a1"));
    CHECK(dictionary.size() == 0U);
    CHECK_FALSE(dictionary.contains("dont"));
}

TEST_CASE("a word refused part-way leaves nothing behind", "[dictionary]") {
    wjs::Dictionary dictionary;
    REQUIRE(dictionary.insert("cat"));

    // The bad letter is after a run of good ones: if the walk wrote as it went,
    // "car" and "do" would be prefixes of the refused word.
    CHECK_FALSE(dictionary.insert("car1"));
    CHECK_FALSE(dictionary.insert("do2g"));
    CHECK(dictionary.size() == 1U);
    CHECK_FALSE(dictionary.has_prefix("CAR1"));
    CHECK_FALSE(dictionary.has_prefix("DO2"));
    CHECK_FALSE(dictionary.has_prefix("D"));
    CHECK_FALSE(dictionary.contains("car1"));
    CHECK(dictionary.contains("cat"));
}

TEST_CASE("a word inserted twice is counted once", "[dictionary]") {
    wjs::Dictionary dictionary;
    REQUIRE(dictionary.insert("tea"));
    REQUIRE(dictionary.insert("TEA"));
    REQUIRE(dictionary.insert("team"));

    CHECK(dictionary.size() == 2U);
}

TEST_CASE("a cursor follows a word one letter at a time", "[dictionary]") {
    wjs::Dictionary dictionary;
    REQUIRE(dictionary.insert("TO"));
    REQUIRE(dictionary.insert("TOP"));

    auto cursor = dictionary.step(wjs::Dictionary::root, 'T');
    REQUIRE(cursor != wjs::Dictionary::no_node);
    CHECK_FALSE(dictionary.is_word(cursor));

    cursor = dictionary.step(cursor, 'O');
    REQUIRE(cursor != wjs::Dictionary::no_node);
    CHECK(dictionary.is_word(cursor));

    CHECK(dictionary.step(cursor, 'Z') == wjs::Dictionary::no_node);
    CHECK_FALSE(dictionary.is_word(wjs::Dictionary::no_node));
}

TEST_CASE("a step from no node is no node, and a cursor from elsewhere is nowhere",
          "[dictionary]") {
    wjs::Dictionary dictionary;
    REQUIRE(dictionary.insert("to"));

    CHECK(dictionary.step(wjs::Dictionary::no_node, 'T') == wjs::Dictionary::no_node);
    CHECK(dictionary.step(wjs::Dictionary::no_node, 'O') == wjs::Dictionary::no_node);
    CHECK(dictionary.step(12345U, 'T') == wjs::Dictionary::no_node);
    CHECK_FALSE(dictionary.is_word(wjs::Dictionary::no_node));
}

TEST_CASE("loading skips blanks and lines that are not words", "[dictionary]") {
    std::istringstream words("cat\r\n\r\ndog\n  bird  \ndon't\nco-op\nCAT\n");

    const auto dictionary = wjs::Dictionary::load(words);

    REQUIRE(dictionary.has_value());
    CHECK(dictionary->size() == 3U);
    CHECK(dictionary->contains("cat"));
    CHECK(dictionary->contains("dog"));
    CHECK(dictionary->contains("bird"));
    CHECK_FALSE(dictionary->contains("dont"));
}

TEST_CASE("a byte-order mark does not cost the first word", "[dictionary]") {
    std::istringstream words(
        "\xEF\xBB\xBF"
        "cat\ndog\n");

    const auto dictionary = wjs::Dictionary::load(words);

    REQUIRE(dictionary.has_value());
    CHECK(dictionary->size() == 2U);
    CHECK(dictionary->contains("cat"));
}

TEST_CASE("a missing file is reported, not thrown", "[dictionary]") {
    const wjs::test::TempDirectory dir;
    const auto missing = dir.path() / "no_such_dictionary.txt";

    const auto dictionary = wjs::Dictionary::load_file(missing);

    REQUIRE_FALSE(dictionary.has_value());
    CHECK(dictionary.error() == wjs::DictionaryError::cannot_open);
}

TEST_CASE("a dictionary is read back from a file", "[dictionary]") {
    // A directory of its own, so that two runs at once (two worktrees, two
    // ctest jobs) never read or remove each other's file.
    const wjs::test::TempDirectory dir;
    dir.write("words.txt", "apple\nbanana\n");

    const auto dictionary = wjs::Dictionary::load_file(dir.path() / "words.txt");

    REQUIRE(dictionary.has_value());
    CHECK(dictionary->size() == 2U);
    CHECK(dictionary->contains("banana"));
}
