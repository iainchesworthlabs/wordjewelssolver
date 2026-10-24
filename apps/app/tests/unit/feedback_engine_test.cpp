// SPDX-License-Identifier: GPL-3.0-or-later
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "demo_data.hpp"
#include "fake_clips.hpp"
#include "feedback_engine.hpp"

// The wiring behind Feedback.cue: the parent's settings map in, and the sound,
// speech and haptics requests out. The sounds themselves need a speaker and the
// voice needs a platform engine, so the cases below choose settings that make
// the policy silent: what is checked is what was asked for, which is the wiring
// docs/design/application.md's table describes. The cue-to-asset mapping and
// every gating rule are the pure policy's case (libs/core/tests/unit/feedback_test.cpp).

namespace {

using wjs::app::FeedbackEngine;

[[nodiscard]] QVariantMap parent_settings(bool sound, bool vibration, const QString& celebration,
                                          const QString& sound_it_out) {
    QVariantMap settings;
    settings.insert(QStringLiteral("sound"), sound);
    settings.insert(QStringLiteral("vibration"), vibration);
    settings.insert(QStringLiteral("celebrationSounds"), celebration);
    settings.insert(QStringLiteral("soundItOut"), sound_it_out);
    // The rest of the parent's map is present and irrelevant; a settings map is
    // the whole of ParentController::settings, not four keys.
    settings.insert(QStringLiteral("help"), true);
    settings.insert(QStringLiteral("boardSize"), 3);
    return settings;
}

// What the engine asked of the sound and the voice, caught where a speaker and a
// voice would be, so that a case that wants the positive path makes no noise.
struct Heard {
    std::vector<std::pair<QString, double>> sounds;
    std::vector<std::pair<QString, double>> speech;
    int requests = 0;
    int settings = 0;
};

void listen(FeedbackEngine& engine, Heard& heard) {
    engine.soundEngine().setOutput([&heard](const QString& asset, double volume) {
        heard.sounds.emplace_back(asset, volume);
    });
    engine.speech().setOutput(
        [&heard](const QString& text, double volume) { heard.speech.emplace_back(text, volume); });
    QObject::connect(&engine, &FeedbackEngine::requestsChanged, &engine,
                     [&heard] { ++heard.requests; });
    QObject::connect(&engine, &FeedbackEngine::settingsChanged, &engine,
                     [&heard] { ++heard.settings; });
}

}  // namespace

TEST_CASE("the master Sound switch silences assets and speech and leaves vibration",
          "[app][feedback]") {
    FeedbackEngine engine;
    engine.setSettings(
        parent_settings(false, true, QStringLiteral("full"), QStringLiteral("full")));

    engine.cue(QStringLiteral("rattle"), QString());
    CHECK(engine.soundsPlayed() == 0);
    CHECK(engine.utterances() == 0);
    CHECK(engine.hapticsRequested() == 1);

    engine.cue(QStringLiteral("word"), QStringLiteral("sat"));
    CHECK(engine.soundsPlayed() == 0);
    CHECK(engine.utterances() == 0);
    CHECK(engine.hapticsRequested() == 2);

    engine.cue(QStringLiteral("speak"), QStringLiteral("box"));
    CHECK(engine.utterances() == 0);

    // Vibration is its own switch: off, the same cue asks for no tap.
    engine.setSettings(
        parent_settings(false, false, QStringLiteral("full"), QStringLiteral("full")));
    engine.cue(QStringLiteral("cleared"), QString());
    CHECK(engine.hapticsRequested() == 2);  // unchanged
    CHECK(engine.soundsPlayed() == 0);
}

TEST_CASE("Celebration sounds and Sound it out each stop their own half", "[app][feedback]") {
    FeedbackEngine engine;
    engine.setSettings(parent_settings(true, false, QStringLiteral("off"), QStringLiteral("off")));

    // Sound is on but the celebration is off and the word is not to be said:
    // this find asks for nothing at all, and neither does the new best or the
    // cleared board. No audio device is touched.
    engine.cue(QStringLiteral("word"), QStringLiteral("sat"));
    engine.cue(QStringLiteral("chime"), QString());
    engine.cue(QStringLiteral("cleared"), QString());
    CHECK(engine.soundsPlayed() == 0);
    CHECK(engine.utterances() == 0);
    CHECK(engine.hapticsRequested() == 0);

    // Master Sound off silences the word even while Sound it out says full.
    engine.setSettings(
        parent_settings(false, false, QStringLiteral("off"), QStringLiteral("full")));
    engine.cue(QStringLiteral("word"), QStringLiteral("sat"));
    CHECK(engine.utterances() == 0);
}

