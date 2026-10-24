// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/test_support.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/settings.hpp"
#include "wjs/sound_it_out.hpp"

// TEST DATA. One phase written for these tests, with placeholder sounds and
// clips: not a phase of the application and not content for a child. It has
// single letters, a vowel team, a split digraph, a letter with two sounds and a
// tricky word, which are the shapes the sequence has to tell apart.

namespace {

using namespace std::chrono_literals;
using wjs::SoundItOut;
using wjs::SoundStep;
using wjs::SoundStepKind;

constexpr std::string_view graphemes =
    "# phase: A\n"
    "a    /t1/  clip:c1\n"
    "a_e  /t2/  clip:c2\n"
    "ai   /t3/  clip:c3\n"
    "c    /t4/  clip:c4\n"
    "k    /t5/  clip:c5\n"
    "n    /t6/  clip:c6\n"
    "o    /tb/  clip:cb\n"
    "o    /tc/  clip:cc\n"
    "p    /t7/  clip:c7\n"
    "r    /t8/  clip:c8\n"
    "s    /t9/  clip:c9\n"
    "t    /ta/  clip:ca\n";

constexpr std::string_view words =
    "# phase: A\n"
    "cake  c a_ k _e\n"
    "pot   p o:2 t\n"
    "rain  r ai n\n"
    "sat   s a t\n"
    "the   tricky\n";

const wjs::PhaseIndex& phase() {
    static const wjs::PhaseSet set = [] {
        std::istringstream graphemes_in{std::string(graphemes)};
        std::istringstream words_in{std::string(words)};
        auto parsed_graphemes = wjs::parse_graphemes(graphemes_in, "a.graphemes");
        auto parsed_words = wjs::parse_words(words_in, "a.words");
        REQUIRE(parsed_graphemes.has_value());
        REQUIRE(parsed_words.has_value());
        const wjs::Dictionary english =
            wjs::test::dictionary_of({"cake", "pot", "rain", "sat", "the"});
        auto built = wjs::PhaseSet::build({*parsed_graphemes}, {*parsed_words}, english);
        REQUIRE(built.has_value());
        return *std::move(built);
    }();
    return *set.find("A");
}

wjs::SoundSequence sequence_of(std::string_view word, SoundItOut setting = SoundItOut::full) {
    auto sequence = wjs::sound_it_out_sequence(phase(), word, setting);
    REQUIRE(sequence.has_value());
    return *std::move(sequence);
}

std::vector<SoundStepKind> kinds_of(const wjs::SoundSequence& sequence) {
    std::vector<SoundStepKind> kinds;
    for (const SoundStep& step : sequence.steps) {
        kinds.push_back(step.kind);
    }
    return kinds;
}

}  // namespace

TEST_CASE("the timings are the design's: 150 ms between sounds and a blend of 380",
          "[sound_it_out]") {
    CHECK(wjs::segment_gap == 150ms);
    CHECK(wjs::blend_duration == 380ms);
}

TEST_CASE("sounding out is each sound, the blend, the word and the cheer, in that order",
          "[sound_it_out]") {
    const auto sequence = sequence_of("sat");

    CHECK(sequence.word == "sat");
    CHECK_FALSE(sequence.whole_word_only);
    CHECK(kinds_of(sequence) ==
          std::vector<SoundStepKind>{SoundStepKind::sound, SoundStepKind::sound,
                                     SoundStepKind::sound, SoundStepKind::blend, SoundStepKind::say,
                                     SoundStepKind::cheer});
}

TEST_CASE("there is one sound step for each grapheme, in the word's order", "[sound_it_out]") {
    const auto sequence = sequence_of("rain");  // r, ai, n: three blocks, four letters

    REQUIRE(sequence.steps.size() == 6);
    const std::vector<std::string> letters = {"r", "ai", "n"};
    const std::vector<std::string> sounds = {"/t8/", "/t3/", "/t6/"};
    const std::vector<std::string> clips = {"c8", "c3", "c6"};
    for (std::size_t block = 0; block < 3; ++block) {
        const SoundStep& step = sequence.steps[block];
        INFO("block " << block);
        CHECK(step.kind == SoundStepKind::sound);
        CHECK(step.block == block);
        CHECK(step.letters == letters[block]);
        CHECK(step.sound == sounds[block]);
        CHECK(step.clips == std::vector<std::string>{clips[block]});
    }
}

TEST_CASE("the sounds are 150 ms apart, the first begins at once", "[sound_it_out]") {
    const auto sequence = sequence_of("rain");

    CHECK(sequence.steps[0].gap_before == 0ms);
    CHECK(sequence.steps[1].gap_before == 150ms);
    CHECK(sequence.steps[2].gap_before == 150ms);
}

