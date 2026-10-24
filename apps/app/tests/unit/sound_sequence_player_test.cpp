// SPDX-License-Identifier: GPL-3.0-or-later
#include <QString>
#include <QStringList>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fake_clips.hpp"
#include "sound_sequence_player.hpp"
#include "wjs/sound_it_out.hpp"

// The player of the sound-it-out sequence (apps/app/src/feedback/sound_sequence_player.*)
// over a fake clip player and a clock the test winds by hand: no sound is made and
// no time passes. The steps themselves are the library's (libs/core/tests/unit/
// sound_it_out_test.cpp); here they are written out, so that what is checked is the
// walk: the order, the waits, the gaps, a clip that is not there, and a sequence
// stopped or replaced.

namespace {

using std::chrono::milliseconds;
using wjs::SoundSequence;
using wjs::SoundStep;
using wjs::SoundStepKind;
using wjs::app::test::FakeClips;

// The clock: waits are collected, and the test runs the next.
struct Clock {
    std::vector<std::pair<milliseconds, std::function<void()>>> waits;

    wjs::app::SoundSequencePlayer::Scheduler scheduler() {
        return [this](milliseconds delay, std::function<void()> then) {
            waits.emplace_back(delay, std::move(then));
        };
    }

    // Runs the oldest wait and says how long it was.
    milliseconds run_next() {
        REQUIRE_FALSE(waits.empty());
        auto next = std::move(waits.front());
        waits.erase(waits.begin());
        next.second();
        return next.first;
    }
};

SoundStep sound(std::size_t block, const char* letters, const char* clip, milliseconds gap) {
    SoundStep step;
    step.kind = SoundStepKind::sound;
    step.gap_before = gap;
    step.block = block;
    step.letters = letters;
    step.clips = {clip};
    return step;
}

// "pain": p, ai, n; then the blend, the word, and the cheer that names "ai".
SoundSequence pain() {
    SoundSequence sequence;
    sequence.word = "pain";
    sequence.steps.push_back(sound(0, "p", "t1", milliseconds{0}));
    sequence.steps.push_back(sound(1, "ai", "t7", wjs::segment_gap));
    sequence.steps.push_back(sound(2, "n", "t3", wjs::segment_gap));
    SoundStep blend;
    blend.kind = SoundStepKind::blend;
    blend.gap_before = wjs::segment_gap;
    blend.duration = wjs::blend_duration;
    blend.letters = "pain";
    blend.clips = {"t1", "t7", "t3"};
    sequence.steps.push_back(blend);
    SoundStep say;
    say.kind = SoundStepKind::say;
    say.letters = "pain";
    sequence.steps.push_back(say);
    SoundStep cheer;
    cheer.kind = SoundStepKind::cheer;
    cheer.letters = "ai";
    cheer.sound = "/t7/";
    sequence.steps.push_back(cheer);
    return sequence;
}

struct Rig {
    Rig()
        : player([this](const QString& word, double volume) {
              said.push_back(word);
              log.push_back("say " + word.toStdString());
              said_volume = volume;
          }) {
        player.setScheduler(clock.scheduler());
        QObject::connect(&player, &wjs::app::SoundSequencePlayer::stepStarted, &player,
                         [this](const QString& kind, int block) {
                             steps.push_back(kind + ":" + QString::number(block));
                         });
        QObject::connect(&player, &wjs::app::SoundSequencePlayer::finished, &player,
                         [this] { ++finished; });
    }

    // Gives the player clips, keeping the fake to drive.
    FakeClips* clips_of(std::set<std::string> have) {
        auto clips = std::make_unique<FakeClips>(std::move(have), log);
        FakeClips* fake = clips.get();
        player.setClipPlayer(std::move(clips));
        return fake;
    }

    std::vector<std::string> log;
    QStringList said;
    QStringList steps;
    double said_volume = -1.0;
    int finished = 0;
    Clock clock;
    wjs::app::SoundSequencePlayer player;
};

}  // namespace

TEST_CASE("with no clips recorded the word is said at once, and the steps are told in order",
          "[app][feedback][sequence]") {
    Rig rig;

    rig.player.start(pain(), 0.75);

    // Nothing to wait for: not a gap, not a clip. The word is said before start
    // returns, as it was before the sequence existed.
    CHECK(rig.said == QStringList{"pain"});
    CHECK(rig.said_volume == 0.75);
    CHECK(rig.clock.waits.empty());
    CHECK_FALSE(rig.player.running());
    CHECK(rig.finished == 1);
    CHECK(rig.steps == QStringList{"sound:0", "sound:1", "sound:2", "blend:0", "say:0", "cheer:0"});
}

