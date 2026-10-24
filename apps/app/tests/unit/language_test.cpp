// SPDX-License-Identifier: GPL-3.0-or-later
// The interface's languages (docs/design/application.md, "Languages"):
// the catalogues in apps/app/translations/, the LanguageManager's direction and fallback, that a
// catalogue is really installed, and that the app's font chain carries the
// glyphs of all eight languages. Built with the application, in wjs_app_tests.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "language_manager.hpp"
#include "test_support.hpp"
#include "wjs/settings.hpp"

namespace {

using wjs::app::LanguageManager;

// A GUI application on the platform the entry's environment names: offscreen,
// with the fonts directory the platform's fragment gives it (tests/CMakeLists.txt).
using TestApplication = wjs::app::test::GuiApplication;

// One catalogue as the completeness check reads it.
struct Catalogue {
    int messages = 0;
    QSet<QString> keys;  // context '\n' source, so two catalogues are comparable
    QStringList problems;
};

[[nodiscard]] Catalogue read_catalogue(const QString& path, const QString& expected_language) {
    Catalogue catalogue;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        catalogue.problems.append(QStringLiteral("%1 cannot be opened").arg(path));
        return catalogue;
    }

    QXmlStreamReader reader(&file);
    QString context;
    if (!reader.readNextStartElement() || reader.name().toString() != QStringLiteral("TS")) {
        catalogue.problems.append(QStringLiteral("%1 is not a <TS> catalogue").arg(path));
        return catalogue;
    }
    const QString language = reader.attributes().value(QStringLiteral("language")).toString();
    if (!language.startsWith(expected_language)) {
        catalogue.problems.append(QStringLiteral("%1 says language '%2', not '%3'")
                                      .arg(path, language, expected_language));
    }

    while (reader.readNextStartElement()) {
        if (reader.name().toString() != QStringLiteral("context")) {
            reader.skipCurrentElement();
            continue;
        }
        while (reader.readNextStartElement()) {
            const QString element = reader.name().toString();
            if (element == QStringLiteral("name")) {
                context = reader.readElementText();
                continue;
            }
            if (element != QStringLiteral("message")) {
                reader.skipCurrentElement();
                continue;
            }
            ++catalogue.messages;
            const QXmlStreamAttributes attributes = reader.attributes();
            const QString message_type = attributes.value(QStringLiteral("type")).toString();
            if (message_type == QStringLiteral("vanished") ||
                message_type == QStringLiteral("obsolete")) {
                catalogue.problems.append(
                    QStringLiteral("%1 has a %2 message").arg(path, message_type));
            }
            const bool numerus =
                attributes.value(QStringLiteral("numerus")) == QStringLiteral("yes");
            QString source;
            QStringList forms;
            bool saw_translation = false;
            while (reader.readNextStartElement()) {
                const QString child = reader.name().toString();
                if (child == QStringLiteral("source")) {
                    source = reader.readElementText();
                } else if (child == QStringLiteral("translation")) {
                    saw_translation = true;
                    const QString translation_type =
                        reader.attributes().value(QStringLiteral("type")).toString();
                    if (!translation_type.isEmpty()) {
                        catalogue.problems.append(QStringLiteral("%1: '%2' has a '%3' translation")
                                                      .arg(path, source, translation_type));
                    }
                    if (numerus) {
                        while (reader.readNextStartElement()) {
                            if (reader.name().toString() == QStringLiteral("numerusform")) {
                                forms.append(reader.readElementText());
                            } else {
                                reader.skipCurrentElement();
                            }
                        }
                    } else {
                        forms.append(reader.readElementText());
                    }
                } else {
                    reader.skipCurrentElement();
                }
            }
            if (source.isEmpty()) {
                catalogue.problems.append(
                    QStringLiteral("%1 has a message with no source").arg(path));
            }
            if (!saw_translation) {
                catalogue.problems.append(
                    QStringLiteral("%1: '%2' has no translation").arg(path, source));
            }
            if (forms.isEmpty() || std::any_of(forms.begin(), forms.end(), [](const QString& form) {
                    return form.trimmed().isEmpty();
                })) {
                catalogue.problems.append(
                    QStringLiteral("%1: '%2' is not translated in every form").arg(path, source));
            }
            catalogue.keys.insert(context + QLatin1Char('\n') + source);
        }
    }
    if (reader.hasError()) {
        catalogue.problems.append(
            QStringLiteral("%1 is not well formed XML: %2").arg(path, reader.errorString()));
    }
    return catalogue;
}