TEST_CASE("the blend takes 380 ms and plays every sound again in order", "[sound_it_out]") {
    const auto sequence = sequence_of("rain");

    const SoundStep& blend = sequence.steps[3];
    REQUIRE(blend.kind == SoundStepKind::blend);
    CHECK(blend.duration == 380ms);
    CHECK(blend.gap_before == 150ms);
    CHECK(blend.letters == "rain");
    CHECK(blend.clips == std::vector<std::string>{"c8", "c3", "c6"});
}

TEST_CASE("the word is said whole after the blend", "[sound_it_out]") {
    const auto sequence = sequence_of("RAIN");  // any case in, lower case out

    const SoundStep& say = sequence.steps[4];
    REQUIRE(say.kind == SoundStepKind::say);
    CHECK(say.letters == "rain");
    CHECK(say.clips.empty());
    CHECK(say.gap_before == 0ms);
    CHECK(sequence.word == "rain");
}

TEST_CASE("the cheer names the sound of a grapheme of more than one letter", "[sound_it_out]") {
    const auto sequence = sequence_of("rain");

    const SoundStep& cheer = sequence.steps.back();
    REQUIRE(cheer.kind == SoundStepKind::cheer);
    CHECK(cheer.letters == "ai");
    CHECK(cheer.sound == "/t3/");
}

TEST_CASE("a word of single letters has a cheer that names no sound", "[sound_it_out]") {
    const auto sequence = sequence_of("sat");

    const SoundStep& cheer = sequence.steps.back();
    REQUIRE(cheer.kind == SoundStepKind::cheer);
    CHECK(cheer.letters.empty());
    CHECK(cheer.sound.empty());
}

TEST_CASE("a split digraph is one block, in the place of its first letter", "[sound_it_out]") {
    const auto sequence = sequence_of("cake");  // c, a_e, k

    REQUIRE(sequence.steps.size() == 6);
    CHECK(sequence.steps[0].letters == "c");
    CHECK(sequence.steps[1].letters == "a_e");
    CHECK(sequence.steps[1].sound == "/t2/");
    CHECK(sequence.steps[2].letters == "k");
    CHECK(sequence.steps.back().letters == "a_e");  // the cheer names it
}

TEST_CASE("a letter with two sounds is sounded as the word uses it", "[sound_it_out]") {
    const auto sequence = sequence_of("pot");  // o:2

    REQUIRE(sequence.steps.size() == 6);
    CHECK(sequence.steps[1].letters == "o");
    CHECK(sequence.steps[1].sound == "/tc/");
    CHECK(sequence.steps[1].clips == std::vector<std::string>{"cc"});
}

TEST_CASE("a tricky word is said whole and never sounded out", "[sound_it_out]") {
    const auto sequence = sequence_of("the");

    CHECK(sequence.whole_word_only);
    REQUIRE(sequence.steps.size() == 1);
    CHECK(sequence.steps[0].kind == SoundStepKind::say);
    CHECK(sequence.steps[0].letters == "the");
}

TEST_CASE("the word only is the word and nothing else", "[sound_it_out]") {
    const auto sequence = sequence_of("rain", SoundItOut::word_only);

    CHECK(sequence.whole_word_only);
    REQUIRE(sequence.steps.size() == 1);
    CHECK(sequence.steps[0].kind == SoundStepKind::say);
    CHECK(sequence.steps[0].letters == "rain");
}

TEST_CASE("off is a sequence of no steps, for a tricky word too", "[sound_it_out]") {
    for (const char* word : {"rain", "the", "sat"}) {
        const auto sequence = sequence_of(word, SoundItOut::off);
        INFO(word);
        CHECK(sequence.steps.empty());
        CHECK_FALSE(sequence.whole_word_only);
    }
}

TEST_CASE("a word the phase does not hold is told, not sounded", "[sound_it_out]") {
    const auto sequence = wjs::sound_it_out_sequence(phase(), "dog", SoundItOut::full);

    REQUIRE_FALSE(sequence.has_value());
    CHECK(sequence.error() == wjs::SoundSequenceError::not_in_phase);
    CHECK_FALSE(wjs::to_string(sequence.error()).empty());
}

TEST_CASE("the time the sequence fixes is its gaps and its blend", "[sound_it_out]") {
    // Three sounds: two gaps between them, one before the blend, and the blend.
    CHECK(wjs::fixed_time(sequence_of("rain")) == 3 * 150ms + 380ms);
    CHECK(wjs::fixed_time(sequence_of("the")) == 0ms);
    CHECK(wjs::fixed_time(sequence_of("rain", SoundItOut::off)) == 0ms);
}

TEST_CASE("the same word under the same setting is the same sequence", "[sound_it_out]") {
    CHECK(sequence_of("rain") == sequence_of("rain"));
    CHECK_FALSE(sequence_of("rain") == sequence_of("rain", SoundItOut::word_only));
}

TEST_CASE("every step kind has a name", "[sound_it_out]") {
    for (const SoundStepKind kind :
         {SoundStepKind::sound, SoundStepKind::blend, SoundStepKind::say, SoundStepKind::cheer}) {
        CHECK_FALSE(wjs::to_string(kind).empty());
    }
}
