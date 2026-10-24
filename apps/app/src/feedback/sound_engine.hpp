// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

namespace wjs::app {

// A short sound from assets/sounds, played through Qt Multimedia when the
// application was built with it and silently doing nothing when it was not.
//
// Which asset a moment plays, and how loudly, is the pure policy in
// wjs/feedback.hpp; this only carries it out. It never records anything: there
// is no audio input in this class and none in the application (section 12.2).
//
// A missing audio device is not an error. A play() is counted and reported
// whether or not a speaker was there to hear it, so the cue wiring is testable
// on a runner with no sound card and a child's device that has its silent
// switch on just plays nothing.
class SoundEngine : public QObject {
    Q_OBJECT

    // Whether the build carries Qt Multimedia (not whether a speaker is
    // plugged in: a device may have neither at any moment).
    Q_PROPERTY(bool available READ available CONSTANT)
    // How many assets have been asked to play. A request, not a sound heard.
    Q_PROPERTY(int plays READ plays NOTIFY played)

public:
    explicit SoundEngine(QObject* parent = nullptr);
    ~SoundEngine() override;

    [[nodiscard]] bool available() const;
    [[nodiscard]] int plays() const;

    // `asset` is a stem under assets/sounds ("rattle"); `volume` is 0 to 1.
    void play(const QString& asset, double volume);

    // Where the sound goes instead of the platform's backend: a test that must
    // not make a noise, or a tool. The play is still counted and reported. An
    // empty function puts the backend back.
    using Output = std::function<void(const QString& asset, double volume)>;
    void setOutput(Output output);

signals:
    // A play was asked for, with the asset and the volume it was asked at.
    void played(const QString& asset, double volume);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace wjs::app
