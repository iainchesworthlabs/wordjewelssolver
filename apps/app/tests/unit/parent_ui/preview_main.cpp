// SPDX-License-Identifier: GPL-3.0-or-later
// The parent area's screens in a stub shell: to look at them, and to check them.
//
//   wjs_parent_preview                          a window, with made-up play
//   wjs_parent_preview --check [--out <dir>]    no window shown: walks the gate,
//                                               the dashboard, the settings and
//                                               the about page, checks what they
//                                               say and that QML complained of
//                                               nothing, and (with --out) saves
//                                               a picture of each
//
// The check is a test (parent.screens): it needs a platform plugin that can run
// without a display (QT_QPA_PLATFORM=offscreen) and the software renderer, which
// it asks for itself. TEST DATA only: see demo_data.hpp.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVariant>
#include <cstdio>

#include "demo_data.hpp"
#include "language_manager.hpp"
#include "parent_controller.hpp"
#include "wjs/calendar.hpp"

namespace {

int g_warnings = 0;
int g_failures = 0;

// The check's clock: time passes only when the check says so, so that a hold of
// two seconds is two seconds however long the machine takes to draw.
qint64 g_now = 1000;

void message_handler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    const QByteArray bytes = message.toLocal8Bit();
    std::fprintf(stderr, "%s\n", bytes.constData());
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        ++g_warnings;
    }
}

void check(bool condition, const QString& what) {
    if (!condition) {
        ++g_failures;
        const QByteArray bytes = what.toLocal8Bit();
        std::fprintf(stderr, "FAILED: %s\n", bytes.constData());
    }
}

void pump(int rounds = 8) {
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(15);
    }
}

// Every text a visible item shows, tags and all. A subtree named
// `skipObjectName` is left out: the About page's licence notice is quoted legal
// text, and the wording rules below are about what a parent is told, not about
// the GPL's own words.
void collect_texts(QQuickItem* item, QStringList& texts, const QString& skipObjectName) {
    if (item == nullptr || !item->isVisible()) {
        return;
    }
    if (!skipObjectName.isEmpty() && item->objectName() == skipObjectName) {
        return;
    }
    const QVariant text = item->property("text");
    if (text.typeId() == QMetaType::QString && !text.toString().isEmpty() &&
        item->metaObject()->indexOfProperty("wrapMode") >= 0) {
        texts.append(text.toString());
    } else if (text.typeId() == QMetaType::QString && !text.toString().isEmpty() &&
               item->metaObject()->indexOfProperty("checkable") >= 0) {
        texts.append(text.toString());  // a button's own label
    }
    for (QQuickItem* child : item->childItems()) {
        collect_texts(child, texts, skipObjectName);
    }
}

QStringList visible_texts(QQuickWindow* window, const QString& skipObjectName = QString()) {
    QStringList texts;
    collect_texts(window->contentItem(), texts, skipObjectName);
    return texts;
}

bool any_contains(const QStringList& texts, const QString& needle) {
    for (const QString& text : texts) {
        if (text.contains(needle)) {
            return true;
        }
    }
    return false;
}

void save(QQuickWindow* window, const QString& directory, const QString& name) {
    if (directory.isEmpty()) {
        return;
    }
    pump();
    const QImage image = window->grabWindow();
    check(!image.isNull(), name + " could not be grabbed");
    QDir().mkpath(directory);
    check(image.save(QDir(directory).filePath(name + ".png")), name + " could not be saved");
}

// Words that must never be on a screen a parent reads (docs/design/dashboard.md,
// "Wording"): an assessment, a grade, a gap, a pull.
const QStringList forbidden = {
    "weak",   "mastered", "mastery", "missed", "blind spot",  "emerging", "grade", "score",
    "streak", "behind",   "failed",  "fail",   "deficit",     "wrong",    "poor",  "struggl",
    "below",  "ahead of", "minutes", "hours",  "time played", "daily"};

void check_no_forbidden_words(const QStringList& texts, const QString& screen) {
    for (const QString& text : texts) {
        for (const QString& word : forbidden) {
            check(!text.contains(word, Qt::CaseInsensitive),
                  screen + " says \"" + word + "\": " + text);
        }
    }
}

