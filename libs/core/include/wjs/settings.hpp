// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What a parent sets (docs/design/dashboard.md, "Settings"). A plain value:
// the store keeps one, the screens edit a copy, and `normalized()` is the one
// place that says which values are allowed, so a hand-edited or damaged file
// and a screen with a bug end up with the same safe settings.
//
// The names of values are the ones in the table of the design. Phase names are
// generic ("A", "B"): what each contains is the reviewer's, and the library
// knows no phase (docs/design/phonics.md).

namespace wjs {

enum class BoardMode {
    standard,  // dice and the full dictionary (the default)
    phonics,   // a phase's words, grapheme blocks, sounding out
};

enum class SoundItOut {
    full,       // each sound, then the word
    word_only,  // the word
    off,
};

enum class CelebrationSounds {
    full,
    quiet,
    off,
};

enum class MotionSetting {
    system,   // follow the system's reduced-motion setting
    reduced,  // reduce, whatever the system says
};

// The light/dark chrome. System follows the operating system's setting; Light
// and Dark are the parent's choice for the whole program (docs/design/
// application.md, "Colour and contrast"). The board itself is dark in every
// scheme.
enum class ColourScheme {
    system,
    light,
    dark,
};

// The clock of a solo board: none, a count-up of how long the board has been
// open, or a count-down that ends the round when it reaches zero. The parent
// sets it; a board has no clock by default.
enum class SoloClock {
    off,
    count_up,
    count_down,
};

// The help a child can ask for in play (docs/design/game.md, "Help"): none,
// the start tile alone (a second press hides it), or the start tile and then
// its first two letters.
enum class HelpLevel {
    off,
    start,
    two_letters,
};

// Each enum is written in the store and in the screens by these names, and
// read back by from_string, which is empty for a name it does not know.
[[nodiscard]] std::string_view to_string(BoardMode value) noexcept;
[[nodiscard]] std::string_view to_string(SoundItOut value) noexcept;
[[nodiscard]] std::string_view to_string(CelebrationSounds value) noexcept;
[[nodiscard]] std::string_view to_string(MotionSetting value) noexcept;
[[nodiscard]] std::string_view to_string(ColourScheme value) noexcept;
[[nodiscard]] std::string_view to_string(SoloClock value) noexcept;
[[nodiscard]] std::string_view to_string(HelpLevel value) noexcept;

[[nodiscard]] std::optional<BoardMode> board_mode_from_string(std::string_view name) noexcept;
[[nodiscard]] std::optional<SoundItOut> sound_it_out_from_string(std::string_view name) noexcept;
[[nodiscard]] std::optional<CelebrationSounds> celebration_sounds_from_string(
    std::string_view name) noexcept;
[[nodiscard]] std::optional<MotionSetting> motion_from_string(std::string_view name) noexcept;
[[nodiscard]] std::optional<ColourScheme> colour_scheme_from_string(std::string_view name) noexcept;
[[nodiscard]] std::optional<SoloClock> solo_clock_from_string(std::string_view name) noexcept;
[[nodiscard]] std::optional<HelpLevel> help_level_from_string(std::string_view name) noexcept;

// The languages the interface is offered in, as the codes the store and the
// screens use: English, French, Spanish, German, Yiddish, Hebrew, Simplified
// Chinese and Japanese (docs/design/application.md, "Languages"). A code is
// the name of its catalogue (wjs_zh_CN.qm). The board's letters, the word list
// and the phonics data are not translated; the chrome is.
inline constexpr std::array<std::string_view, 8> languages = {"en", "fr", "es",    "de",
                                                              "yi", "he", "zh_CN", "ja"};

// Whether `code` is one of the languages above. Empty is not: it is the system's
// language, which only the screens know.
[[nodiscard]] bool is_valid_language(std::string_view code) noexcept;

// The code in `languages` that `code` names when upper and lower case are not
// told apart ("zh_cn" is "zh_CN"), or empty when it names none.
[[nodiscard]] std::string_view canonical_language(std::string_view code) noexcept;

// The board sizes the parent area offers as a default. The standard board plays
// 3x3 to 6x6 and the child picks on the box; the parent's default is one of the
// two sizes the phonics board is designed for (docs/design/game.md).
inline constexpr int smallest_default_board_size = 3;
inline constexpr int largest_default_board_size = 4;
// The largest size the standard board is played at (its dice, data/dice).
inline constexpr int largest_standard_board_size = 6;

// The text sizes on offer, as a percentage of the system's: 1, 1.25, 1.5.
inline constexpr std::array<int, 3> text_size_percents = {100, 125, 150};

// How long a count-down board has, in whole minutes, and which of them the
// parent can choose.
inline constexpr int default_solo_clock_minutes = 5;
inline constexpr std::array<int, 8> solo_clock_minutes_choices = {1, 2, 3, 5, 10, 15, 20, 30};

// The longest a phase, a sound or a font name may be, and the most focus
// sounds there can be. A bound, so that a damaged file cannot make a large
// setting.
inline constexpr std::size_t max_name_length = 16;
inline constexpr std::size_t max_font_name_length = 48;
inline constexpr std::size_t max_focus_sounds = 40;

struct Settings {
    // Help: none, the start tile alone, or the start tile and then its first
    // two letters. The design's two-rung help is the default.
    HelpLevel help = HelpLevel::two_letters;

