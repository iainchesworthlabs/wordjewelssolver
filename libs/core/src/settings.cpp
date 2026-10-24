// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/settings.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include "ascii.hpp"
#include "wjs/profiling.hpp"

namespace wjs {

namespace {

using ascii::to_lower;
using ascii::to_upper;

// The overload set the callers below use with std::transform.
[[nodiscard]] char lower(char c) noexcept {
    return to_lower(c);
}

[[nodiscard]] char upper(char c) noexcept {
    return to_upper(c);
}

[[nodiscard]] bool is_name_character(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] bool is_font_character(char c) noexcept {
    return is_name_character(c) || c == '-' || c == '.';
}

template <typename Enum>
[[nodiscard]] Enum or_default(Enum value, Enum fallback, Enum last) noexcept {
    return static_cast<int>(value) >= 0 && static_cast<int>(value) <= static_cast<int>(last)
               ? value
               : fallback;
}

}  // namespace

std::string_view to_string(BoardMode value) noexcept {
    switch (value) {
        case BoardMode::standard:
            return "standard";
        case BoardMode::phonics:
            return "phonics";
    }
    return "standard";
}

std::string_view to_string(SoundItOut value) noexcept {
    switch (value) {
        case SoundItOut::full:
            return "full";
        case SoundItOut::word_only:
            return "word";
        case SoundItOut::off:
            return "off";
    }
    return "full";
}

std::string_view to_string(CelebrationSounds value) noexcept {
    switch (value) {
        case CelebrationSounds::full:
            return "full";
        case CelebrationSounds::quiet:
            return "quiet";
        case CelebrationSounds::off:
            return "off";
    }
    return "full";
}

std::string_view to_string(MotionSetting value) noexcept {
    switch (value) {
        case MotionSetting::system:
            return "system";
        case MotionSetting::reduced:
            return "reduced";
    }
    return "system";
}

std::string_view to_string(ColourScheme value) noexcept {
    switch (value) {
        case ColourScheme::system:
            return "system";
        case ColourScheme::light:
            return "light";
        case ColourScheme::dark:
            return "dark";
    }
    return "system";
}

std::string_view to_string(SoloClock value) noexcept {
    switch (value) {
        case SoloClock::off:
            return "off";
        case SoloClock::count_up:
            return "up";
        case SoloClock::count_down:
            return "down";
    }
    return "off";
}

std::string_view to_string(HelpLevel value) noexcept {
    switch (value) {
        case HelpLevel::off:
            return "off";
        case HelpLevel::start:
            return "start";
        case HelpLevel::two_letters:
            return "two";
    }
    return "two";
}

std::optional<BoardMode> board_mode_from_string(std::string_view name) noexcept {
    if (name == "standard") {
        return BoardMode::standard;
    }
    if (name == "phonics") {
        return BoardMode::phonics;
    }
    return std::nullopt;
}

std::optional<SoundItOut> sound_it_out_from_string(std::string_view name) noexcept {
    if (name == "full") {
        return SoundItOut::full;
    }
    if (name == "word") {
        return SoundItOut::word_only;
    }
    if (name == "off") {
        return SoundItOut::off;
    }
    return std::nullopt;
}

std::optional<CelebrationSounds> celebration_sounds_from_string(std::string_view name) noexcept {
    if (name == "full") {
        return CelebrationSounds::full;
    }
    if (name == "quiet") {
        return CelebrationSounds::quiet;
    }
    if (name == "off") {
        return CelebrationSounds::off;
    }
    return std::nullopt;
}

std::optional<MotionSetting> motion_from_string(std::string_view name) noexcept {
    if (name == "system") {
        return MotionSetting::system;
    }
    if (name == "reduced") {
        return MotionSetting::reduced;
    }
    return std::nullopt;
}

std::optional<ColourScheme> colour_scheme_from_string(std::string_view name) noexcept {
    if (name == "system") {
        return ColourScheme::system;
    }
    if (name == "light") {
        return ColourScheme::light;
    }
    if (name == "dark") {
        return ColourScheme::dark;
    }
    return std::nullopt;
}

std::optional<SoloClock> solo_clock_from_string(std::string_view name) noexcept {
    if (name == "off") {
        return SoloClock::off;
    }
    if (name == "up") {
        return SoloClock::count_up;
    }
    if (name == "down") {
        return SoloClock::count_down;
    }
    return std::nullopt;
}

std::optional<HelpLevel> help_level_from_string(std::string_view name) noexcept {
    if (name == "off") {
        return HelpLevel::off;
    }
    if (name == "start") {
        return HelpLevel::start;
    }
    if (name == "two") {
        return HelpLevel::two_letters;
    }
    return std::nullopt;
}

bool is_valid_language(std::string_view code) noexcept {
    return std::find(std::begin(languages), std::end(languages), code) != std::end(languages);
}

std::string_view canonical_language(std::string_view code) noexcept {
    const auto same = [code](std::string_view known) {
        return known.size() == code.size() &&
               std::equal(known.begin(), known.end(), code.begin(),
                          [](char a, char b) { return lower(a) == lower(b); });
    };
    const auto found = std::find_if(std::begin(languages), std::end(languages), same);
    return found == std::end(languages) ? std::string_view{} : *found;
}

bool is_valid_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= max_name_length &&
           std::all_of(name.begin(), name.end(), is_name_character);
}