int run_check(const QString& out) {
    const wjs::Day today = wjs::day_from_civil(2026, 10, 16);

    wjs::PhaseSet phases = wjs::app::demo::phases();
    wjs::Store store = wjs::app::demo::busy_store(today);

    wjs::app::ParentController controller;
    controller.useCalendar([] { return today; }, 1);
    controller.useClock([] { return g_now; });
    controller.useStore(&store);
    controller.usePhases(&phases);

    wjs::app::LanguageManager language;
    language.apply(QStringLiteral("en"));

    QQmlApplicationEngine engine;
    QObject::connect(&language, &wjs::app::LanguageManager::languageChanged, &engine,
                     &QQmlEngine::retranslate);
    engine.setInitialProperties({
        {QStringLiteral("controller"), QVariant::fromValue(&controller)},
        {QStringLiteral("languageManager"), QVariant::fromValue(&language)},
    });
    engine.loadFromModule("ParentPreview", "PreviewHost");
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "The stub shell did not load.\n");
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    check(window != nullptr, "the stub shell is a window");
    if (window == nullptr) {
        return 1;
    }
    window->resize(1024, 700);
    pump();

    // --- the gate ---------------------------------------------------------------
    check(!controller.unlocked(), "the gate is shut at the start");
    QMetaObject::invokeMethod(window, "openParent");
    pump();
    check(window->property("parentOpen").toBool(), "the parent area opens");

    QStringList texts = visible_texts(window);
    check(any_contains(texts, QCoreApplication::translate("ParentWording", "For grown-ups")),
          "the gate has its title");
    check(any_contains(texts, QCoreApplication::translate("ParentWording",
                                                          "Press and hold for two seconds.")),
          "the gate says what to do");
    check(!any_contains(texts, "This week"), "nothing of the data is shown at the gate");
    check(!any_contains(texts, "Sounds"), "no sound is shown at the gate");
    check_no_forbidden_words(texts, "the gate");
    save(window, out, "gate");

    const QVariantList spots = controller.gateSpots();
    check(spots.size() == 2, "the gate has two spots");
    if (spots.size() == 2) {
        const QVariantMap a = spots.at(0).toMap();
        const QVariantMap b = spots.at(1).toMap();
        // Two fingers on two spots: a moment in, half way, and all of it. Time is
        // the check's own clock, which the circles read as they draw.
        g_now = 1000;
        controller.gateTouchDown(1, a.value("x").toDouble(), a.value("y").toDouble(), g_now);
        controller.gateTouchDown(2, b.value("x").toDouble(), b.value("y").toDouble(), g_now);
        check(controller.gateHolding(), "both spots held is a hold");
        g_now = 2000;
        pump();
        check(!controller.unlocked(), "one second does not open the gate");
        check(controller.gateProgress() > 0.4 && controller.gateProgress() < 0.6,
              "and the circles are half full");
        save(window, out, "gate-holding");
        g_now = 2999;
        pump();
        check(!controller.unlocked(), "two seconds less a moment does not open the gate");
        g_now = 3000;
        pump();
        check(controller.unlocked(), "two seconds opens the gate");
        controller.gateTouchUp(1, g_now + 100);
        controller.gateTouchUp(2, g_now + 100);
    }
    pump();

    // --- the dashboard --------------------------------------------------------------
    texts = visible_texts(window);
    check(any_contains(texts, "This week"), "the dashboard has this week");
    check(any_contains(texts, "How the play is going"),
          "the records have a heading of their own, below the action");
    check(any_contains(texts, "Sounds"), "the dashboard has the sounds");
    check(any_contains(texts, "The vault"), "the dashboard has the vault");
    check(any_contains(texts, "Best"), "the dashboard has the best");
    check(any_contains(texts, "Words with <i>ai</i> have been on"),
          "the sentence of a sound that has come up");
    check(any_contains(texts, "were found.") || any_contains(texts, "was found."),
          "and how many were found");
    check(any_contains(texts, "not met yet"), "a sound that has not come up says so");
    check(any_contains(texts, "Found without a start tile:"), "the unassisted count");
    check(any_contains(texts, "few words use this sound yet"),
          "a ticked sound with few words says so");
    check(!any_contains(texts, "Nothing to show yet."), "a dashboard with play is not empty");
    check_no_forbidden_words(texts, "the dashboard");
    save(window, out, "dashboard");

    window->resize(380, 760);
    pump();
    save(window, out, "dashboard-narrow");
    window->resize(1024, 700);

    // --- the settings -------------------------------------------------------------------
    window->setProperty("page", "settings");
    pump();
    texts = visible_texts(window);
    for (const char* label :
         {"Help", "Board", "Phase", "Focus sounds", "Board size", "Timer", "Sound it out",
          "Celebration sounds", "Sound", "Vibration", "Shake the device", "Motion", "Text size",
          "Reset", "Clear boards and finds", "Clear stars and stickers"}) {
        check(any_contains(texts, label), QString("the settings have: ") + label);
    }
    check(!any_contains(texts, "School font"), "no school font is offered: none can be bundled");
    check(any_contains(texts, "A start tile") && any_contains(texts, "Two letters"),
          "the help is one of off, the start tile, or two letters");
    check(any_contains(texts, "Count up") && any_contains(texts, "Count down"),
          "the board's clock is one of off, up or down");
    check_no_forbidden_words(texts, "the settings");
    save(window, out, "settings");

    window->resize(380, 760);
    pump();
    save(window, out, "settings-narrow");
    window->resize(1024, 700);

    // A setting changes what is shown, and what is kept.
    check(controller.setSetting("help", "start"), "the help can be its middle level");
    check(store.settings().help == wjs::HelpLevel::start, "and the store keeps it");
    pump();
    check(controller.toggleFocusSound("s"), "a sound of the phase can be ticked");
    pump();
    // The board's clock: a count-down of the length the parent chose.
    check(controller.setSetting("timer", "down"), "the board's clock can count down");
    check(controller.setSetting("timerLength", 10), "and its length can be chosen");
    check(store.settings().solo_clock == wjs::SoloClock::count_down &&
              store.settings().solo_clock_minutes == 10,
          "and the store keeps both");
    check(!controller.setSetting("timer", "sideways"), "a clock that is not offered is refused");
    pump();
    save(window, out, "settings-ticked");

    // --- the about page --------------------------------------------------------------------
    window->setProperty("page", "about");
    pump();
    texts = visible_texts(window);
    check(any_contains(texts, "version"), "the about page says the version");
    check(any_contains(texts, "commit"), "and the commit the build was made from");
    check(any_contains(texts, "branch"), "and its branch");
    check(any_contains(texts, "does not use the network"), "and what is never done");
    // One privacy section, not two: the summary card is gone, and the page says
    // the privacy line once.
    {
        int summary = 0;
        for (const QString& text : texts) {
            if (text.contains("Kept on this device. No recording, no network.")) {
                ++summary;
            }
        }
        check(summary == 1, "the about page has one privacy section, not two");
    }
    check_no_forbidden_words(texts, "the about page");
    save(window, out, "about");

    // --- the licences page -------------------------------------------------------------------
    window->setProperty("page", "licences");
    pump();
    texts = visible_texts(window);
    check(any_contains(texts, "GNU General Public License"), "the licences page has the licence");
    // The full third-party notices: embedded, and shown in the page's own
    // scroll box. The resource is the one a package's application carries
    // (cmake/Notices.cmake, wjs_add_notices_resource).
    check(QFile::exists(QStringLiteral(":/notices/NOTICES.txt")),
          "the third-party notices are embedded as :/notices/NOTICES.txt");
    check(any_contains(texts, "GPL-3.0-only"),
          "the notices say the application binary is GPL-3.0 only");
    check(any_contains(texts, "Qt Quick 3D"), "and carry the Qt Quick 3D section");
    check(any_contains(texts, "ENABLE"), "and the word list's");
    check(any_contains(texts, "Catch2"), "and what is used only to test it");
    check(!any_contains(texts, "does not use the network"),
          "the licences page is the licences, not the privacy");
    check_no_forbidden_words(visible_texts(window, QStringLiteral("licenceNotices")),
                             "the licences page");
    save(window, out, "licences");

    // --- back shuts the gate -----------------------------------------------------------------
    window->setProperty("page", "dashboard");
    pump();
    controller.lock();
    pump();
    check(!controller.unlocked(), "locking shuts the gate");
    texts = visible_texts(window);
    check(!any_contains(texts, "This week"), "and the data is gone from the screen");

    if (g_warnings != 0) {
        std::fprintf(stderr, "QML warned %d times.\n", g_warnings);
    }
    return (g_failures == 0 && g_warnings == 0) ? 0 : 1;
}