    BoardMode board = BoardMode::standard;

    // The phonics board's phase, upper case ("A"). Used only by that board.
    std::string phase = "A";

    // The sounds the school is on this week (lower case, as written in the
    // graphemes file), sorted and each once. Only sounds of the phase can be
    // ticked: that is the dashboard's to hold to (restrict_focus_sounds).
    std::vector<std::string> focus_sounds;

    // The board size the box offers first: 3x3 on a new device, then the last
    // used. 3x3 to 6x6 on the standard board, 3x3 and 4x4 on the phonics board
    // (normalized() holds it to the board's).
    int board_size = 3;

    SoundItOut sound_it_out = SoundItOut::full;
    CelebrationSounds celebration_sounds = CelebrationSounds::full;
    bool sound = true;
    bool vibration = true;
    bool shake = true;  // shake the device to roll
    MotionSetting motion = MotionSetting::system;

    // The clock of a solo board: off, a count-up, or a count-down of
    // solo_clock_minutes that ends the round at zero.
    SoloClock solo_clock = SoloClock::off;
    int solo_clock_minutes = default_solo_clock_minutes;

    // The light/dark chrome: the system's setting, or the parent's choice.
    ColourScheme colour_scheme = ColourScheme::system;

    // 100, 125 or 150: a percentage of the system's text size.
    int text_size_percent = 100;

    // A school font's name, or empty for none. Only a font whose licence
    // permits bundling can ever be named here, and none is bundled yet.
    std::string school_font;

    // The language of the interface, one of `languages`, or empty for the
    // device's own when it is one of them and English otherwise. The board and
    // the words on it are never this: the game is an English word game.
    std::string language;

    // The same settings with every value brought to one the table allows:
    // a size, a text scale or a mode that is not on offer becomes the default
    // for it, names are cut to their bounds and made of the characters a name
    // may have, and the focus sounds are sorted and unique.
    [[nodiscard]] Settings normalized() const;

    friend bool operator==(const Settings&, const Settings&) = default;
};

// Whether `name` can be a phase or a sound: one to max_name_length letters,
// digits or underscores (an underscore is a split digraph, "a_e").
[[nodiscard]] bool is_valid_name(std::string_view name) noexcept;

// Whether `name` can be a font's name. Not "-", which the store uses for none.
[[nodiscard]] bool is_valid_font_name(std::string_view name) noexcept;

}  // namespace wjs
