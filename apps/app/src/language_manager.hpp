// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

class QTranslator;

namespace wjs::app {

// The interface's language, in one place: the catalogue installed on the
// application, the layout direction and the code the screens read
// (docs/design/application.md, "Languages").
//
// The parent's Language setting is "" for the device's own language, or one of
// the eight codes (`languages`). The device's language is used when it is one of
// the eight and English otherwise. Applying a language in the parent area
// installs its catalogue at once and tells the window, which sets
// `LayoutMirroring` from `rightToLeft`; the engine's bindings are retranslated
// by whoever owns the engine, through `languageChanged` (main.cpp connects it to
// `QQmlEngine::retranslate`).
//
// The catalogues are the `:/i18n/wjs_<code>.qm` resources built from `i18n/`.
// The board, the dice letters and the word list are never translated: the game
// is an English word game and the chrome around it is what changes.
class LanguageManager : public QObject {
    Q_OBJECT

    // The code in force: one of the eight, never empty ("en" before apply()).
    Q_PROPERTY(QString code READ code NOTIFY languageChanged)
    // The setting it came from: "" for the device's own, else a code.
    Q_PROPERTY(QString setting READ setting NOTIFY languageChanged)
    // Hebrew and Yiddish read right to left; the other six left to right.
    Q_PROPERTY(bool rightToLeft READ rightToLeft NOTIFY languageChanged)

public:
    explicit LanguageManager(QObject* parent = nullptr);
    ~LanguageManager() override;

    // The eight codes, in the order the store and `wjs::languages` have them.
    [[nodiscard]] static QStringList languages();
    [[nodiscard]] static bool is_known(const QString& code);
    // Hebrew and Yiddish, and nothing else, are right to left.
    [[nodiscard]] static bool is_right_to_left(const QString& code);
    // The code a locale name ("he_IL", "fr-CA", "ja") names: the first part in
    // lower case when it is one of the eight, English otherwise.
    [[nodiscard]] static QString language_from_locale(const QString& locale_name);
    // The device's own language, as above.
    [[nodiscard]] static QString system_language();
    // The code a setting comes to: "" is the device's, an unknown code is English.
    [[nodiscard]] static QString resolved(const QString& setting);

    [[nodiscard]] QString code() const { return code_; }
    [[nodiscard]] QString setting() const { return setting_; }
    [[nodiscard]] bool rightToLeft() const;

    // Applies a setting ("" or one of the eight), installs its catalogue, sets
    // the application's layout direction and returns the code in force. Doing
    // the same language again changes nothing and emits nothing.
    Q_INVOKABLE QString apply(const QString& setting);

signals:
    // The code changed: the screens retranslate and mirror themselves from here.
    void languageChanged();

private:
    void install(const QString& code);

    QString code_ = QStringLiteral("en");
    QString setting_;
    std::unique_ptr<QTranslator> translator_;
};

}  // namespace wjs::app
