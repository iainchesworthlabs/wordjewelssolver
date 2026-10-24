// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDate>
#include <QDebug>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QStyleHints>
#include <QTimer>
#include <QVariant>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>

#include "feedback/feedback_engine.hpp"
#include "feedback/motion_source.hpp"
#include "frame_profiler.hpp"
#include "language_manager.hpp"
#include "parent/parent_controller.hpp"
#include "qt_text.hpp"
#include "scan/assist_game.hpp"
#include "scan/assist_game_factory.hpp"
#include "solo/solo_game_factory.hpp"
#include "solo/solo_game_setup.hpp"
#include "startup_failure.hpp"
#include "wjs/calendar.hpp"
#include "wjs/platform/paths.hpp"
#include "wjs/platform/window_chrome.hpp"
#include "wjs/profiling.hpp"
#include "wjs/settings.hpp"
#include "wjs/store.hpp"
#include "wjs/version.hpp"

namespace {

// The value after `flag` in the arguments, if the program was started with one.
[[nodiscard]] std::optional<QString> value_of(const QStringList& arguments, const QString& flag) {
    const qsizetype at = arguments.indexOf(flag);
    if (at < 0 || at + 1 >= arguments.size()) {
        return std::nullopt;
    }
    return arguments.at(at + 1);
}

// The device's own calendar, the one the game and the parent area share: Qt's
// Julian day of 1970-01-01 is 2440588.
[[nodiscard]] wjs::Day device_today() {
    return static_cast<wjs::Day>(QDate::currentDate().toJulianDay() - 2440588);
}

// ---------------------------------------------------------------------------
// The smoke run (docs/profiling.md, "The smoke mode")
//
// `WordJewelsSolver --smoke [<frames>]` shows Solo Play, plays the shake cycle
// again and again, counts the frames, prints the frame-time report and leaves
// with 0. It is the workload a capture attaches to, and the frame check's
// program. The frames are measured by FrameProfiler; the scene and the screens
// are the application's own.
// ---------------------------------------------------------------------------

// How many frames a smoke run measures when the option gives no number: five
// seconds at 60 Hz, two shakes and the screens around them.
constexpr int kSmokeFrames = 300;
// How many frame intervals a smoke run leaves out before it measures: the
// window's first frames create the scene graph, the scene and its textures
// (docs/design/shake-prototype.md, "Making the view"), and are not a steady
// frame.
constexpr int kSmokeWarmupFrames = 60;
// How long a smoke run gives a profiler that was started first to attach, before
// the setup it is meant to see. In a build without Tracy there is nothing to
// wait for, and the wait is compiled to nothing.
constexpr int kProfilerWaitMs = 5000;
// How long a smoke run waits for its first frame before saying the window cannot
// render: Quick 3D needs a display, and the offscreen platform plugin does not
// give it one (docs/profiling.md, "The smoke mode").
constexpr int kSmokeFrameTimeoutMs = 20000;
// How many of Qt's QML warnings a failed load puts in the dialog: the first ones
// are the cause, the rest follow from it.
constexpr qsizetype kMaxQmlErrors = 5;
// The seed a smoke run plays when none is given, so that a capture is the same
// boards in the same order on every run.
constexpr std::uint32_t kSmokeSeed = 7919;

// The smoke mode: 0 when the program was not started with --smoke, otherwise how
// many frames to measure. `--smoke 600` measures 600; `--smoke` alone, or with
// something that is not a number after it, measures kSmokeFrames.
[[nodiscard]] int smoke_frames(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view{argv[i]} != "--smoke") {
            continue;
        }
        if (i + 1 < argc) {
            const std::string_view text{argv[i + 1]};
            int frames = 0;
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), frames);
            if (error == std::errc{} && end == text.data() + text.size() && frames > 0) {
                return frames;
            }
        }
        return kSmokeFrames;
    }
    return 0;
}

// Whether this run gives a profiler the moment it needs before the setup: a smoke
// run always does, and any run does when it is started with `--wait-for-profiler`
// or with WJS_WAIT_FOR_PROFILER set (to anything). Tracy is attached on demand and
// takes about a second to find a program, so without the wait a normal launch
// has finished app.startup and app.load_qml before a profiler could see them.
[[nodiscard]] bool profiler_wait_asked(int argc, char* argv[]) {
    if (qEnvironmentVariableIsSet("WJS_WAIT_FOR_PROFILER")) {
        return true;
    }
    for (int i = 1; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--wait-for-profiler") {
            return true;
        }
    }
    return false;
}

