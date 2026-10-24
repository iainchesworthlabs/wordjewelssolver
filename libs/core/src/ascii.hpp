// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <string>
#include <string_view>

// ASCII-only case and trimming helpers, shared by the library's sources.
//
// They are deliberately not the <cctype> or <locale> functions: those depend on
// the current locale, and a word or a grapheme is the same on every machine.
// Only A-Z and a-z change case; every other byte, including the bytes of a
// multi-byte UTF-8 sequence, passes through untouched.

namespace wjs::ascii {

[[nodiscard]] constexpr char to_upper(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] constexpr char to_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] inline std::string upper(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) { return to_upper(c); });
    return result;
}

[[nodiscard]] inline std::string lower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) { return to_lower(c); });
    return result;
}

// `text` without any leading or trailing character that is in `blanks`. A view
// of the same storage: it lives as long as `text`'s buffer does.
[[nodiscard]] constexpr std::string_view trim(std::string_view text,
                                              std::string_view blanks) noexcept {
    const auto first = text.find_first_not_of(blanks);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(blanks);
    return text.substr(first, last - first + 1);
}

}  // namespace wjs::ascii
