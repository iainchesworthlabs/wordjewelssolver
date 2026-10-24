// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"

// What the unit tests share, so that each file does not carry its own copy.
// Header only, in namespace wjs::test; include it as "support/test_support.hpp".
// It holds no test data: the words and phases of a case stay in the file that
// uses them.

namespace wjs::test {

// The board as text, rows by '/', tiles by `separator` ("C A T/X X X" by
// default; "" for the tiles run together, which is how two boards are compared
// when only the letters matter).
[[nodiscard]] inline std::string text_of(const wjs::Board& board,
                                         std::string_view separator = " ") {
    std::string text;
    for (std::size_t row = 0; row < board.rows(); ++row) {
        if (row > 0) {
            text += '/';
        }
        for (std::size_t column = 0; column < board.columns(); ++column) {
            if (column > 0) {
                text += separator;
            }
            text += board.tile({row, column});
        }
    }
    return text;
}

// A board from its text; the case fails if the text is not a board.
[[nodiscard]] inline wjs::Board board_of(std::string_view text) {
    auto board = wjs::Board::parse(text);
    REQUIRE(board.has_value());
    return *std::move(board);
}

// A dictionary of these words; the case fails if the dictionary refuses one.
[[nodiscard]] inline wjs::Dictionary dictionary_of(std::initializer_list<std::string_view> words) {
    wjs::Dictionary dictionary;
    for (const std::string_view word : words) {
        REQUIRE(dictionary.insert(word));
    }
    return dictionary;
}

// The shipped English dictionary, read once from the checkout's data directory
// (WJS_DATA_DIRECTORY, defined for the unit tests by tests/CMakeLists.txt).
[[nodiscard]] inline const wjs::Dictionary& shipped_english() {
    static const wjs::Dictionary dictionary = [] {
        auto loaded = wjs::Dictionary::load_file(std::filesystem::path(WJS_DATA_DIRECTORY) /
                                                 "dictionaries" / "en.txt");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return dictionary;
}

// A directory of the case's own under the system's temporary directory, gone
// when the case ends. The name is made from random bits, not a counter or a
// clock, so two processes (two worktrees, two ctest jobs) never share one.
class TempDirectory {
public:
    TempDirectory() {
        std::random_device random;
        const std::filesystem::path base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const std::uint64_t token = (std::uint64_t{random()} << 32) | random();
            path_ = base / ("wjs_test_" + std::to_string(token));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error) && !error) {
                return;
            }
        }
        FAIL("no temporary directory could be made under " << base.string());
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    // A file in the directory, made (with its parent directories) and written.
    void write(const std::filesystem::path& name, std::string_view text) const {
        const std::filesystem::path file = path_ / name;
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
    }

private:
    std::filesystem::path path_;
};

// The whole of a file, or "" if it cannot be read.
[[nodiscard]] inline std::string slurp(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A file written whole, its directory made if need be.
inline void spit(const std::filesystem::path& file, std::string_view text) {
    std::error_code error;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

}  // namespace wjs::test
