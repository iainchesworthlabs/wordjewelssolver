// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "wjs/feedback.hpp"
#include "wjs/settings.hpp"

// The cue policy of docs/design/application.md, "Audio and touch feedback": a
// cue, the parent's settings and the cue's detail in, and what is played, said
// and tapped out. No audio device, no sensor and no Qt: this is the mapping the
// application's engines carry out.

namespace {

using wjs::CelebrationSounds;
using wjs::CuePlan;
using wjs::FeedbackCue;
using wjs::FeedbackSettings;
using wjs::HapticCue;
using wjs::SoundItOut;

[[nodiscard]] CuePlan plan(FeedbackCue cue, std::string_view detail = {},
                           FeedbackSettings settings = {}) {
    return wjs::plan_cue(cue, detail, settings);
}

}  // namespace

TEST_CASE("every cue kind has its name, and a name it does not know is nothing", "[feedback]") {
    const FeedbackCue cues[] = {FeedbackCue::speak, FeedbackCue::tap,         FeedbackCue::rattle,
                                FeedbackCue::tile,  FeedbackCue::tile_landed, FeedbackCue::word,
                                FeedbackCue::tick,  FeedbackCue::chime,       FeedbackCue::cleared};
    for (const FeedbackCue cue : cues) {
        const auto parsed = wjs::feedback_cue_from_string(wjs::to_string(cue));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == cue);
    }
    CHECK_FALSE(wjs::feedback_cue_from_string("").has_value());
    CHECK_FALSE(wjs::feedback_cue_from_string("tile-landed ").has_value());
    CHECK(wjs::feedback_cue_from_string("tile-landed") == FeedbackCue::tile_landed);
    CHECK(wjs::feedback_cue_from_string("rattle") == FeedbackCue::rattle);
}

TEST_CASE("a cue with a sound names its asset and the master switch silences it", "[feedback]") {
    CHECK(plan(FeedbackCue::rattle).sound);
    CHECK(plan(FeedbackCue::rattle).asset == "rattle");
    CHECK(plan(FeedbackCue::tile).asset == "letter");
    CHECK(plan(FeedbackCue::tile_landed).asset == "land");
    CHECK(plan(FeedbackCue::tick).asset == "tick");
    CHECK(plan(FeedbackCue::chime).asset == "chime");
    CHECK(plan(FeedbackCue::cleared).asset == "cleared");
    CHECK(plan(FeedbackCue::tap).asset == "tap");
    CHECK(plan(FeedbackCue::word, "sat").asset == "cheer");

    FeedbackSettings silent;
    silent.sound = false;
    for (const FeedbackCue cue :
         {FeedbackCue::tap, FeedbackCue::rattle, FeedbackCue::tile, FeedbackCue::tile_landed,
          FeedbackCue::tick, FeedbackCue::chime, FeedbackCue::cleared}) {
        CHECK_FALSE(plan(cue, {}, silent).sound);
        CHECK(plan(cue, {}, silent).asset.empty());
    }
    // A find is silent too, and so is its spoken word.
    const CuePlan found = plan(FeedbackCue::word, "sat", silent);
    CHECK_FALSE(found.sound);
    CHECK_FALSE(found.speak_word);
    CHECK_FALSE(found.speak_sequence);
    CHECK_FALSE(plan(FeedbackCue::speak, "box", silent).speak_text);
}