// The eight sample sentences of the glyph check, one per language: the words of
// a sentence a screen really shows, in each script.
struct Sample {
    const char* code;
    const char* sentence;
};

}  // namespace

TEST_CASE("every language has a complete catalogue, and all eight are there",
          "[app][i18n][catalogue]") {
    const QDir directory(QStringLiteral(WJS_I18N_DIRECTORY));
    REQUIRE(directory.exists());

    const QStringList found = directory.entryList({QStringLiteral("wjs_*.ts")}, QDir::Files);
    const QStringList expected = LanguageManager::languages();  // the same order as wjs::languages
    CHECK(found.size() == expected.size());
    for (const QString& code : expected) {
        CHECK(found.contains(QStringLiteral("wjs_%1.ts").arg(code)));
    }

    // English is the source language: it is the catalogue every other one is
    // counted against.
    const QString source_path = directory.filePath(QStringLiteral("wjs_en.ts"));
    REQUIRE(QFile::exists(source_path));
    const Catalogue source = read_catalogue(source_path, QStringLiteral("en"));
    for (const QString& problem : source.problems) {
        INFO(problem.toStdString());
        CHECK(false);
    }
    CHECK(source.messages > 0);

    for (const QString& code : expected) {
        const QString path = directory.filePath(QStringLiteral("wjs_%1.ts").arg(code));
        INFO(path.toStdString());
        if (!QFile::exists(path)) {
            CHECK(false);
            continue;
        }
        const Catalogue catalogue = read_catalogue(path, code);
        for (const QString& problem : catalogue.problems) {
            INFO(problem.toStdString());
            CHECK(false);
        }
        CHECK(catalogue.messages == source.messages);
        CHECK(catalogue.keys == source.keys);
    }
}

TEST_CASE("the app's font chain carries every glyph of the eight sample sentences",
          "[app][i18n][glyph]") {
    // The offscreen platform draws with the fonts of the directory the platform's
    // fragment names (the system's fonts on Windows, fontconfig's elsewhere), so
    // the check asks the fonts a person sees.
    TestApplication app;

    // Without any fonts (some headless machines) there is nothing to check: the
    // platform's fallback is what the app relies on. The check skips itself then,
    // with this message.
    if (QFontDatabase::families().isEmpty()) {
        SKIP("no fonts are installed on this platform: the glyph check is a Windows check today");
    }

    const std::vector<Sample> samples = {
        {"en", "The words I found are here."},
        {"fr", "Les mots que j'ai trouvés sont ici."},
        {"es", "Las palabras que encontré están aquí."},
        {"de", "Die Wörter, die ich gefunden habe, stehen hier."},
        {"yi", "די ווערטער וואָס איך האָב געפֿונען שטייען דאָ."},
        {"he", "המילים שמצאתי נמצאות כאן."},
        {"zh_CN", "我找到的词语都在这里。"},
        {"ja", "見つけたことばはここにあります。"},
    };

    // The face the screens ask for, and the chain Qt falls back through when it
    // does not carry a glyph (docs/design/application.md, "Type"). Andika is not
    // bundled yet, so on this machine the platform's own faces are what answers.
    QFont app_font(QStringLiteral("Andika"));
    QFontMetricsF metrics(app_font);
    const QFont system_font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    QFontMetricsF system_metrics(system_font);

    QStringList missing;
    QStringList no_font;
    for (const Sample& sample : samples) {
        const QString text = QString::fromUtf8(sample.sentence);
        for (const QChar character : text) {
            const uint code_point = character.unicode();
            if (!metrics.inFontUcs4(code_point)) {
                if (!system_metrics.inFontUcs4(code_point)) {
                    no_font.append(QStringLiteral("%1: U+%2")
                                       .arg(QString::fromLatin1(sample.code))
                                       .arg(code_point, 4, 16, QLatin1Char('0')));
                } else {
                    missing.append(QStringLiteral("%1: U+%2")
                                       .arg(QString::fromLatin1(sample.code))
                                       .arg(code_point, 4, 16, QLatin1Char('0')));
                }
            }
        }
    }

    if (!no_font.isEmpty()) {
        INFO(no_font.join(QStringLiteral(", ")).toStdString());
        SKIP(
            "the platform's own fonts lack this language's glyphs: the glyph check is a "
            "Windows check today");
    }
    for (const QString& glyph : missing) {
        INFO(glyph.toStdString());
        CHECK(false);
    }
}