TEST_CASE("a cue kind the design does not have is ignored", "[app][feedback]") {
    FeedbackEngine engine;
    engine.setSettings(
        parent_settings(false, true, QStringLiteral("full"), QStringLiteral("full")));
    const int haptics = engine.hapticsRequested();

    engine.cue(QStringLiteral("not-a-cue"), QStringLiteral("anything"));
    engine.cue(QString(), QString());

    CHECK(engine.soundsPlayed() == 0);
    CHECK(engine.utterances() == 0);
    CHECK(engine.hapticsRequested() == haptics);
}

TEST_CASE("the engine starts with the library's defaults", "[app][feedback]") {
    // Before Main.qml binds the parent's map: sound on, full celebrations, full
    // sound-it-out, vibration on. The values are read from the map the engine
    // would use, so a settings row cannot be missing a default.
    const FeedbackEngine engine;
    const QVariantMap settings = engine.settings();
    CHECK(settings.value(QStringLiteral("sound")).toBool());
    CHECK(settings.value(QStringLiteral("vibration")).toBool());
    CHECK(settings.value(QStringLiteral("celebrationSounds")).toString() == QStringLiteral("full"));
    CHECK(settings.value(QStringLiteral("soundItOut")).toString() == QStringLiteral("full"));
}

// --- the positive paths: what a cue asks for when it is not silenced -------------

TEST_CASE("a found word at full volume asks for the cheer and the voice, once each",
          "[app][feedback]") {
    FeedbackEngine engine;
    Heard heard;
    listen(engine, heard);
    engine.setSettings(parent_settings(true, true, QStringLiteral("full"), QStringLiteral("full")));

    engine.cue(QStringLiteral("word"), QStringLiteral("sat"));

    REQUIRE(heard.sounds.size() == 1);
    CHECK(heard.sounds[0].first == QStringLiteral("cheer"));
    CHECK(heard.sounds[0].second == 1.0);
    REQUIRE(heard.speech.size() == 1);
    CHECK(heard.speech[0].first == QStringLiteral("sat"));
    CHECK(heard.speech[0].second == 1.0);
    CHECK(engine.soundsPlayed() == 1);
    CHECK(engine.utterances() == 1);
    CHECK(engine.hapticsRequested() == 1);
    // One accepted cue is one report of requests, however many things it asked.
    CHECK(heard.requests == 1);
}

TEST_CASE("a quiet celebration is half as loud, in the cheer and in the word", "[app][feedback]") {
    FeedbackEngine engine;
    Heard heard;
    listen(engine, heard);
    engine.setSettings(
        parent_settings(true, true, QStringLiteral("quiet"), QStringLiteral("full")));

    engine.cue(QStringLiteral("word"), QStringLiteral("sat"));

    REQUIRE(heard.sounds.size() == 1);
    REQUIRE(heard.speech.size() == 1);
    CHECK(heard.sounds[0].second == 0.5);
    CHECK(heard.speech[0].second == 0.5);
}

TEST_CASE("the defaults the engine starts with are the ones it plays by", "[app][feedback]") {
    // Before the parent's map is bound: a word is cheered and said in full.
    FeedbackEngine engine;
    Heard heard;
    listen(engine, heard);

    engine.cue(QStringLiteral("word"), QStringLiteral("pin"));

    REQUIRE(heard.sounds.size() == 1);
    CHECK(heard.sounds[0].first == QStringLiteral("cheer"));
    REQUIRE(heard.speech.size() == 1);
    CHECK(heard.speech[0].first == QStringLiteral("pin"));
    CHECK(heard.sounds[0].second == 1.0);
}

TEST_CASE("the rattle plays its sound and buzzes, and says nothing", "[app][feedback]") {
    FeedbackEngine engine;
    Heard heard;
    listen(engine, heard);

    engine.cue(QStringLiteral("rattle"), QString());

    REQUIRE(heard.sounds.size() == 1);
    CHECK(heard.sounds[0].first == QStringLiteral("rattle"));
    CHECK(heard.sounds[0].second == 0.75);
    CHECK(heard.speech.empty());
    CHECK(engine.hapticsRequested() == 1);
    CHECK(heard.requests == 1);
}

TEST_CASE("a word's name is said as text for a picture, and not as a found word",
          "[app][feedback]") {
    FeedbackEngine engine;
    Heard heard;
    listen(engine, heard);

    engine.cue(QStringLiteral("speak"), QStringLiteral("box"));

    CHECK(heard.sounds.empty());
    REQUIRE(heard.speech.size() == 1);
    CHECK(heard.speech[0].first == QStringLiteral("box"));
    CHECK(engine.utterances() == 1);
    CHECK(heard.requests == 1);
}

