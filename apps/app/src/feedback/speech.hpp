// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

namespace wjs::app {

// The device's own voice, over Qt TextToSpeech when the application was built
// with it and silently nothing when it was not. Whole words and sentences only:
// the isolated phonemes of the sound-it-out sequence are recordings the reviewer
// owns (docs/design/phonics.md), which SoundSequencePlayer plays; the voice
// never stands in for one, because letter names are not the sounds a child is
// learning. It never captures audio: speech is output only.
//
// A build or a device with no voice engine answers available() false and every
// say() is a harmless request. The application's policy decides whether a word
// is said at all (Sound it out, and the master Sound switch); this class does
// not read the settings.
class Speech : public QObject {
    Q_OBJECT

    // Whether the platform has a text-to-speech engine at all.
    Q_PROPERTY(bool available READ available CONSTANT)
    // How many utterances have been asked for. A request, not a voice heard.
    Q_PROPERTY(int utterances READ utterances NOTIFY spoken)

public:
    explicit Speech(QObject* parent = nullptr);
    ~Speech() override;

    [[nodiscard]] bool available() const;
    [[nodiscard]] int utterances() const;

    // The whole word, through the platform's voice. `volume` is 0 to 1. False,
    // and nothing said, for an empty word.
    bool say_word(const QString& word, double volume = 1.0);

    // Where the voice goes instead of the platform's engine: a test that must not
    // speak, or a tool. The utterance is still counted and reported. An empty
    // function puts the engine back.
    using Output = std::function<void(const QString& text, double volume)>;
    void setOutput(Output output);

    // A sentence or a picture's name, through the platform's voice. False, and
    // nothing said, for an empty text.
    bool say_text(const QString& text, double volume = 1.0);

    // Stops whatever is being said. Safe when nothing is.
    void stop();

signals:
    void spoken(const QString& text);

private:
    bool say(const QString& text, double volume);

    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace wjs::app
