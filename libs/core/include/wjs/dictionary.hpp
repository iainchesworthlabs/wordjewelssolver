// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <istream>
#include <string_view>
#include <vector>

namespace wjs {

enum class DictionaryError {
    cannot_open,  // the file could not be opened
    read_failed,  // the stream failed part-way through
};

[[nodiscard]] std::string_view to_string(DictionaryError error) noexcept;

// A set of words held as a trie, so that a search can ask "is this a word?"
// and "could this still become one?" one letter at a time. Words are ASCII
// letters, stored upper-case.
class Dictionary {
public:
    // A position in the trie. Start at root and step() one letter at a time;
    // no_node means no word has the letters stepped so far as a prefix.
    using Cursor = std::uint32_t;
    static constexpr Cursor root = 0;
    static constexpr Cursor no_node = 0xFFFFFFFFU;

    Dictionary();

    // Adds a word. Returns false, and leaves the dictionary unchanged, for an
    // empty word or one with anything but ASCII letters in it.
    [[nodiscard]] bool insert(std::string_view word);

    // One word per line. Blank lines are skipped, and so is a line that is not
    // a word (a proper noun with an apostrophe, a hyphenated compound).
    [[nodiscard]] static std::expected<Dictionary, DictionaryError> load(std::istream& words);
    [[nodiscard]] static std::expected<Dictionary, DictionaryError> load_file(
        const std::filesystem::path& path);

    // Distinct words held.
    [[nodiscard]] std::size_t size() const noexcept { return word_count_; }

    // Case-insensitive. The empty string is a prefix of every dictionary.
    [[nodiscard]] bool contains(std::string_view word) const;
    [[nodiscard]] bool has_prefix(std::string_view prefix) const;

    // The node reached from `from` by `letter` (upper-case), or no_node. A step
    // from no_node (or from any cursor this dictionary did not give) is no_node.
    [[nodiscard]] Cursor step(Cursor from, char letter) const noexcept;

    // Whether the letters stepped to reach `at` spell a word.
    [[nodiscard]] bool is_word(Cursor at) const noexcept;

private:
    struct Edge {
        char letter;
        Cursor target;
    };

    struct Node {
        std::vector<Edge> edges;  // sorted by letter
        bool terminal = false;
    };

    std::vector<Node> nodes_;
    std::size_t word_count_ = 0;
};

}  // namespace wjs