TEST_CASE("Sound it out set to word still says the word, and off does not", "[app][feedback]") {
    FeedbackEngine engine;
    Heard heard;
    listen(engine, heard);

    engine.setSettings(
        parent_settings(true, false, QStringLiteral("full"), QStringLiteral("word")));
    engine.cue(QStringLiteral("word"), QStringLiteral("sat"));
    CHECK(heard.speech.size() == 1);

    engine.setSettings(parent_settings(true, false, QStringLiteral("full"), QStringLiteral("off")));
    engine.cue(QStringLiteral("word"), QStringLiteral("sat"));
    CHECK(heard.speech.size() == 1);  // no second utterance
    CHECK(heard.sounds.size() == 2);  // the cheer still plays
}

TEST_CASE("requests are reported once for each cue that asked for something, and for no other",
          "[app][feedback]") {
    FeedbackEngine engine;
    Heard heard;
    listen(engine, heard);

    engine.cue(QStringLiteral("rattle"), QString());
    CHECK(heard.requests == 1);
    engine.cue(QStringLiteral("not-a-cue"), QString());
    CHECK(heard.requests == 1);

    // Everything silenced: the cue is understood and asks for nothing.
    engine.setSettings(parent_settings(false, false, QStringLiteral("off"), QStringLiteral("off")));
    engine.cue(QStringLiteral("rattle"), QString());
    engine.cue(QStringLiteral("word"), QStringLiteral("sat"));
    CHECK(heard.requests == 1);
    CHECK(heard.sounds.size() == 1);
}

TEST_CASE("a word with no letters is cheered but not said, and the counts agree",
          "[app][feedback]") {
    FeedbackEngine engine;
    Heard heard;
    listen(engine, heard);

    engine.cue(QStringLiteral("word"), QString());

    CHECK(heard.sounds.size() == 1);
    CHECK(heard.speech.empty());
    // The engine's count is the voice's: nothing was said, so nothing is counted.
    CHECK(engine.utterances() == 0);
    CHECK(engine.speech().utterances() == 0);
}

TEST_CASE("the settings are reported once for each map that differs", "[app][feedback]") {
    FeedbackEngine engine;
    Heard heard;
    listen(engine, heard);
    const QVariantMap quiet =
        parent_settings(true, true, QStringLiteral("quiet"), QStringLiteral("full"));

    engine.setSettings(quiet);
    CHECK(heard.settings == 1);
    engine.setSettings(quiet);
    CHECK(heard.settings == 1);
    engine.setSettings(parent_settings(true, true, QStringLiteral("full"), QStringLiteral("full")));
    CHECK(heard.settings == 2);
}

TEST_CASE("a damaged or missing value falls back to the library's default", "[app][feedback]") {
    FeedbackEngine engine;
    Heard heard;
    listen(engine, heard);

    // The words are not ones the table has: full is what a cheer and a word get.
    QVariantMap damaged;
    damaged.insert(QStringLiteral("celebrationSounds"), QStringLiteral("bogus"));
    damaged.insert(QStringLiteral("soundItOut"), QStringLiteral("nonsense"));
    engine.setSettings(damaged);  // sound and vibration are missing: on
    engine.cue(QStringLiteral("word"), QStringLiteral("sat"));

    REQUIRE(heard.sounds.size() == 1);
    CHECK(heard.sounds[0].second == 1.0);
    CHECK(heard.speech.size() == 1);
    CHECK(engine.hapticsRequested() == 1);
}

// --- sounding a found word out ---------------------------------------------------

namespace {

// The engine over the demo phases' phase B ("ai" is its grapheme; PAIN is a word of
// it), told it is on the phonics board, with the sequence's waits in the test's hands.
struct SoundItOutRig {
    explicit SoundItOutRig(const QString& sound_it_out = QStringLiteral("full"),
                           const QString& board = QStringLiteral("phonics"))
        : phases(wjs::app::demo::phases()) {
        engine.usePhase(phases.find("B"));
        listen(engine, heard);
        QVariantMap settings = parent_settings(true, false, QStringLiteral("full"), sound_it_out);
        settings.insert(QStringLiteral("board"), board);
        engine.setSettings(settings);
        engine.soundSequence().setScheduler(
            [this](std::chrono::milliseconds, std::function<void()> then) {
                waits.push_back(std::move(then));
            });
        QObject::connect(&engine.soundSequence(), &wjs::app::SoundSequencePlayer::stepStarted,
                         &engine, [this](const QString& kind, int block) {
                             steps.push_back(kind + QStringLiteral(":") + QString::number(block));
                         });
    }

    wjs::PhaseSet phases;
    FeedbackEngine engine;
    Heard heard;
    QStringList steps;
    std::vector<std::function<void()>> waits;
    std::vector<std::string> log;
};

}  // namespace