TEST_CASE("clips play one after another, with the gap between them, and the word is last",
          "[app][feedback][sequence]") {
    Rig rig;
    FakeClips* clips = rig.clips_of({"t1", "t7", "t3"});

    rig.player.start(pain(), 1.0);

    // The first sound has begun and nothing else: its end is what is waited for.
    CHECK(rig.log == std::vector<std::string>{"clip t1"});
    CHECK(rig.player.running());
    CHECK(rig.said.isEmpty());

    clips->end();
    // 150 ms of silence, then the second.
    REQUIRE(rig.clock.waits.size() == 1);
    CHECK(rig.log.size() == 1);
    CHECK(rig.clock.run_next() == wjs::segment_gap);
    CHECK(rig.log.back() == "clip t7");

    clips->end();
    CHECK(rig.clock.run_next() == wjs::segment_gap);
    CHECK(rig.log.back() == "clip t3");

    // The blend: the same sounds again, back to back, after the gap.
    clips->end();
    CHECK(rig.clock.run_next() == wjs::segment_gap);
    CHECK(rig.log.back() == "clip t1");
    clips->end();
    CHECK(rig.log.back() == "clip t7");
    clips->end();
    CHECK(rig.log.back() == "clip t3");
    CHECK(rig.said.isEmpty());

    // And the last clip's end brings the whole word.
    clips->end();
    CHECK(rig.said == QStringList{"pain"});
    CHECK(rig.finished == 1);
    CHECK_FALSE(rig.player.running());
    CHECK(rig.clock.waits.empty());
}

TEST_CASE("a clip that is not there takes no time and no gap", "[app][feedback][sequence]") {
    Rig rig;
    FakeClips* clips = rig.clips_of({"t1", "t3"});  // no t7, the digraph's

    rig.player.start(pain(), 1.0);
    clips->end();  // p was heard, so the gap before the next sound is waited
    CHECK(rig.clock.run_next() == wjs::segment_gap);
    // The digraph has no clip: it takes no time, and the sound after it follows at
    // once, with no gap, because a gap is silence after a sound that was heard.
    CHECK(rig.log == std::vector<std::string>{"clip t1", "missing t7", "clip t3"});
    CHECK(rig.clock.waits.empty());
}

TEST_CASE("the whole-word-only sequence is the word and nothing else",
          "[app][feedback][sequence]") {
    Rig rig;
    FakeClips* clips = rig.clips_of({"t1"});
    SoundSequence word_only;
    word_only.word = "pain";
    word_only.whole_word_only = true;
    SoundStep say;
    say.kind = SoundStepKind::say;
    say.letters = "pain";
    word_only.steps.push_back(say);

    rig.player.start(std::move(word_only), 0.5);

    CHECK(rig.said == QStringList{"pain"});
    CHECK(rig.steps == QStringList{"say:0"});
    CHECK(clips->playing() == false);
    CHECK(rig.log == std::vector<std::string>{"say pain"});
}

TEST_CASE("a new sequence stops the one under way, and the old one's clip says nothing more",
          "[app][feedback][sequence]") {
    Rig rig;
    FakeClips* clips = rig.clips_of({"t1", "t7", "t3"});
    rig.player.start(pain(), 1.0);
    auto stale_end = clips->take();  // the first sound's end, as the old walk holds it
    REQUIRE(stale_end);

    SoundSequence other = pain();
    other.word = "pane";
    other.steps[4].letters = "pane";
    rig.player.start(std::move(other), 1.0);

    CHECK(rig.log == std::vector<std::string>{"clip t1", "stop", "clip t1"});
    // The old sequence's clip ends late: it finds itself replaced and does nothing.
    stale_end();
    CHECK(rig.clock.waits.empty());
    CHECK(rig.said.isEmpty());
    CHECK(rig.finished == 0);

    // The new one goes on as it should, to its own word.
    clips->end();
    rig.clock.run_next();
    clips->end();
    rig.clock.run_next();
    clips->end();
    rig.clock.run_next();
    clips->end();
    clips->end();
    clips->end();
    CHECK(rig.said == QStringList{"pane"});
}

TEST_CASE("a sequence cancelled in a gap does not go on when the gap is over",
          "[app][feedback][sequence]") {
    Rig rig;
    FakeClips* clips = rig.clips_of({"t1", "t7", "t3"});
    rig.player.start(pain(), 1.0);
    clips->end();
    REQUIRE(rig.clock.waits.size() == 1);

    rig.player.cancel();
    CHECK_FALSE(rig.player.running());
    rig.clock.run_next();  // the timer fires anyway

    CHECK(rig.log.back() == "stop");
    CHECK(rig.said.isEmpty());
    CHECK(rig.finished == 0);
    // Safe again and with nothing running.
    rig.player.cancel();
}

TEST_CASE("the volume reaches the clips and the word", "[app][feedback][sequence]") {
    Rig rig;
    FakeClips* clips = rig.clips_of({"t1"});
    rig.player.start(pain(), 0.5);
    CHECK(clips->last_volume == 0.5);
    while (rig.player.running()) {
        if (clips->playing()) {
            clips->end();
        } else {
            rig.clock.run_next();
        }
    }
    CHECK(rig.said_volume == 0.5);
}