// Gives a profiler that was started before this program the moment it needs to
// find the client (about a second, docs/profiling.md) before the setup, so that
// app.startup, app.load_qml and the store's load are in the capture. The wait is
// over as soon as one is connected and is over the deadline otherwise, so the
// run works with no profiler too. Calling connected() starts Tracy's client, and
// so its listener, if nothing else has.
void wait_for_profiler() {
    if constexpr (wjs::profiling::enabled) {
        std::fputs("waiting up to 5 s for a profiler (Tracy build)\n", stdout);
        std::fflush(stdout);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kProfilerWaitMs);
        while (!wjs::profiling::connected() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::fputs(wjs::profiling::connected() ? "profiler connected\n"
                                               : "no profiler connected; continuing\n",
                   stdout);
        std::fflush(stdout);
    }
}

// The program's one store: the file `--store <file>` names, otherwise the
// per-user one, and a store that is never written when there is nowhere to keep
// one. The game and the parent area both work on it, so that a change made in the
// one is what the other reads.
[[nodiscard]] wjs::OpenResult open_store(const QStringList& arguments) {
    std::filesystem::path file;
    if (const auto given = value_of(arguments, QStringLiteral("--store"))) {
        file = wjs::app::to_path(*given);
    } else {
        // Qt's per-user application data directory, which already exists for the
        // cache and follows the app's organisation name on every platform. The
        // old path was the bare LOCALAPPDATA: a second, unrelated folder, and
        // on a machine where that folder could not be made the store fell back
        // to memory and the parent was told progress could not be saved.
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!base.isEmpty()) {
            file = wjs::app::to_path(base) / std::string{wjs::store_file_name};
        } else if (const std::filesystem::path fallback = wjs::platform::user_data_directory();
                   !fallback.empty()) {
            file = wjs::default_store_path(fallback);
        }
    }
    if (file.empty()) {
        return {wjs::Store::in_memory(), wjs::LoadStatus::created, {}};
    }
    return wjs::Store::open(file);
}

// What the real game is to be made from: the English word list and the standard
// board's dice, both defaulting to the directories the build places beside the
// executable, and the phases of a data directory when the program is told where
// one is (`--phase-directory <dir>`, with `--phase <name>` to go with it). The
// repository has no phase (the files have to be written and reviewed by a person
// qualified to do it), so without the directory the program plays the standard
// board, the game's default. `repeatable` gives the run a fixed seed when none
// is asked for: the smoke's boards are the same on every run.
[[nodiscard]] wjs::app::SoloGameOptions real_game_options(const QStringList& arguments,
                                                          bool repeatable) {
    wjs::app::SoloGameOptions options;
    if (const auto directory = value_of(arguments, QStringLiteral("--phase-directory"))) {
        options.phase_directory = wjs::app::to_path(*directory);
    }
    if (options.phase_directory.empty()) {
        if (arguments.contains(QStringLiteral("--phase"))) {
            qWarning() << "--phase applies only with --phase-directory; ignored.";
        }
    } else if (const auto phase = value_of(arguments, QStringLiteral("--phase"))) {
        options.phase = phase->toStdString();
    }
    if (const auto dictionary = value_of(arguments, QStringLiteral("--dictionary"))) {
        options.dictionary = wjs::app::to_path(*dictionary);
    } else {
        options.dictionary =
            wjs::platform::executable_path().parent_path() / "dictionaries" / "en.txt";
    }
    options.dice_directory = wjs::platform::executable_path().parent_path() / "dice";
    if (const auto seed = value_of(arguments, QStringLiteral("--seed"))) {
        bool ok = false;
        const std::uint32_t number = seed->toUInt(&ok);
        if (ok) {
            options.seed = number;
        } else {
            qWarning() << "--seed wants a whole number; a random one is used.";
        }
    } else if (repeatable) {
        options.seed = kSmokeSeed;
    }
    options.today = device_today;
    return options;
}

// What to tell the person about the store's file, or null when it was read (or made)
// as it should be.
[[nodiscard]] const char* store_note(wjs::LoadStatus status) noexcept {
    switch (status) {
        case wjs::LoadStatus::created:
        case wjs::LoadStatus::loaded:
        case wjs::LoadStatus::migrated:
            return nullptr;
        case wjs::LoadStatus::unreadable:
            return "The store's file was damaged: it is set aside and a new one is started.";
        case wjs::LoadStatus::newer_version:
            return "The store's file was written by a later version: it is left alone and "
                   "nothing is saved this run.";
        case wjs::LoadStatus::cannot_read:
            return "The store's file could not be read: it is left alone and nothing is saved "
                   "this run.";
    }
    return nullptr;
}

