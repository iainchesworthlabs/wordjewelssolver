// SPDX-License-Identifier: GPL-3.0-or-later
#include "language_manager.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLocale>
#include <QTranslator>
#include <algorithm>
#include <string_view>

#include "wjs/profiling.hpp"
#include "wjs/settings.hpp"

// The catalogues are a resource of the wjs_i18n library. A resource is linked
// out of a static library unless something refers to it, and this is that
// reference. It is made at global scope because Q_INIT_RESOURCE declares its
// extern function where it is expanded: inside wjs::app it would declare
// wjs::app::qInitResources_..., while the generated object defines the global
// one, and clang-cl + lld-link reject that link.
static void init_translations_resource() {
    Q_INIT_RESOURCE(wjs_translations);
}

namespace wjs::app {
namespace {

// A code from the library's list as Qt text, so the eight are named once
// (wjs/settings.hpp) and the store and the catalogue cannot drift apart.
[[nodiscard]] QString code_text(std::string_view code) {
    return QString::fromLatin1(code.data(), static_cast<qsizetype>(code.size()));
}

// The code in the library's list that `code` names, whatever its case, or empty.
[[nodiscard]] QString canonical(const QString& code) {
    return code_text(wjs::canonical_language(code.toUtf8().toStdString()));
}

}  // namespace

LanguageManager::LanguageManager(QObject* parent) : QObject(parent) {
    // The catalogues are a resource of this library. A resource is linked out of
    // a static library unless something refers to it, and the helper is that
    // reference (made at global scope; see the note above).
    ::init_translations_resource();
}

LanguageManager::~LanguageManager() {
    if (translator_ && QCoreApplication::instance() != nullptr) {
        QCoreApplication::removeTranslator(translator_.get());
    }
}

QStringList LanguageManager::languages() {
    QStringList codes;
    for (const std::string_view code : wjs::languages) {
        codes.append(code_text(code));
    }
    return codes;
}

bool LanguageManager::is_known(const QString& code) {
    return !canonical(code).isEmpty();
}

bool LanguageManager::is_right_to_left(const QString& code) {
    // Hebrew and Yiddish, and nothing else (docs/design/application.md, "Languages").
    return code == QLatin1String("he") || code == QLatin1String("yi");
}

QString LanguageManager::language_from_locale(const QString& locale_name) {
    QString name = locale_name;
    name.replace(QLatin1Char('-'), QLatin1Char('_'));
    // A locale that is one of the codes ("zh_CN") is that code. Any other
    // region of a language is the code of the language ("zh_TW" has no
    // catalogue of its own, so it is the Chinese there is; "fr_CA" is French).
    const QString exact = canonical(name);
    if (!exact.isEmpty()) {
        return exact;
    }
    const QString language = name.section(QLatin1Char('_'), 0, 0).toLower();
    for (const std::string_view known : wjs::languages) {
        const QString code = code_text(known);
        if (code.section(QLatin1Char('_'), 0, 0) == language) {
            return code;
        }
    }
    return QStringLiteral("en");
}

QString LanguageManager::system_language() {
    return language_from_locale(QLocale::system().name());
}

QString LanguageManager::resolved(const QString& setting) {
    if (setting.isEmpty()) {
        return system_language();
    }
    const QString code = canonical(setting);
    return code.isEmpty() ? QStringLiteral("en") : code;
}

bool LanguageManager::rightToLeft() const {
    return is_right_to_left(code_);
}

QString LanguageManager::apply(const QString& setting) {
    const QString code = resolved(setting);
    // The setting the screens read back: "" for the device's own. An unknown
    // code asks for a language this program does not have, so it is English and
    // the setting is not one of the eight either: it is kept as the system's.
    const QString stored = canonical(setting);

    if (code == code_ && translator_) {
        if (stored != setting_) {
            setting_ = stored;
            emit languageChanged();
        }
        return code_;
    }

    install(code);
    code_ = code;
    setting_ = stored;

    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr) {
        QGuiApplication::setLayoutDirection(rightToLeft() ? Qt::RightToLeft : Qt::LeftToRight);
    }
    emit languageChanged();
    return code_;
}

void LanguageManager::install(const QString& code) {
    // The catalogue's read and install, on the GUI thread: it is a resource, so
    // it is quick, and it is timed so a trace shows when a language change pays
    // it (once per change, not per frame).
    auto zone = wjs::profiling::zone<"data.load_catalogue">();
    if (translator_) {
        QCoreApplication::removeTranslator(translator_.get());
        translator_.reset();
    }
    auto translator = std::make_unique<QTranslator>();
    // A catalogue that is not there is not fatal: the source strings are
    // English and the catalogue check and the language smoke would say so.
    if (!translator->load(QStringLiteral(":/i18n/wjs_%1.qm").arg(code))) {
        qWarning("LanguageManager: no catalogue for '%s'; the source wording is used.",
                 qPrintable(code));
    }
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::installTranslator(translator.get());
    }
    translator_ = std::move(translator);
}

}  // namespace wjs::app