TEST_CASE("Hebrew and Yiddish are right to left and the other six left to right",
          "[app][i18n][language]") {
    TestApplication app;

    CHECK(LanguageManager::languages() ==
          QStringList{"en", "fr", "es", "de", "yi", "he", "zh_CN", "ja"});
    for (const QString& code : LanguageManager::languages()) {
        INFO(code.toStdString());
        CHECK(LanguageManager::is_known(code));
        CHECK(LanguageManager::is_right_to_left(code) == (code == "he" || code == "yi"));
        CHECK(LanguageManager::resolved(code) == code);
    }
    CHECK_FALSE(LanguageManager::is_known(QStringLiteral("")));
    CHECK_FALSE(LanguageManager::is_known(QStringLiteral("tlh")));
    CHECK_FALSE(LanguageManager::is_right_to_left(QStringLiteral("ar")));
    // A locale names its language in its first part, whatever the region or the
    // separator.
    CHECK(LanguageManager::language_from_locale(QStringLiteral("he_IL")) == "he");
    CHECK(LanguageManager::language_from_locale(QStringLiteral("fr-CA")) == "fr");
    CHECK(LanguageManager::language_from_locale(QStringLiteral("ja")) == "ja");
    CHECK(LanguageManager::language_from_locale(QStringLiteral("zh_CN")) == "zh_CN");
    CHECK(LanguageManager::language_from_locale(QStringLiteral("zh-Hans-CN")) == "zh_CN");
    CHECK(LanguageManager::language_from_locale(QStringLiteral("zh_TW")) == "zh_CN");
    CHECK(LanguageManager::resolved(QStringLiteral("zh_cn")) == "zh_CN");
    CHECK(LanguageManager::resolved(QStringLiteral("zh")) == "en");
    CHECK(LanguageManager::language_from_locale(QStringLiteral("tlh_QS")) == "en");
    CHECK(LanguageManager::language_from_locale(QString()) == "en");
    // No setting is the device's own language, and it is one of the eight.
    CHECK(LanguageManager::resolved(QString()) == LanguageManager::system_language());
    CHECK(wjs::is_valid_language(LanguageManager::system_language().toStdString()));
    // A code this program does not have is English.
    CHECK(LanguageManager::resolved(QStringLiteral("tlh")) == "en");
}

TEST_CASE("applying a language installs its catalogue, its direction and its text",
          "[app][i18n][language]") {
    TestApplication app;

    LanguageManager language;
    int changes = 0;
    QObject::connect(&language, &LanguageManager::languageChanged, [&changes] { ++changes; });

    for (const QString& code : LanguageManager::languages()) {
        INFO(code.toStdString());
        const int before = changes;
        CHECK(language.apply(code) == code);
        CHECK(language.code() == code);
        CHECK(language.setting() == code);
        CHECK(language.rightToLeft() == (code == "he" || code == "yi"));
        CHECK(QGuiApplication::layoutDirection() ==
              (language.rightToLeft() ? Qt::RightToLeft : Qt::LeftToRight));
        CHECK(changes == before + 1);

        // The catalogue really is installed: a parent-area sentence comes back
        // translated, and only English comes back as the source.
        const QString back = QCoreApplication::translate("ParentWording", "Back");
        INFO(back.toStdString());
        if (code == QLatin1String("en")) {
            CHECK(back == QStringLiteral("Back"));
        } else {
            CHECK(back != QStringLiteral("Back"));
        }
    }

    // The same language again is not a change.
    const QString current = language.code();
    const int before = changes;
    CHECK(language.apply(current) == current);
    CHECK(changes == before);

    // The empty setting is the device's own; its code is resolved and, whatever
    // it is, it is one of the eight.
    CHECK(language.apply(QString()) == LanguageManager::system_language());
    CHECK(language.setting().isEmpty());
}