// A store with nothing, for the empty screens.
int run_empty_check(const QString& out) {
    wjs::app::ParentController controller;
    wjs::Store store = wjs::app::demo::empty_store();
    controller.useStore(&store);

    wjs::app::LanguageManager language;
    language.apply(QStringLiteral("en"));

    QQmlApplicationEngine engine;
    QObject::connect(&language, &wjs::app::LanguageManager::languageChanged, &engine,
                     &QQmlEngine::retranslate);
    engine.setInitialProperties({
        {QStringLiteral("controller"), QVariant::fromValue(&controller)},
        {QStringLiteral("languageManager"), QVariant::fromValue(&language)},
    });
    engine.loadFromModule("ParentPreview", "PreviewHost");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    window->resize(1024, 700);
    QMetaObject::invokeMethod(window, "openParent");
    pump();
    controller.unlockForAssistiveTechnology();
    pump();

    const QStringList texts = visible_texts(window);
    check(any_contains(texts, "Nothing to show yet."),
          "an empty dashboard says there is nothing yet");
    check(!any_contains(texts, "This week"), "and has no week");
    check(!any_contains(texts, "Sounds"), "and no sounds");
    check(!any_contains(texts, "0 boards"), "and no nothing counted");
    check_no_forbidden_words(texts, "the empty dashboard");
    save(window, out, "dashboard-empty");

    // A store that is not saved says so, for the parent.
    check(any_contains(texts, "could not be saved"), "an unsaved store is noted");

    window->setProperty("page", "settings");
    pump();
    const QStringList settings = visible_texts(window);
    check(!any_contains(settings, "Focus sounds"),
          "without phase data there are no sounds to tick");
    check(!any_contains(settings, "Phase"), "and no phase to choose");
    check(!controller.setSetting("board", "phonics"),
          "the phonics board cannot be chosen without its words");
    check(any_contains(settings, "Phonics needs the phonics data"),
          "and the settings say why Phonics is not offered");
    check(controller.setSetting("timer", "up"), "the board's clock does not need phase data");
    save(window, out, "settings-no-phases");
    return (g_failures == 0 && g_warnings == 0) ? 0 : 1;
}

