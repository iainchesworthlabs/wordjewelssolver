// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "wjs/settings.hpp"

TEST_CASE("the defaults are the table's", "[settings]") {
    const wjs::Settings s;

    CHECK(s.help == wjs::HelpLevel::two_letters);
    CHECK(s.board == wjs::BoardMode::standard);
    CHECK(s.phase == "A");
    CHECK(s.focus_sounds.empty());
    CHECK(s.board_size == 3);
    CHECK(s.sound_it_out == wjs::SoundItOut::full);
    CHECK(s.celebration_sounds == wjs::CelebrationSounds::full);
    CHECK(s.sound);
    CHECK(s.vibration);
    CHECK(s.shake);
    CHECK(s.motion == wjs::MotionSetting::system);
    CHECK(s.colour_scheme == wjs::ColourScheme::system);
    CHECK(s.solo_clock == wjs::SoloClock::off);
    CHECK(s.solo_clock_minutes == wjs::default_solo_clock_minutes);
    CHECK(s.text_size_percent == 100);
    CHECK(s.school_font.empty());
    CHECK(s.language.empty());
}

TEST_CASE("the defaults are already normal", "[settings]") {
    const wjs::Settings s;
    CHECK(s.normalized() == s);
}

TEST_CASE("every value the screens offer survives normalisation", "[settings]") {
    wjs::Settings s;
    s.help = wjs::HelpLevel::start;
    s.board = wjs::BoardMode::phonics;
    s.phase = "B";
    s.focus_sounds = {"ai", "ee"};
    s.board_size = 4;
    s.sound_it_out = wjs::SoundItOut::word_only;
    s.celebration_sounds = wjs::CelebrationSounds::quiet;
    s.sound = false;
    s.vibration = false;
    s.shake = false;
    s.motion = wjs::MotionSetting::reduced;
    s.colour_scheme = wjs::ColourScheme::dark;
    s.solo_clock = wjs::SoloClock::count_down;
    s.solo_clock_minutes = 20;
    s.text_size_percent = 150;
    s.school_font = "Some-Font.1";
    s.language = "he";

    CHECK(s.normalized() == s);
}

TEST_CASE("a value outside the table becomes the default for it", "[settings]") {
    wjs::Settings s;
    s.board_size = 9;
    s.text_size_percent = 130;
    s.phase = "not valid!";
    s.school_font = "-";
    s.board = static_cast<wjs::BoardMode>(7);
    s.help = static_cast<wjs::HelpLevel>(9);
    s.sound_it_out = static_cast<wjs::SoundItOut>(-1);
    s.celebration_sounds = static_cast<wjs::CelebrationSounds>(99);
    s.motion = static_cast<wjs::MotionSetting>(5);
    s.colour_scheme = static_cast<wjs::ColourScheme>(5);
    s.solo_clock = static_cast<wjs::SoloClock>(9);
    s.solo_clock_minutes = 7;
    s.language = "tlh";

    const wjs::Settings n = s.normalized();

    CHECK(n.board_size == 3);
    CHECK(n.text_size_percent == 100);
    CHECK(n.phase == "A");
    CHECK(n.school_font.empty());
    CHECK(n.language.empty());
    CHECK(n.board == wjs::BoardMode::standard);
    CHECK(n.help == wjs::HelpLevel::two_letters);
    CHECK(n.sound_it_out == wjs::SoundItOut::full);
    CHECK(n.celebration_sounds == wjs::CelebrationSounds::full);
    CHECK(n.motion == wjs::MotionSetting::system);
    CHECK(n.colour_scheme == wjs::ColourScheme::system);
    CHECK(n.solo_clock == wjs::SoloClock::off);
    CHECK(n.solo_clock_minutes == wjs::default_solo_clock_minutes);
}

TEST_CASE("a board size is the mode's sizes and nothing else", "[settings]") {
    for (int size = -2; size <= 8; ++size) {
        // The standard board plays 3x3 to 6x6.
        wjs::Settings standard;
        standard.board_size = size;
        const bool standard_size = size >= 3 && size <= 6;
        CHECK(standard.normalized().board_size == (standard_size ? size : 3));

        // The phonics board plays 3x3 and 4x4.
        wjs::Settings phonics;
        phonics.board = wjs::BoardMode::phonics;
        phonics.board_size = size;
        const bool phonics_size = size == 3 || size == 4;
        CHECK(phonics.normalized().board_size == (phonics_size ? size : 3));
    }
}

TEST_CASE("a text size is 100, 125 or 150 and nothing else", "[settings]") {
    for (const int percent : {0, 99, 100, 101, 124, 125, 126, 149, 150, 151, 200}) {
        wjs::Settings s;
        s.text_size_percent = percent;
        const int got = s.normalized().text_size_percent;
        CHECK(((percent == 100 || percent == 125 || percent == 150) ? got == percent : got == 100));
    }
}

TEST_CASE("a phase is upper cased and must be a name", "[settings]") {
    wjs::Settings s;
    s.phase = "b";
    CHECK(s.normalized().phase == "B");
    s.phase = "";
    CHECK(s.normalized().phase == "A");
    s.phase = std::string(wjs::max_name_length + 1, 'a');
    CHECK(s.normalized().phase == "A");
    s.phase = std::string(wjs::max_name_length, 'a');
    CHECK(s.normalized().phase == std::string(wjs::max_name_length, 'A'));
    s.phase = "A B";
    CHECK(s.normalized().phase == "A");
}

