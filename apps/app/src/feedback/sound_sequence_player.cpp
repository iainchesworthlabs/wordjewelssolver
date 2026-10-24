// SPDX-License-Identifier: GPL-3.0-or-later
#include "sound_sequence_player.hpp"

#include <QTimer>
#include <utility>

#include "qt_text.hpp"
#include "wjs/profiling.hpp"

namespace wjs::app {
namespace {

[[nodiscard]] QString kind_name(wjs::SoundStepKind kind) {
    return to_qstring(wjs::to_string(kind));
}

}  // namespace

SoundSequencePlayer::SoundSequencePlayer(SayWord say, QObject* parent)
    : QObject(parent), say_(std::move(say)), clips_(std::make_unique<NoClips>()) {
    scheduler_ = [this](std::chrono::milliseconds delay, std::function<void()> then) {
        QTimer::singleShot(delay, this, std::move(then));
    };
}

SoundSequencePlayer::~SoundSequencePlayer() {
    cancel();
}

void SoundSequencePlayer::setClipPlayer(std::unique_ptr<ClipPlayer> clips) {
    cancel();
    clips_ = clips ? std::move(clips) : std::make_unique<NoClips>();
}

void SoundSequencePlayer::setScheduler(Scheduler scheduler) {
    scheduler_ = std::move(scheduler);
}

void SoundSequencePlayer::start(wjs::SoundSequence sequence, double volume) {
    // Walking a word's steps, up to the first clip that has to be waited for.
    const auto zone = wjs::profiling::zone<"sound.sequence">();
    cancel();
    sequence_ = std::move(sequence);
    volume_ = volume;
    next_ = 0;
    blend_clip_ = 0;
    in_blend_ = false;
    after_audible_ = false;
    gap_waited_ = false;
    running_ = true;
    advance(serial_);
}

void SoundSequencePlayer::cancel() {
    if (running_) {
        running_ = false;
        clips_->stop();
    }
    // Whatever a clip or a timer of the sequence that was running still holds is
    // stale from here.
    ++serial_;
}

void SoundSequencePlayer::advance(std::uint64_t serial) {
    if (serial != serial_ || !running_) {
        return;
    }
    while (next_ < sequence_.steps.size()) {
        const wjs::SoundStep& step = sequence_.steps[next_];
        const bool is_clip_step =
            step.kind == wjs::SoundStepKind::sound || step.kind == wjs::SoundStepKind::blend;
        // The gap before a sound is waited only after a sound that was heard: the
        // gap is silence between two clips, not a delay before the first.
        if (is_clip_step && after_audible_ && !gap_waited_ && step.gap_before.count() > 0) {
            gap_waited_ = true;
            scheduler_(step.gap_before, [this, serial] { advance(serial); });
            return;
        }
        gap_waited_ = false;

        // Copied before the signal goes out: a slot may start another sequence.
        const wjs::SoundStepKind kind = step.kind;
        const int block = static_cast<int>(step.block);
        const std::string word = step.letters;
        ++next_;
        emit stepStarted(kind_name(kind), block);
        if (serial != serial_ || !running_) {
            return;
        }

        switch (kind) {
            case wjs::SoundStepKind::sound: {
                const wjs::SoundStep& played = sequence_.steps[next_ - 1];
                if (!played.clips.empty() &&
                    clips_->play(played.clips.front(), volume_,
                                 [this, serial] { clipFinished(serial); })) {
                    after_audible_ = true;
                    return;
                }
                after_audible_ = false;
                break;
            }
            case wjs::SoundStepKind::blend: {
                in_blend_ = true;
                blend_clip_ = 0;
                if (playBlendClip(serial)) {
                    after_audible_ = true;
                    return;
                }
                in_blend_ = false;
                after_audible_ = false;
                break;
            }
            case wjs::SoundStepKind::say:
                after_audible_ = false;
                if (say_) {
                    say_(to_qstring(word), volume_);
                }
                break;
            case wjs::SoundStepKind::cheer:
                // The phrase is a recording whose wording the reviewer owns: there
                // is none to play, and the voice does not stand in for it.
                after_audible_ = false;
                break;
        }
    }
    finish();
}

bool SoundSequencePlayer::playBlendClip(std::uint64_t serial) {
    const wjs::SoundStep& blend = sequence_.steps[next_ - 1];
    while (blend_clip_ < blend.clips.size()) {
        if (clips_->play(blend.clips[blend_clip_], volume_,
                         [this, serial] { clipFinished(serial); })) {
            return true;
        }
        ++blend_clip_;
    }
    return false;
}

void SoundSequencePlayer::clipFinished(std::uint64_t serial) {
    if (serial != serial_ || !running_) {
        return;
    }
    if (in_blend_) {
        // The blend plays its clips back to back, the grapheme sounds in order.
        ++blend_clip_;
        if (playBlendClip(serial)) {
            return;
        }
        in_blend_ = false;
    }
    advance(serial);
}

void SoundSequencePlayer::finish() {
    running_ = false;
    emit finished();
}

}  // namespace wjs::app