// Everything the window is made from, before QML: the store the game and the
// parent area share, the options the game is made with, and the game's setup.
// One object so that `app.startup` times exactly the making of it, and not the
// event loop after it. It is made on the heap and never moved: the game's setup
// holds the store by reference, so the store has to be at its final address
// before the setup is opened.
struct Startup {
    Startup(wjs::OpenResult opened_result, wjs::app::SoloGameOptions game_options)
        : opened(std::move(opened_result)), options(std::move(game_options)) {}

    wjs::OpenResult opened;
    wjs::app::SoloGameOptions options;
    std::unique_ptr<wjs::app::SoloGameSetup> solo_setup;
};

// Opens the store and loads what the game is played from. The sentence that says
// why, when the game cannot be made.
[[nodiscard]] std::expected<std::unique_ptr<Startup>, QString> start(const QStringList& arguments,
                                                                     bool repeatable) {
    auto zone = wjs::profiling::zone<"app.startup">();

    auto made =
        std::make_unique<Startup>(open_store(arguments), real_game_options(arguments, repeatable));
    if (const char* note = store_note(made->opened.status)) {
        qWarning() << note;
    }
    auto setup = wjs::app::SoloGameSetup::open(made->options, made->opened.store);
    if (!setup) {
        return std::unexpected(QString::fromStdString(setup.error()));
    }
    made->solo_setup = *std::move(setup);
    return made;
}

}  // namespace