TEST_CASE("a language is one of the eight codes, lower case, or the system's",
          "[settings][language]") {
    for (const std::string_view code : wjs::languages) {
        wjs::Settings s;
        s.language = std::string{code};
        CHECK(s.normalized().language == code);
        CHECK(wjs::is_valid_language(code));
    }
    // An upper-case code is the same language.
    wjs::Settings upper;
    upper.language = "HE";
    CHECK(upper.normalized().language == "he");
    // Anything else is the system's: empty.
    for (const std::string_view code : {"", "xx", "en_US", "e"}) {
        wjs::Settings s;
        s.language = std::string{code};
        CHECK(s.normalized().language.empty());
        CHECK_FALSE(wjs::is_valid_language(code));
    }
    // Empty is the one value that is not a language but is allowed.
    wjs::Settings system;
    CHECK(system.normalized().language.empty());
}

TEST_CASE("focus sounds are lower case, valid, sorted and each once", "[settings]") {
    wjs::Settings s;
    s.focus_sounds = {"EE", "ai", "ai", "a_e", "not a sound", "", "ay"};

    const auto n = s.normalized();

    CHECK(n.focus_sounds == std::vector<std::string>{"a_e", "ai", "ay", "ee"});
}

TEST_CASE("there are no more focus sounds than the bound", "[settings]") {
    wjs::Settings s;
    for (int i = 0; i < 200; ++i) {
        s.focus_sounds.push_back("s" + std::to_string(i));
    }

    CHECK(s.normalized().focus_sounds.size() == wjs::max_focus_sounds);
}

TEST_CASE("a school font is a plain name or none", "[settings]") {
    wjs::Settings s;
    s.school_font = "Edu.NSW-Foundation_2";
    CHECK(s.normalized().school_font == "Edu.NSW-Foundation_2");
    s.school_font = "has space";
    CHECK(s.normalized().school_font.empty());
    s.school_font = "../path";
    CHECK(s.normalized().school_font.empty());
    s.school_font = std::string(wjs::max_font_name_length + 1, 'a');
    CHECK(s.normalized().school_font.empty());
}

TEST_CASE("each enum is written by one name and read back by it", "[settings]") {
    for (const auto v : {wjs::BoardMode::standard, wjs::BoardMode::phonics}) {
        CHECK(wjs::board_mode_from_string(wjs::to_string(v)) == v);
    }
    for (const auto v : {wjs::SoundItOut::full, wjs::SoundItOut::word_only, wjs::SoundItOut::off}) {
        CHECK(wjs::sound_it_out_from_string(wjs::to_string(v)) == v);
    }
    for (const auto v : {wjs::CelebrationSounds::full, wjs::CelebrationSounds::quiet,
                         wjs::CelebrationSounds::off}) {
        CHECK(wjs::celebration_sounds_from_string(wjs::to_string(v)) == v);
    }
    for (const auto v : {wjs::MotionSetting::system, wjs::MotionSetting::reduced}) {
        CHECK(wjs::motion_from_string(wjs::to_string(v)) == v);
    }
    for (const auto v :
         {wjs::ColourScheme::system, wjs::ColourScheme::light, wjs::ColourScheme::dark}) {
        CHECK(wjs::colour_scheme_from_string(wjs::to_string(v)) == v);
    }
    for (const auto v :
         {wjs::SoloClock::off, wjs::SoloClock::count_up, wjs::SoloClock::count_down}) {
        CHECK(wjs::solo_clock_from_string(wjs::to_string(v)) == v);
    }
    for (const auto v : {wjs::HelpLevel::off, wjs::HelpLevel::start, wjs::HelpLevel::two_letters}) {
        CHECK(wjs::help_level_from_string(wjs::to_string(v)) == v);
    }
    CHECK_FALSE(wjs::board_mode_from_string("Standard").has_value());
    CHECK_FALSE(wjs::motion_from_string("").has_value());
    CHECK_FALSE(wjs::colour_scheme_from_string("Dark").has_value());
    CHECK_FALSE(wjs::solo_clock_from_string("countdown").has_value());
    CHECK_FALSE(wjs::help_level_from_string("everything").has_value());
}

TEST_CASE("a colour scheme is the system's or the person's choice and nothing else", "[settings]") {
    for (const auto v :
         {wjs::ColourScheme::system, wjs::ColourScheme::light, wjs::ColourScheme::dark}) {
        wjs::Settings s;
        s.colour_scheme = v;
        CHECK(s.normalized().colour_scheme == v);
    }
}

TEST_CASE("a board's clock is off, up or down, and its minutes are one of the choices",
          "[settings]") {
    for (const auto v :
         {wjs::SoloClock::off, wjs::SoloClock::count_up, wjs::SoloClock::count_down}) {
        wjs::Settings s;
        s.solo_clock = v;
        CHECK(s.normalized().solo_clock == v);
    }
    for (const int minutes : wjs::solo_clock_minutes_choices) {
        wjs::Settings s;
        s.solo_clock_minutes = minutes;
        CHECK(s.normalized().solo_clock_minutes == minutes);
    }
    for (const int minutes : {0, 4, 7, 60, 100}) {
        wjs::Settings s;
        s.solo_clock_minutes = minutes;
        CHECK(s.normalized().solo_clock_minutes == wjs::default_solo_clock_minutes);
    }
}

TEST_CASE("the help is off, the start tile, or the start and two letters", "[settings]") {
    for (const auto v : {wjs::HelpLevel::off, wjs::HelpLevel::start, wjs::HelpLevel::two_letters}) {
        wjs::Settings s;
        s.help = v;
        CHECK(s.normalized().help == v);
    }
}
