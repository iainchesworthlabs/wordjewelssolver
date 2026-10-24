// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "wjs/sound_it_out.hpp"

namespace wjs::app {

// Plays the clips of the sound-it-out sequence. A clip is a pre-recorded phoneme
// (docs/design/phonics.md, "Where the sounds come from"); none is in the tree yet,
// because recording them is the reviewer's job, so the program has the one that
// answers "there is no such clip" (NoClips) and the day they exist this is the
// interface a player of them implements.
class ClipPlayer {
public:
    ClipPlayer() = default;
    virtual ~ClipPlayer() = default;

    ClipPlayer(const ClipPlayer&) = delete;
    ClipPlayer& operator=(const ClipPlayer&) = delete;

    // Starts the clip with this id. False, and nothing played, when there is no
    // such clip. `finished` is called when the clip has ended, from the event loop
    // and never from inside this call.
    virtual bool play(const std::string& clip, double volume, std::function<void()> finished) = 0;

    // Stops whatever clip is playing. Safe when none is. Its `finished` is not
    // called.
    virtual void stop() = 0;
};

// The clips of a build with none recorded: every id is "no such clip".
class NoClips final : public ClipPlayer {
public:
    bool play(const std::string&, double, std::function<void()>) override { return false; }
    void stop() override {}
};

// Walks a wjs::SoundSequence: the list of steps the library makes of a word, a
// phase and the parent's setting (wjs/sound_it_out.hpp). It plays nothing itself.
// A `sound` or `blend` step is played through the ClipPlayer, one clip after
// another, each starting when the one before has ended; the `say` step is the
// whole word, handed to the voice. The `cheer` is a recorded phrase whose wording
// is the reviewer's, so it is skipped until there is one.
//
// A step whose clip does not exist takes no time at all, and neither does the gap
// before it: the design lights a block when its sound *starts* (the clip's start
// event, not a clock), so with no sound there is nothing to wait for and the word
// is said at once, as it was before the sequence existed. When a clip does play,
// the gap before the next sound is the library's `gap_before` (150 ms).
//
// One sequence runs at a time: starting another (the next word is found) stops
// the one under way.
class SoundSequencePlayer : public QObject {
    Q_OBJECT

public:
    // Says the whole word through the voice.
    using SayWord = std::function<void(const QString& word, double volume)>;
    // Runs the function after the delay, on the event loop. The default is a
    // single-shot timer owned by the player; a test supplies its own and runs the
    // waits by hand.
    using Scheduler = std::function<void(std::chrono::milliseconds, std::function<void()>)>;

    explicit SoundSequencePlayer(SayWord say, QObject* parent = nullptr);
    ~SoundSequencePlayer() override;

    void setClipPlayer(std::unique_ptr<ClipPlayer> clips);
    void setScheduler(Scheduler scheduler);

    // Starts the sequence at `volume` (0 to 1), after stopping the one under way.
    // The steps up to the first wait run before this returns, so a sequence with
    // no clips has said its word by then.
    void start(wjs::SoundSequence sequence, double volume);

    // Stops the sequence and its clip. Safe when none is running.
    void cancel();

    [[nodiscard]] bool running() const noexcept { return running_; }

signals:
    // A step is beginning: "sound" (block: the grapheme's index in the word, the
    // block to light), "blend", "say" or "cheer". Emitted for every step, in order,
    // as it begins; for a step with no clip that is at once.
    void stepStarted(const QString& kind, int block);
    // The last step has begun and its clip, if any, has ended.
    void finished();

private:
    void advance(std::uint64_t serial);
    bool playBlendClip(std::uint64_t serial);
    void clipFinished(std::uint64_t serial);
    void finish();

    SayWord say_;
    std::unique_ptr<ClipPlayer> clips_;
    Scheduler scheduler_;
    wjs::SoundSequence sequence_;
    double volume_ = 1.0;
    std::size_t next_ = 0;        // the next step to begin
    std::size_t blend_clip_ = 0;  // the next clip of the blend under way
    bool in_blend_ = false;
    bool after_audible_ = false;  // the step before played a clip, so a gap follows it
    bool gap_waited_ = false;
    bool running_ = false;
    std::uint64_t serial_ = 0;  // a callback of an older sequence finds this changed
};

}  // namespace wjs::app