bool is_valid_font_name(std::string_view name) noexcept {
    return !name.empty() && name != "-" && name.size() <= max_font_name_length &&
           std::all_of(name.begin(), name.end(), is_font_character);
}

Settings Settings::normalized() const {
    auto zone = profiling::zone<"settings.normalize">();

    Settings result = *this;

    result.board = or_default(result.board, BoardMode::standard, BoardMode::phonics);
    result.help = or_default(result.help, HelpLevel::two_letters, HelpLevel::two_letters);
    result.sound_it_out = or_default(result.sound_it_out, SoundItOut::full, SoundItOut::off);
    result.celebration_sounds =
        or_default(result.celebration_sounds, CelebrationSounds::full, CelebrationSounds::off);
    result.motion = or_default(result.motion, MotionSetting::system, MotionSetting::reduced);
    result.colour_scheme =
        or_default(result.colour_scheme, ColourScheme::system, ColourScheme::dark);
    result.solo_clock = or_default(result.solo_clock, SoloClock::off, SoloClock::count_down);
    if (std::find(std::begin(solo_clock_minutes_choices), std::end(solo_clock_minutes_choices),
                  result.solo_clock_minutes) == std::end(solo_clock_minutes_choices)) {
        result.solo_clock_minutes = default_solo_clock_minutes;
    }

    std::transform(result.phase.begin(), result.phase.end(), result.phase.begin(), upper);
    if (!is_valid_name(result.phase)) {
        result.phase = "A";
    }

    // The board size the box offers first is one the mode's board is played at:
    // 3x3 to 6x6 on the standard board, the two sizes the phonics board is
    // designed for on the other.
    const int largest = result.board == BoardMode::standard ? largest_standard_board_size
                                                            : largest_default_board_size;
    if (result.board_size < smallest_default_board_size || result.board_size > largest) {
        result.board_size = smallest_default_board_size;
    }

    if (std::find(std::begin(text_size_percents), std::end(text_size_percents),
                  result.text_size_percent) == std::end(text_size_percents)) {
        result.text_size_percent = 100;
    }

    if (!is_valid_font_name(result.school_font)) {
        result.school_font.clear();
    }

    // The language is one of the eight codes, or empty for the system's. The
    // device's own language is chosen by the manager, not here.
    result.language = std::string{canonical_language(result.language)};

    for (std::string& tick : result.focus_sounds) {
        std::transform(tick.begin(), tick.end(), tick.begin(), lower);
    }
    std::erase_if(result.focus_sounds,
                  [](const std::string& tick) { return !is_valid_name(tick); });
    std::sort(result.focus_sounds.begin(), result.focus_sounds.end());
    result.focus_sounds.erase(std::unique(result.focus_sounds.begin(), result.focus_sounds.end()),
                              result.focus_sounds.end());
    if (result.focus_sounds.size() > max_focus_sounds) {
        result.focus_sounds.resize(max_focus_sounds);
    }

    return result;
}

}  // namespace wjs