TEST_CASE("a celebration has its own switch, and quiet lowers it", "[feedback]") {
    FeedbackSettings quiet;
    quiet.celebration_sounds = CelebrationSounds::quiet;
    const CuePlan cheer = plan(FeedbackCue::word, "sat", quiet);
    CHECK(cheer.sound);
    CHECK(cheer.volume == wjs::quiet_volume);
    CHECK(cheer.speak_word);
    CHECK(plan(FeedbackCue::chime, {}, quiet).volume == wjs::quiet_volume);
    CHECK(plan(FeedbackCue::cleared, {}, quiet).volume == wjs::quiet_volume);

    FeedbackSettings off;
    off.celebration_sounds = CelebrationSounds::off;
    // The cheer, the chime and the cleared notes stop; the word is still said
    // because Sound it out governs that, not Celebration sounds.
    CHECK_FALSE(plan(FeedbackCue::word, "sat", off).sound);
    CHECK(plan(FeedbackCue::word, "sat", off).speak_word);
    CHECK_FALSE(plan(FeedbackCue::chime, {}, off).sound);
    CHECK_FALSE(plan(FeedbackCue::cleared, {}, off).sound);
    // A rattle is not a celebration: it is not a parent's to turn off here.
    CHECK(plan(FeedbackCue::rattle, {}, off).sound);
}

TEST_CASE("Sound it out alone decides whether a found word is said", "[feedback]") {
    FeedbackSettings full;
    full.sound_it_out = SoundItOut::full;
    CuePlan said = plan(FeedbackCue::word, "sat", full);
    CHECK(said.speak_word);
    CHECK(said.speak_sequence);

    FeedbackSettings word_only;
    word_only.sound_it_out = SoundItOut::word_only;
    said = plan(FeedbackCue::word, "sat", word_only);
    CHECK(said.speak_word);
    CHECK_FALSE(said.speak_sequence);

    FeedbackSettings spoken_off;
    spoken_off.sound_it_out = SoundItOut::off;
    said = plan(FeedbackCue::word, "sat", spoken_off);
    CHECK_FALSE(said.speak_word);
    CHECK_FALSE(said.speak_sequence);
    // The cheer is Celebration sounds' and still plays.
    CHECK(said.sound);

    // A word with no text (a damaged cue) is not said.
    CHECK_FALSE(plan(FeedbackCue::word, "", full).speak_word);
}

TEST_CASE("a speak cue says its detail as text, and nothing else", "[feedback]") {
    const CuePlan box = plan(FeedbackCue::speak, "box");
    CHECK(box.speak_text);
    CHECK_FALSE(box.sound);
    CHECK_FALSE(box.speak_word);
    CHECK_FALSE(plan(FeedbackCue::speak, "").speak_text);
    CHECK_FALSE(plan(FeedbackCue::tick, "sat").speak_text);
}

TEST_CASE("a real word that is not today's is a soft tick and nothing else", "[feedback]") {
    const CuePlan tick = plan(FeedbackCue::tick, "sat");
    CHECK(tick.sound);
    CHECK(tick.asset == "tick");
    CHECK(tick.volume < 0.5);  // soft: below the tap's half
    CHECK_FALSE(tick.speak_word);
    CHECK_FALSE(tick.speak_text);
    CHECK(tick.haptic == HapticCue::none);
}

TEST_CASE("vibration is its own switch and drops every haptic", "[feedback]") {
    FeedbackSettings on;
    on.sound = false;  // sound and vibration are separate
    CHECK(plan(FeedbackCue::rattle, {}, on).haptic == HapticCue::light);
    CHECK(plan(FeedbackCue::tile, {}, on).haptic == HapticCue::light);
    CHECK(plan(FeedbackCue::word, "sat", on).haptic == HapticCue::medium);
    CHECK(plan(FeedbackCue::chime, {}, on).haptic == HapticCue::medium);
    CHECK(plan(FeedbackCue::cleared, {}, on).haptic == HapticCue::double_tap);
    CHECK(plan(FeedbackCue::tile_landed, {}, on).haptic == HapticCue::none);
    CHECK(plan(FeedbackCue::tick, {}, on).haptic == HapticCue::none);

    FeedbackSettings off;
    off.vibration = false;
    for (const FeedbackCue cue : {FeedbackCue::rattle, FeedbackCue::tile, FeedbackCue::word,
                                  FeedbackCue::chime, FeedbackCue::cleared}) {
        CHECK(plan(cue, "sat", off).haptic == HapticCue::none);
    }
    // And a vibration-only plan still sounds when sound is on.
    CHECK(plan(FeedbackCue::rattle, {}, off).sound);
}