// Every language, each applied before the screen is loaded: the screens draw,
// QML warns of nothing, the catalogue is really in force (the gate's title is
// the translated one, and for the seven it is not the English), and Hebrew and
// Yiddish turn the layout around while the other six leave it as it was.
int run_language_check(const QString& out) {
    const wjs::Day today = wjs::day_from_civil(2026, 10, 16);
    QSet<QString> english;

    for (const QString& code : wjs::app::LanguageManager::languages()) {
        const int warnings_before = g_warnings;
        const int failures_before = g_failures;

        wjs::app::LanguageManager language;
        language.apply(code);
        const bool right_to_left = language.rightToLeft();
        check(language.code() == code, "the language in force is " + code);
        check(right_to_left == (code == "he" || code == "yi"),
              "Hebrew and Yiddish are right to left, the rest left to right: " + code);
        check(QGuiApplication::layoutDirection() ==
                  (right_to_left ? Qt::RightToLeft : Qt::LeftToRight),
              "the application's layout direction follows the language: " + code);

        wjs::PhaseSet phases = wjs::app::demo::phases();
        wjs::Store store = wjs::app::demo::busy_store(today);
        wjs::app::ParentController controller;
        controller.useCalendar([] { return today; }, 1);
        controller.useClock([] { return g_now; });
        controller.useStore(&store);
        controller.usePhases(&phases);

        QQmlApplicationEngine engine;
        QObject::connect(&language, &wjs::app::LanguageManager::languageChanged, &engine,
                         &QQmlEngine::retranslate);
        engine.setInitialProperties({
            {QStringLiteral("controller"), QVariant::fromValue(&controller)},
            {QStringLiteral("languageManager"), QVariant::fromValue(&language)},
        });
        engine.loadFromModule("ParentPreview", "PreviewHost");
        if (engine.rootObjects().isEmpty()) {
            check(false, "the stub shell loads in " + code);
            continue;
        }
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (window == nullptr) {
            check(false, "the stub shell is a window in " + code);
            continue;
        }
        window->resize(1024, 700);
        pump();
        QMetaObject::invokeMethod(window, "openParent");
        pump();

        // The gate: its title comes from the catalogue that is installed.
        const QString gate_title = QCoreApplication::translate("ParentWording", "For grown-ups");
        check(any_contains(visible_texts(window), gate_title),
              "the gate's title is the catalogue's for " + code);
        if (code != QLatin1String("en")) {
            check(gate_title != QLatin1String("For grown-ups"),
                  "a catalogue is really installed for " + code);
        }
        check(window->property("mirrored").toBool() == right_to_left,
              "QML's mirroring follows the language: " + code);
        const double back_x = window->property("backButtonX").toDouble();
        const double width = window->width();
        check(right_to_left ? back_x > width / 2 : back_x < width / 2,
              "the bar's way back is on the leading edge: " + code);

        QStringList texts = visible_texts(window);
        controller.unlockForAssistiveTechnology();
        pump();
        texts += visible_texts(window);

        // The settings, where the language is chosen: the names in their own
        // scripts are there whatever the screens are in.
        window->setProperty("page", "settings");
        pump();
        const QStringList settings_texts = visible_texts(window);
        for (const char* label : {"Help", "Board", "Focus sounds", "Board size", "Text size",
                                  "Language", "Clear boards and finds"}) {
            check(any_contains(settings_texts, QCoreApplication::translate("ParentWording", label)),
                  QString("the settings have the catalogue's ") + label + " in " + code);
        }
        check(any_contains(settings_texts, QStringLiteral("Français")),
              "a language names itself in its own script: " + code);
        check(any_contains(settings_texts, QStringLiteral("中文")), "and so does Chinese: " + code);
        texts += settings_texts;

        window->setProperty("page", "about");
        pump();
        texts += visible_texts(window);
        save(window, out, "languages-" + code);

        if (code == QLatin1String("en")) {
            english = QSet<QString>(texts.begin(), texts.end());

            // A language chosen while the screens are open is applied at once: the
            // catalogue is swapped, the engine retranslates and the layout turns.
            // This is the path Main.qml takes when the parent changes the setting.
            check(controller.setSetting("language", QStringLiteral("de")),
                  "a language can be chosen while the screens are open");
            pump();
            const QString german_back = QCoreApplication::translate("ParentWording", "Back");
            check(german_back == QStringLiteral("Zurück"),
                  "the German catalogue is in force at once");
            check(any_contains(visible_texts(window), german_back),
                  "the open screen retranslated at once");
            check(controller.setSetting("language", QStringLiteral("he")),
                  "and changed again to Hebrew");
            pump();
            check(window->property("mirrored").toBool(), "the layout turns at once");
            check(window->property("backButtonX").toDouble() > width / 2,
                  "and the bar's way back moves to the right");
            check(controller.setSetting("language", QStringLiteral("en")), "and back to English");
            pump();
            check(any_contains(visible_texts(window), QStringLiteral("Back")),
                  "and back to English at once");
        } else {
            int different = 0;
            for (const QString& text : texts) {
                if (!english.contains(text)) {
                    ++different;
                }
            }
            check(different > 0, "at least one string differs from English: " + code);
        }

        check(g_warnings == warnings_before, "QML warned of nothing in " + code);
        check(g_failures == failures_before, "everything held in " + code);
        if (g_warnings != warnings_before || g_failures != failures_before) {
            std::fprintf(stderr, "the parent screens failed in %s\n",
                         code.toLocal8Bit().constData());
        }
    }
    return (g_failures == 0 && g_warnings == 0) ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    QStringList arguments;
    for (int i = 0; i < argc; ++i) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }
    const bool is_check = arguments.contains(QStringLiteral("--check"));
    const bool is_languages = arguments.contains(QStringLiteral("--languages"));
    QString out;
    const qsizetype flag = arguments.indexOf(QStringLiteral("--out"));
    if (flag >= 0 && flag + 1 < arguments.size()) {
        out = arguments.at(flag + 1);
    }

    if (is_check || is_languages) {
        // No display and no GPU: the software renderer draws into memory.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qInstallMessageHandler(message_handler);
    }
    QGuiApplication application(argc, argv);

    if (is_check) {
        const int first = run_check(out);
        const int second = run_empty_check(out);
        std::fprintf(stderr, first == 0 && second == 0 ? "parent screens: ok\n"
                                                       : "parent screens: FAILED\n");
        return first != 0 || second != 0 ? 1 : 0;
    }
    if (is_languages) {
        const int result = run_language_check(out);
        std::fprintf(stderr, result == 0 ? "parent languages: ok\n" : "parent languages: FAILED\n");
        return result;
    }

    // Interactive: made-up play, as of today, in a stub shell.
    wjs::PhaseSet phases = wjs::app::demo::phases();
    wjs::Store store = wjs::app::demo::busy_store(
        static_cast<wjs::Day>(QDate::currentDate().toJulianDay() - 2440588));
    wjs::app::ParentController controller;
    controller.useStore(&store);
    controller.usePhases(&phases);

    wjs::app::LanguageManager language;
    language.apply(QString());

    QQmlApplicationEngine engine;
    QObject::connect(&language, &wjs::app::LanguageManager::languageChanged, &engine,
                     &QQmlEngine::retranslate);
    engine.setInitialProperties({
        {QStringLiteral("controller"), QVariant::fromValue(&controller)},
        {QStringLiteral("languageManager"), QVariant::fromValue(&language)},
    });
    engine.loadFromModule("ParentPreview", "PreviewHost");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return application.exec();
}