TEST_CASE("a found word of the phonics board is sounded out, then said",
          "[app][feedback][sequence]") {
    SoundItOutRig rig;

    rig.engine.cue(QStringLiteral("word"), QStringLiteral("pain"));

    // p, ai, n; the blend; the word; and the cheer that names "ai".
    CHECK(rig.steps == QStringList{"sound:0", "sound:1", "sound:2", "blend:0", "say:0", "cheer:0"});
    // No clip is recorded, so the sounds take no time and the word follows at once.
    REQUIRE(rig.heard.speech.size() == 1);
    CHECK(rig.heard.speech[0].first == QStringLiteral("pain"));
    CHECK(rig.engine.utterances() == 1);
    CHECK(rig.heard.requests == 1);
    CHECK(rig.waits.empty());
}

TEST_CASE("Sound it out set to the word only says the word and sounds nothing out",
          "[app][feedback][sequence]") {
    SoundItOutRig rig(QStringLiteral("word"));

    rig.engine.cue(QStringLiteral("word"), QStringLiteral("pain"));

    CHECK(rig.steps == QStringList{"say:0"});
    CHECK(rig.heard.speech.size() == 1);
}

TEST_CASE("a word the phase does not hold, the standard board and no phase say the word whole",
          "[app][feedback][sequence]") {
    {
        SoundItOutRig rig;
        rig.engine.cue(QStringLiteral("word"), QStringLiteral("cat"));  // not in phase B
        CHECK(rig.steps.isEmpty());
        CHECK(rig.heard.speech.size() == 1);
    }
    {
        SoundItOutRig rig(QStringLiteral("full"), QStringLiteral("standard"));
        rig.engine.cue(QStringLiteral("word"), QStringLiteral("pain"));
        CHECK(rig.steps.isEmpty());
        CHECK(rig.heard.speech.size() == 1);
    }
    {
        SoundItOutRig rig;
        rig.engine.usePhase(nullptr);
        rig.engine.cue(QStringLiteral("word"), QStringLiteral("pain"));
        CHECK(rig.steps.isEmpty());
        CHECK(rig.heard.speech.size() == 1);
    }
}

TEST_CASE("Sound it out off says and sounds nothing out", "[app][feedback][sequence]") {
    SoundItOutRig rig(QStringLiteral("off"));

    rig.engine.cue(QStringLiteral("word"), QStringLiteral("pain"));

    CHECK(rig.steps.isEmpty());
    CHECK(rig.heard.speech.empty());
    CHECK(rig.engine.utterances() == 0);
}

TEST_CASE("with clips the word waits for the sounds, and the next word stops them",
          "[app][feedback][sequence]") {
    SoundItOutRig rig;
    auto clips = std::make_unique<wjs::app::test::FakeClips>(
        std::set<std::string>{"t3", "t4", "t5", "t6", "t7"}, rig.log);
    wjs::app::test::FakeClips* fake = clips.get();
    rig.engine.soundSequence().setClipPlayer(std::move(clips));

    rig.engine.cue(QStringLiteral("word"), QStringLiteral("pain"));

    // The cue is accepted and the first sound is playing; the word is not said yet.
    CHECK(rig.heard.requests == 1);
    CHECK(fake->playing());
    CHECK(rig.heard.speech.empty());
    CHECK(rig.engine.utterances() == 0);

    // Every sound and the blend play, then the word.
    while (rig.engine.soundSequence().running()) {
        if (fake->playing()) {
            fake->end();
        } else {
            REQUIRE_FALSE(rig.waits.empty());
            auto next = std::move(rig.waits.front());
            rig.waits.erase(rig.waits.begin());
            next();
        }
    }
    REQUIRE(rig.heard.speech.size() == 1);
    CHECK(rig.heard.speech[0].first == QStringLiteral("pain"));

    // A second word found while the first's sounds are playing stops them, and the
    // first word is never said.
    rig.heard.speech.clear();
    rig.engine.cue(QStringLiteral("word"), QStringLiteral("saint"));
    REQUIRE(fake->playing());
    rig.engine.cue(QStringLiteral("word"), QStringLiteral("stain"));
    CHECK(std::ranges::count(rig.log, std::string{"stop"}) >= 1);
    while (rig.engine.soundSequence().running()) {
        if (fake->playing()) {
            fake->end();
        } else if (!rig.waits.empty()) {
            auto next = std::move(rig.waits.front());
            rig.waits.erase(rig.waits.begin());
            next();
        } else {
            break;
        }
    }
    REQUIRE(rig.heard.speech.size() == 1);
    CHECK(rig.heard.speech[0].first == QStringLiteral("stain"));
}
