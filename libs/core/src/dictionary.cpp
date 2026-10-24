// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/dictionary.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>

#include "ascii.hpp"
#include "wjs/profiling.hpp"

namespace wjs {
namespace {

using ascii::to_upper;

// The word upper-cased, or nothing if it is empty or holds a non-letter.
[[nodiscard]] std::optional<std::string> normalise(std::string_view word) {
    if (word.empty()) {
        return std::nullopt;
    }
    std::string upper;
    upper.reserve(word.size());
    for (const char c : word) {
        const char u = to_upper(c);
        if (u < 'A' || u > 'Z') {
            return std::nullopt;
        }
        upper.push_back(u);
    }
    return upper;
}

[[nodiscard]] std::string_view trim(std::string_view line) noexcept {
    return ascii::trim(line, " \t\r\n");
}

}  // namespace

std::string_view to_string(DictionaryError error) noexcept {
    switch (error) {
        case DictionaryError::cannot_open:
            return "the dictionary file could not be opened";
        case DictionaryError::read_failed:
            return "reading the dictionary failed";
    }
    return "unknown dictionary error";
}

Dictionary::Dictionary() : nodes_(1) {}

bool Dictionary::insert(std::string_view word) {
    if (word.empty()) {
        return false;
    }
    // Every letter is checked before anything is changed: a word refused
    // part-way leaves the dictionary exactly as it was, as normalise() did.
    for (const char c : word) {
        const char u = to_upper(c);
        if (u < 'A' || u > 'Z') {
            return false;
        }
    }

    // The walk needs no copy of the word: normalise() built one string per
    // insert, and loading a word list is 172,823 inserts, so each letter is
    // upper-cased as it is looked up and nothing is allocated per word.
    Cursor at = root;
    for (const char c : word) {
        const char letter = to_upper(c);
        const auto& edges = nodes_[at].edges;
        const auto found = std::ranges::lower_bound(edges, letter, {}, &Edge::letter);
        if (found != edges.end() && found->letter == letter) {
            at = found->target;
            continue;
        }

        // The offset into the node's edges is an index, so it survives
        // nodes_.emplace_back (which may reallocate nodes_ but moves each node's
        // edge vector in place): the edge is found once, not again after the
        // append, one binary search less for every new node of the trie.
        const auto offset = found - edges.begin();
        const auto target = static_cast<Cursor>(nodes_.size());
        nodes_.emplace_back();
        nodes_[at].edges.insert(nodes_[at].edges.begin() + offset, Edge{letter, target});
        at = target;
    }

    if (!nodes_[at].terminal) {
        nodes_[at].terminal = true;
        ++word_count_;
    }
    return true;
}

std::expected<Dictionary, DictionaryError> Dictionary::load(std::istream& words) {
    auto zone = profiling::zone<"data.load_dictionary">();

    // A file saved by some editors starts with a UTF-8 byte-order mark; left in
    // place it would make the first word not a word.
    constexpr std::string_view byte_order_mark = "\xEF\xBB\xBF";

    Dictionary dictionary;
    std::string line;
    bool first_line = true;
    while (std::getline(words, line)) {
        std::string_view text = line;
        if (first_line && text.starts_with(byte_order_mark)) {
            text.remove_prefix(byte_order_mark.size());
        }
        first_line = false;
        // A line that is not a word is refused, and skipped, as the header says.
        static_cast<void>(dictionary.insert(trim(text)));
    }
    if (words.bad()) {
        return std::unexpected(DictionaryError::read_failed);
    }
    zone.Value(dictionary.size());
    return dictionary;
}

std::expected<Dictionary, DictionaryError> Dictionary::load_file(
    const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::unexpected(DictionaryError::cannot_open);
    }
    return load(file);
}

bool Dictionary::contains(std::string_view word) const {
    const auto upper = normalise(word);
    if (!upper) {
        return false;
    }
    Cursor at = root;
    for (const char letter : *upper) {
        at = step(at, letter);
        if (at == no_node) {
            return false;
        }
    }
    return is_word(at);
}

bool Dictionary::has_prefix(std::string_view prefix) const {
    if (prefix.empty()) {
        return true;
    }
    const auto upper = normalise(prefix);
    if (!upper) {
        return false;
    }
    Cursor at = root;
    for (const char letter : *upper) {
        at = step(at, letter);
        if (at == no_node) {
            return false;
        }
    }
    return true;
}

Dictionary::Cursor Dictionary::step(Cursor from, char letter) const noexcept {
    if (from >= nodes_.size()) {
        return no_node;  // no_node stepped from stays nowhere, as is_word(no_node) is false
    }
    const auto& edges = nodes_[from].edges;
    const auto found = std::ranges::lower_bound(edges, letter, {}, &Edge::letter);
    return (found != edges.end() && found->letter == letter) ? found->target : no_node;
}

bool Dictionary::is_word(Cursor at) const noexcept {
    return at != no_node && nodes_[at].terminal;
}

}  // namespace wjs