int main(int argc, char* argv[]) {
    const int smoke = smoke_frames(argc, argv);
    if (smoke > 0 || profiler_wait_asked(argc, argv)) {
        // Before the clock starts and before anything is loaded, so that a
        // capture has the start-up zones and no number contains the wait.
        wait_for_profiler();
    }
    wjs::app::FrameProfiler::start_process_clock();
    wjs::profiling::thread_name("main");

    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("WordJewelsSolver"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Word Jewels Solver"));
    QGuiApplication::setOrganizationName(QStringLiteral("Iain Chesworth Labs"));
    QGuiApplication::setApplicationVersion(QString::fromUtf8(
        wjs::project_version.data(), static_cast<qsizetype>(wjs::project_version.size())));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    // The house faces and Andika are compiled into the program (apps/app/src/
    // CMakeLists.txt). Register them before the engine loads so every QML
    // `font.family` resolves to the face the design names rather than a
    // platform fallback.
    {
        // Six font files read and parsed before the first screen: a cost every
        // launch pays, so it has a name in the profile.
        auto zone = wjs::profiling::zone<"app.fonts">();
        for (const char* font : {":/fonts/Archivo-Regular.ttf", ":/fonts/Archivo-SemiBold.ttf",
                                 ":/fonts/Archivo-ExtraBold.ttf", ":/fonts/IBMPlexMono-Medium.ttf",
                                 ":/fonts/Andika-Regular.ttf", ":/fonts/Andika-Bold.ttf"}) {
            const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(font));
            if (id < 0) {
                qWarning() << "The bundled font" << font << "could not be loaded.";
            }
        }
    }
    const QStringList arguments = application.arguments();

    // Everything the engine points at is declared before it, which is gone first:
    // the store, which the game and the parent area share, then the game's word
    // list, dice and (given one) phases, and the parent area's controller.
    auto started = start(arguments, smoke > 0);
    if (!started) {
        return wjs::app::fail_startup(wjs::app::ExitStatus::setup_failed, started.error());
    }
    Startup& setup = **started;
    wjs::Store& store = setup.opened.store;
    if (!setup.options.phase_directory.empty() && !setup.solo_setup->reviewed()) {
        qWarning() << "The phase files are test data, not a reviewed phase; they are for a"
                      " run on this machine and are not content for a child.";
    }
    const wjs::app::ScopedSoloGameMaker solo_maker(
        [game_setup = setup.solo_setup.get()] { return game_setup->make(); });

    // Mode A's rounds, over the same dictionary and the same store: the
    // assistant's board is recorded where Solo Play's is, so the dashboard
    // counts both. The maker is installed before the engine loads, and the
    // engine is gone before the setup and the store it points to (below).
    const wjs::app::ScopedAssistGameMaker assist_maker(
        [game_setup = setup.solo_setup.get(), &store](wjs::Board board) {
            return std::make_unique<wjs::app::AssistGame>(
                std::move(board), game_setup->dictionary(), store, [] { return device_today(); });
        });

    // The parent area's controller, on the same store, the same calendar and the
    // phases the game plays (none without a phase directory).
    wjs::app::ParentController parent;
    parent.useCalendar(device_today, 0);
    parent.useStore(&store, setup.opened.status);
    parent.usePhases(!setup.solo_setup->phases().phases().empty() ? &setup.solo_setup->phases()
                                                                  : nullptr);

    // The interface's language, in force before the engine loads: the store's
    // setting, which is the device's own language when it is empty (and English
    // when the device's is not one of the eight). The catalogue and the layout
    // direction are installed here, so the first screen parsed is already
    // translated and mirrored; a change in the parent area goes through the same
    // object, and the engine's bindings follow `languageChanged`.
    wjs::app::LanguageManager language;
    language.apply(wjs::app::to_qstring(store.settings().language));

    // The feedback engines and the device's motion, made before the engine that
    // points at them and gone after it: Main.qml binds the parent's settings to
    // the feedback engine, and hands the motion source to Solo Play. They do
    // nothing until a cue arrives or the box is shown; a build without Qt
    // Multimedia, Sensors or TextToSpeech has their silent variants behind
    // these same objects.
    wjs::app::FeedbackEngine feedback;
    // The phase the phonics board is played in, for sounding a found word out.
    feedback.usePhase(setup.solo_setup->phase());
    if (smoke > 0) {
        // A smoke run measures frames, and plays nothing: a sound is noise in the
        // numbers, and on Windows Qt Multimedia's audio-device thread sends an event
        // to an object of the main thread, which Qt's debug libraries assert on
        // ("Cannot send events to objects owned by a different thread") about one
        // run in three, so that a Debug smoke run on a machine with a sound device
        // aborts. The cues are still counted and reported; they reach no speaker
        // and no voice.
        feedback.soundEngine().setOutput([](const QString&, double) {});
        feedback.speech().setOutput([](const QString&, double) {});
    }
    wjs::app::MotionSource motion;

    QQmlApplicationEngine engine;
    QObject::connect(&language, &wjs::app::LanguageManager::languageChanged, &engine,
                     &QQmlEngine::retranslate);
    // Qt reports a QML file that will not load as warnings, which are all the
    // person is told; keep the first few to say in the dialog.
    QStringList qml_errors;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
                     [&qml_errors](const QList<QQmlError>& errors) {
                         for (const QQmlError& error : errors) {
                             if (qml_errors.size() < kMaxQmlErrors) {
                                 qml_errors.append(error.toString());
                             }
                         }
                     });
    {
        auto zone = wjs::profiling::zone<"app.load_qml">();
        engine.setInitialProperties({
            {QStringLiteral("parentController"), QVariant::fromValue(&parent)},
            {QStringLiteral("languageManager"), QVariant::fromValue(&language)},
            {QStringLiteral("feedback"), QVariant::fromValue(&feedback)},
            {QStringLiteral("motion"), QVariant::fromValue(&motion)},
            // The smoke run's switch: Main.qml shows Solo Play and shakes the
            // box, and the frames are counted below, in C++.
            {QStringLiteral("smoke"), smoke > 0},
        });
        engine.loadFromModule("WordJewelsSolver", "Main");
    }
    if (engine.rootObjects().isEmpty()) {
        return wjs::app::fail_startup(
            wjs::app::ExitStatus::window_failed,
            QStringLiteral("The window could not be loaded.\n\n") + qml_errors.join(u'\n'));
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        return wjs::app::fail_startup(wjs::app::ExitStatus::window_failed,
                                      QStringLiteral("The QML root object is not a window; the"
                                                     " frame clock has nothing to attach to."));
    }
    wjs::platform::set_window_chrome(
        reinterpret_cast<void*>(window->winId()),
        QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);

    // The frame clock is attached after the QML is loaded (the scene's creation
    // is in app.load_qml's span) and before the event loop, so the first frame
    // is the first one it sees.
    wjs::app::FrameProfiler profiler(window);
    if (smoke > 0) {
        profiler.set_target(smoke, kSmokeWarmupFrames);
        QObject::connect(
            &profiler, &wjs::app::FrameProfiler::finished, &application, [&application, &profiler] {
                const QByteArray report = profiler.report_text().toUtf8();
                std::fwrite(report.constData(), 1, static_cast<std::size_t>(report.size()), stdout);
                std::fflush(stdout);
                application.quit();
            });
        // A window that cannot present a frame (Quick 3D with no display) is
        // said, not left hanging, and not faked: nothing is reported.
        QTimer::singleShot(kSmokeFrameTimeoutMs, &application, [&application, &profiler] {
            if (profiler.first_frame_seen()) {
                return;
            }
            qCritical().noquote() << "smoke: no frame was presented in"
                                  << kSmokeFrameTimeoutMs / 1000
                                  << "s. Qt Quick 3D needs a display; the offscreen platform"
                                     " plugin does not provide one (docs/profiling.md, \"The"
                                     " smoke mode\").";
            application.exit(wjs::app::exit_code(wjs::app::ExitStatus::no_frame));
        });
    }
    return application.exec();
}
