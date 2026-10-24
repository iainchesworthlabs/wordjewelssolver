// SPDX-License-Identifier: GPL-3.0-or-later
// The Family Assistant's screens in a stub shell: a program to look at them with,
// and, with --check, a test that walks `scan.ready`, the typed board, the check
// screen and `assist.playing` without a display, checks what they say and that
// QML complained of nothing. With --camera, the same shell with a CameraSource
// over the fake backend and one synthetic board: the live view, Capture and the
// check screen are walked without a device. A live camera is never started.
//
// The application's real shell (apps/app/qml/Main.qml) is not used, because it
// carries the 3D Solo Play scene and a window that needs a display. The screens
// here are the application's own files and the models are the application's own
// classes: this is test support, not a copy.
//
// TEST DATA. The words and the board are made up for the check; none of it is
// content for a child.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFontDatabase>
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
#include <QVariantMap>
#include <cstdio>
#include <memory>
#include <utility>

#include "../camera_test_frame.hpp"
#include "../fake_camera.hpp"
#include "assist_game.hpp"
#include "assist_model.hpp"
#include "camera_source.hpp"
#include "language_manager.hpp"
#include "scan_model.hpp"
#include "wjs/board.hpp"
#include "wjs/calendar.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/scan/reading.hpp"
#include "wjs/store.hpp"

namespace {

int g_warnings = 0;
int g_failures = 0;

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

// Every text a visible item shows. A button's label is a Text child, so one walk
// collects both.
void collect_texts(QQuickItem* item, QStringList& texts) {
    if (item == nullptr || !item->isVisible()) {
        return;
    }
    const QVariant text = item->property("text");
    if (text.typeId() == QMetaType::QString && !text.toString().isEmpty() &&
        item->metaObject()->indexOfProperty("wrapMode") >= 0) {
        texts.append(text.toString());
    }
    for (QQuickItem* child : item->childItems()) {
        collect_texts(child, texts);
    }
}

QStringList visible_texts(QQuickWindow* window) {
    QStringList texts;
    collect_texts(window->contentItem(), texts);
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

// A check on what a screen says; a miss prints everything the screen does say,
// so a changed sentence is a sentence to read and not a hunt.
void check_says(const QStringList& texts, const QString& needle, const QString& what) {
    if (any_contains(texts, needle)) {
        return;
    }
    ++g_failures;
    std::fprintf(stderr, "FAILED: %s\n  wanted: %s\n  shown:\n", what.toLocal8Bit().constData(),
                 needle.toLocal8Bit().constData());
    for (const QString& text : texts) {
        std::fprintf(stderr, "    %s\n", text.toLocal8Bit().constData());
    }
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

int run_check(const QString& out) {
    const wjs::Day today = wjs::day_from_civil(2026, 10, 19);

    // A board the check walks through: SAT across the top, PIN below it, X for
    // the rest.
    wjs::Dictionary dictionary;
    for (const char* word : {"SAT", "SIT", "PIN", "NIP", "TAP"}) {
        static_cast<void>(dictionary.insert(word));
    }
    wjs::Store store = wjs::Store::in_memory();

    wjs::app::LanguageManager language;
    language.apply(QStringLiteral("en"));

    wjs::app::ScanModel scan;
    wjs::app::AssistModel assist([&dictionary, &store](wjs::Board board) {
        return std::make_unique<wjs::app::AssistGame>(std::move(board), dictionary, store,
                                                      [] { return today; });
    });

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("scan"), QVariant::fromValue(&scan)},
        {QStringLiteral("assist"), QVariant::fromValue(&assist)},
        {QStringLiteral("languageManager"), QVariant::fromValue(&language)},
    });
    engine.loadFromModule("WordJewelsSolver", "PreviewHost");
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "The stub shell did not load.\n");
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    check(window != nullptr, "the stub shell is a window");
    if (window == nullptr) {
        return 1;
    }
    window->resize(1100, 700);
    pump();

    // --- scan.ready -----------------------------------------------------------------
    check(scan.state() == "ready", "the scan screen opens on ready");
    QStringList texts = visible_texts(window);
    check_says(texts, "Type in your board", "the typed board is offered");
    check_says(texts, "No camera was found", "the camera's absence is said");
    check_says(texts, "Try camera again", "the camera can be retried");
    check(!any_contains(texts, "Capture"), "there is no Capture without a camera");
    save(window, out, "scan-ready");

    // --- type in a board: scan.check -------------------------------------------------
    scan.typeBoard();
    pump();
    check(scan.state() == "check", "the typed board opens the check screen");
    texts = visible_texts(window);
    check_says(texts, "Is this your board?", "the check screen asks its question");
    check_says(texts, "Start over", "Start over is there for the typed board");
    check_says(texts, "Start playing", "Start playing is there");
    check(!any_contains(texts, "A ringed tile may be wrong"),
          "a typed board rings nothing, so there is no doubt to explain");
    save(window, out, "scan-check-empty");

    // Every tile, as the picker would leave them.
    const QStringList letters = {"S", "A", "T", "X", "P", "I", "N", "X",
                                 "X", "X", "X", "X", "X", "X", "X", "X"};
    for (int index = 0; index < letters.size(); ++index) {
        check(scan.setTile(index, letters.at(index)), "a tile can be typed");
    }
    pump();
    check(scan.complete(), "the filled board is complete");
    save(window, out, "scan-check-filled");

    // --- "Start playing": assist.playing ---------------------------------------------
    QVariant played;
    check(QMetaObject::invokeMethod(window, "play", Q_RETURN_ARG(QVariant, played)),
          "the shell has its play function");
    pump();
    check(played.toBool(), "the check screen's Yes, play confirmed the board");
    check(window->property("page").toString() == "assist", "the assistant's screen is shown");
    check(assist.active(), "the assistant has the board");
    check(assist.rows() == 4 && assist.columns() == 4, "the board is the 4x4 that was typed");
    check(assist.tiles().at(0) == "S" && assist.tiles().at(6) == "N",
          "the board's letters are the confirmed ones");
    check(assist.history().isEmpty(), "a new device's history is empty until a board is left");

    texts = visible_texts(window);
    check_says(texts, "Words I found", "the panel has the tray's title");
    check_says(texts, "No words yet", "a new tray is empty, not counted");
    check_says(texts, "Show me a start", "the one help is there");
    check_says(texts, "New board", "a new round is offered");
    save(window, out, "assist-playing");

    // --- "Show me a start" lights a tile, and never says the word ---------------------
    assist.askHelp();
    pump();
    // SAT and SIT both start at the top-left tile; SAT sorts first, so that is
    // the tile the help lights.
    check(assist.litTile() == 0, "the start tile of the shortest word is lit");
    save(window, out, "assist-start");

    // --- a word found on the replica is a jewel in the tray ---------------------------
    assist.tapTile(0);
    assist.tapTile(1);
    assist.tapTile(2);
    assist.done();
    pump();
    check(assist.found().size() == 1, "SAT is found once");
    check(assist.found().at(0).toMap().value("word").toString() == "sat",
          "the tray holds the word in lower case");
    check(assist.litTile() == -1, "the shown start is gone once its word is found");
    texts = visible_texts(window);
    check_says(texts, "sat", "the tray shows the word");
    check(!any_contains(texts, "No words yet"), "the empty tray's sentence is gone");
    save(window, out, "assist-found");

    // --- leaving records the board, once ----------------------------------------------
    assist.leave();
    assist.leave();
    check(store.data().boards.size() == 1, "the board reaches the store once");
    check(store.data().boards.front().size == 4, "as the 4x4 it was");
    check(store.data().boards.front().viable_total == 5,
          "with the count of the words that counted on it");
    check(store.data().boards.front().found == 1, "and its one find");
    check(assist.history().size() == 1, "the board left is in the history");
    check(assist.history().at(0).toMap().value("rows").toInt() == 4, "as the 4x4 it was");

    // --- New board: a new scan, ready, from the start ---------------------------------
    check(QMetaObject::invokeMethod(window, "newBoard"), "the shell has its new-board function");
    pump();
    check(window->property("page").toString() == "scan", "New board goes back to the scan");
    check(scan.state() == "ready", "and the camera's ready screen is new");
    check(scan.rows() == 0, "with no draft left over");

    if (g_warnings != 0) {
        std::fprintf(stderr, "QML warned %d times.\n", g_warnings);
    }
    return (g_failures == 0 && g_warnings == 0) ? 0 : 1;
}

// Mode A with a camera source that is not a device: the fake backend hands over
// one board drawn by the scanner's synthetic corpus. The live view is a real
// VideoOutput of the build's Qt Multimedia, the Capture is the application's
// button, and the frame goes through the same reader the program uses.
int run_camera_check(const QString& out) {
    const wjs::Day today = wjs::day_from_civil(2026, 10, 19);

    wjs::Dictionary dictionary;
    for (const char* word : {"SAT", "SIT", "PIN", "NIP", "TAP"}) {
        static_cast<void>(dictionary.insert(word));
    }
    wjs::Store store = wjs::Store::in_memory();

    wjs::app::LanguageManager language;
    language.apply(QStringLiteral("en"));

    // The fake backend is a camera with no device: it is available, it starts,
    // and it holds one frame. Set on the model the way Main.qml sets the camera
    // it makes. TEST DATA: a synthetic frame, never a photograph.
    auto backend = std::make_unique<wjs::app::test::FakeCameraBackend>();
    backend->setAvailable(true);
    backend->setProblem(QString());
    backend->setFrame(wjs::app::test::synthetic_board_frame());
    wjs::app::CameraSource camera(std::move(backend));

    wjs::app::ScanModel scan;
    scan.setCameraSource(&camera);
    wjs::app::AssistModel assist([&dictionary, &store](wjs::Board board) {
        return std::make_unique<wjs::app::AssistGame>(std::move(board), dictionary, store,
                                                      [] { return today; });
    });

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("scan"), QVariant::fromValue(&scan)},
        {QStringLiteral("assist"), QVariant::fromValue(&assist)},
        {QStringLiteral("languageManager"), QVariant::fromValue(&language)},
    });
    engine.loadFromModule("WordJewelsSolver", "PreviewHost");
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "The stub shell did not load.\n");
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    check(window != nullptr, "the stub shell is a window");
    if (window == nullptr) {
        return 1;
    }
    window->resize(1100, 700);
    pump();

    // --- scan.ready with the camera running -----------------------------------------
    check(scan.state() == "ready", "the scan screen opens on ready");
    check(scan.cameraAvailable(), "the camera source started");
    check(scan.cameraCapturing(), "the camera is running while ready is shown");
    check(scan.cameraProblem().isEmpty(), "a running camera has no problem");
    check(!scan.cameraSource()->devices().isEmpty(), "the source lists its device");
    // The loader creates the preview a frame after the property changes.
    for (int i = 0; i < 40 && !window->property("previewShown").toBool(); ++i) {
        pump(1);
    }
    check(window->property("previewShown").toBool(), "the live view is shown");
    QStringList texts = visible_texts(window);
    check_says(texts, "Capture", "Capture is offered with a camera");
    check_says(texts, "Type in your board", "the typed board stays beside it");
    check(!any_contains(texts, "There is no camera"), "the camera's absence is not said");
    save(window, out, "camera-preview");

    // --- Capture: the frame arrives, is read, and the check screen opens -----------
    scan.capture();
    // The read runs on the scanner's own thread: the window keeps drawing while it
    // does, and the answer comes back through the event loop.
    for (int i = 0; i < 400 && scan.state() == "reading"; ++i) {
        pump(1);
    }
    pump();
    check(scan.state() == "check", "Capture opens the check screen");
    check(scan.rows() == 4 && scan.columns() == 4, "the synthetic board is the 4x4 it is");
    check(scan.letters().size() == 16, "every tile has a reading");
    check(!scan.cameraCapturing(), "the camera stops as soon as the frame is in hand");
    check(!camera.running(), "the source is stopped after the read");
    texts = visible_texts(window);
    check_says(texts, "Is this your board?", "the check screen asks its question");
    check_says(texts, "Scan again", "Scan again is there for the camera board");
    check_says(texts, "Start playing", "Start playing is there");
    check(!any_contains(texts, "Capture"), "the ready screen's Capture is gone");
    save(window, out, "camera-check");

    // A frame that cannot be read returns to ready with the camera on again: the
    // fake has given its one frame, so the next capture waits and is answered by
    // the model's patience. The check is not reached, and nothing is faked.
    check(QMetaObject::invokeMethod(window, "newBoard"), "the shell has its new-board function");
    pump(20);
    check(scan.state() == "ready", "New board returns to the ready screen");
    check(scan.cameraCapturing(), "and starts the camera again");

    if (g_warnings != 0) {
        std::fprintf(stderr, "QML warned %d times.\n", g_warnings);
    }
    return (g_failures == 0 && g_warnings == 0) ? 0 : 1;
}

// Every language of the Family Assistant's screens, applied before they load:
// they draw, QML warns of nothing, the catalogue is really in force (the ready
// screen's title is the translated one, and for the seven it is not the English),
// and the split and the bar follow the direction while the board does not.
int run_language_check(const QString& out) {
    const wjs::Day today = wjs::day_from_civil(2026, 10, 19);
    QSet<QString> english;

    for (const QString& code : wjs::app::LanguageManager::languages()) {
        const int warnings_before = g_warnings;

        wjs::app::LanguageManager language;
        language.apply(code);
        const bool right_to_left = language.rightToLeft();
        check(language.code() == code, "the language in force is " + code);
        check(QGuiApplication::layoutDirection() ==
                  (right_to_left ? Qt::RightToLeft : Qt::LeftToRight),
              "the application's layout direction follows the language: " + code);

        wjs::Dictionary dictionary;
        for (const char* word : {"SAT", "SIT", "PIN", "NIP", "TAP"}) {
            static_cast<void>(dictionary.insert(word));
        }
        wjs::Store store = wjs::Store::in_memory();
        wjs::app::ScanModel scan;
        wjs::app::AssistModel assist([&dictionary, &store](wjs::Board board) {
            return std::make_unique<wjs::app::AssistGame>(std::move(board), dictionary, store,
                                                          [] { return today; });
        });

        QQmlApplicationEngine engine;
        QObject::connect(&language, &wjs::app::LanguageManager::languageChanged, &engine,
                         &QQmlEngine::retranslate);
        engine.setInitialProperties({
            {QStringLiteral("scan"), QVariant::fromValue(&scan)},
            {QStringLiteral("assist"), QVariant::fromValue(&assist)},
            {QStringLiteral("languageManager"), QVariant::fromValue(&language)},
        });
        engine.loadFromModule("WordJewelsSolver", "PreviewHost");
        if (engine.rootObjects().isEmpty()) {
            check(false, "the stub shell loads in " + code);
            continue;
        }
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (window == nullptr) {
            check(false, "the stub shell is a window in " + code);
            continue;
        }
        window->resize(1100, 700);
        pump();

        // --- scan.ready ------------------------------------------------------
        QStringList texts = visible_texts(window);
        const QString ready_title =
            QCoreApplication::translate("ScanWording", "Put the board in the frame");
        check(any_contains(texts, ready_title), "the ready title is the catalogue's: " + code);
        if (code != QLatin1String("en")) {
            check(ready_title != QLatin1String("Put the board in the frame"),
                  "a catalogue is really installed for " + code);
        }
        check(window->property("mirrored").toBool() == right_to_left,
              "QML's mirroring follows the language: " + code);
        const double scan_back = window->property("backButtonX").toDouble();
        const double width = window->width();
        check(right_to_left ? scan_back > width / 2 : scan_back < width / 2,
              "the scan bar's way back is on the leading edge: " + code);
        save(window, out, "languages-scan-" + code);

        // --- scan.check ------------------------------------------------------
        scan.typeBoard();
        pump();
        const QString check_title =
            QCoreApplication::translate("ScanWording", "Is this your board?");
        const QString start_playing = QCoreApplication::translate("ScanWording", "Start playing");
        texts = visible_texts(window);
        check(any_contains(texts, check_title), "the check screen asks its question: " + code);
        check(any_contains(texts, start_playing), "Start playing is on the check screen: " + code);
        const double yes_play_x = window->property("checkYesPlayX").toDouble();
        check(right_to_left ? yes_play_x < width / 2 : yes_play_x > width / 2,
              "the check screen's Yes, play is on the trailing side: " + code);

        const QStringList letters = {"S", "A", "T", "X", "P", "I", "N", "X",
                                     "X", "X", "X", "X", "X", "X", "X", "X"};
        for (int index = 0; index < letters.size(); ++index) {
            check(scan.setTile(index, letters.at(index)), "a tile can be typed: " + code);
        }
        pump();
        QVariant played;
        check(QMetaObject::invokeMethod(window, "play", Q_RETURN_ARG(QVariant, played)),
              "the shell has its play function: " + code);
        pump();
        check(played.toBool(), "Yes, play confirmed the board: " + code);

        // --- assist.playing ---------------------------------------------------
        const QString words_found = QCoreApplication::translate("ScanWording", "Words I found");
        texts = visible_texts(window);
        check(any_contains(texts, words_found), "the panel's tray is translated: " + code);
        const double panel_x = window->property("panelX").toDouble();
        check(right_to_left ? panel_x < width / 2 : panel_x > width / 2,
              "the panel takes the trailing third of the split: " + code);
        save(window, out, "languages-assist-" + code);

        if (code == QLatin1String("en")) {
            english = QSet<QString>(texts.begin(), texts.end());
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
        if (g_warnings != warnings_before) {
            std::fprintf(stderr, "the scan screens warned in %s\n", code.toLocal8Bit().constData());
        }
    }
    return (g_failures == 0 && g_warnings == 0) ? 0 : 1;
}

// The first item under `root` that has every one of these properties: how the
// capture mode reaches a screen's inner state (the check screen's `picked`, the
// history sheet's `open`) without the screens exposing anything for it.
QQuickItem* find_with(QQuickItem* root, const QStringList& properties) {
    if (root == nullptr) {
        return nullptr;
    }
    bool all = true;
    for (const QString& name : properties) {
        if (root->metaObject()->indexOfProperty(name.toLatin1().constData()) < 0) {
            all = false;
            break;
        }
    }
    if (all) {
        return root;
    }
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = find_with(child, properties)) {
            return found;
        }
    }
    return nullptr;
}

// What the picture is of: a window size, a colour scheme and a text scale.
struct ShotSetup {
    const char* tag;
    int width;
    int height;
    const char* scheme;
    double scale;
};

// Every Mode A screen, in each setup, as software-rendered PNGs named
// "<setup>-<screen>": the images the design pass is judged by, before and after.
// TEST DATA: a made-up board and words; nothing here is content for a child.
int run_shots(const QString& out) {
    const wjs::Day today = wjs::day_from_civil(2026, 10, 19);
    const ShotSetup setups[] = {
        {"d1100x700-light", 1100, 700, "light", 1.0},
        {"d1100x700-dark", 1100, 700, "dark", 1.0},
        {"s900x620-light", 900, 620, "light", 1.0},
        {"s900x620-dark", 900, 620, "dark", 1.0},
        {"s900x620-150-light", 900, 620, "light", 1.5},
        {"s900x1200-light", 900, 1200, "light", 1.0},
        {"s1024x768-light", 1024, 768, "light", 1.0},
        {"s2400x1200-light", 2400, 1200, "light", 1.0},
    };

    for (const ShotSetup& setup : setups) {
        wjs::Dictionary dictionary;
        for (const char* word : {"SAT", "SIT", "PIN", "NIP", "TAP"}) {
            static_cast<void>(dictionary.insert(word));
        }
        wjs::Store store = wjs::Store::in_memory();
        wjs::app::LanguageManager language;
        language.apply(QStringLiteral("en"));

        // A camera that is there, for the ready screen's live view; the no-camera
        // screen is the same models with no source (the second pass below).
        for (const bool with_camera : {false, true}) {
            auto backend = std::make_unique<wjs::app::test::FakeCameraBackend>();
            backend->setAvailable(true);
            backend->setProblem(QString());
            wjs::app::CameraSource camera(std::move(backend));

            wjs::app::ScanModel scan;
            if (with_camera) {
                scan.setCameraSource(&camera);
            }
            wjs::app::AssistModel assist([&dictionary, &store](wjs::Board board) {
                return std::make_unique<wjs::app::AssistGame>(std::move(board), dictionary, store,
                                                              [] { return today; });
            });
            QQmlApplicationEngine engine;
            engine.setInitialProperties({
                {QStringLiteral("scan"), QVariant::fromValue(&scan)},
                {QStringLiteral("assist"), QVariant::fromValue(&assist)},
                {QStringLiteral("languageManager"), QVariant::fromValue(&language)},
                {QStringLiteral("scheme"), QString::fromLatin1(setup.scheme)},
                {QStringLiteral("textScale"), setup.scale},
            });
            engine.loadFromModule("WordJewelsSolver", "PreviewHost");
            if (engine.rootObjects().isEmpty()) {
                check(false, "the stub shell loads");
                continue;
            }
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            if (window == nullptr) {
                check(false, "the stub shell is a window");
                continue;
            }
            window->resize(setup.width, setup.height);
            pump(12);
            const QString prefix = QString::fromLatin1(setup.tag) + "-";

            if (with_camera) {
                for (int i = 0; i < 40 && !window->property("previewShown").toBool(); ++i) {
                    pump(1);
                }
                save(window, out, prefix + "ready-camera");
                continue;
            }

            save(window, out, prefix + "ready-nocamera");

            scan.typeBoard();
            pump(12);
            save(window, out, prefix + "check-empty");

            // A reading with doubts: the ringed tiles and the hint under the board.
            wjs::scan::BoardReading reading;
            reading.rows = 3;
            reading.columns = 3;
            reading.tiles.resize(9);
            const char* seen[] = {"C", "A", "T", "S", "E", "R", "P", "I", "N"};
            for (int i = 0; i < 9; ++i) {
                reading.tiles[static_cast<std::size_t>(i)].letters = seen[i];
                reading.tiles[static_cast<std::size_t>(i)].confidence = 0.9;
                reading.tiles[static_cast<std::size_t>(i)].doubtful = false;
            }
            reading.tiles[1].doubtful = true;
            reading.tiles[1].confidence = 0.4;
            reading.tiles[1].alternatives[0].letters = "O";
            reading.tiles[1].alternatives[0].score = 0.4;
            reading.tiles[4].doubtful = true;
            reading.tiles[4].confidence = 0.45;
            if (scan.useReading(reading)) {
                pump(12);
                save(window, out, prefix + "check-doubtful");
                QQuickItem* check_screen =
                    find_with(window->contentItem(), {"picked", "boardRows"});
                if (check_screen != nullptr) {
                    check_screen->setProperty("picked", 1);
                    pump(12);
                    save(window, out, prefix + "picker");
                    check_screen->setProperty("picked", -1);
                    pump(4);
                }
            } else {
                check(false, "the doubtful reading was accepted");
            }

            scan.typeBoard();
            const QStringList letters = {"S", "A", "T", "X", "P", "I", "N", "X",
                                         "X", "X", "X", "X", "X", "X", "X", "X"};
            for (int index = 0; index < letters.size(); ++index) {
                static_cast<void>(scan.setTile(index, letters.at(index)));
            }
            pump(12);
            save(window, out, prefix + "check-filled");

            QVariant played;
            QMetaObject::invokeMethod(window, "play", Q_RETURN_ARG(QVariant, played));
            pump(12);
            save(window, out, prefix + "assist");
            assist.askHelp();
            pump(8);
            save(window, out, prefix + "assist-help");
            assist.tapTile(0);
            assist.tapTile(1);
            pump(8);
            save(window, out, prefix + "assist-tracing");
            assist.tapTile(2);
            assist.done();
            pump(12);
            save(window, out, prefix + "assist-found");

            // New board records the round; a second round then has a history.
            QMetaObject::invokeMethod(window, "newBoard");
            pump(8);
            scan.typeBoard();
            for (int index = 0; index < letters.size(); ++index) {
                static_cast<void>(scan.setTile(index, letters.at(index)));
            }
            QMetaObject::invokeMethod(window, "play", Q_RETURN_ARG(QVariant, played));
            pump(12);
            QQuickItem* history = find_with(window->contentItem(), {"open", "assist"});
            if (history != nullptr) {
                history->setProperty("open", true);
                pump(12);
                save(window, out, prefix + "assist-history");
                history->setProperty("open", false);
                pump(4);
            }
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
    const bool is_camera = arguments.contains(QStringLiteral("--camera"));
    const bool is_shots = arguments.contains(QStringLiteral("--shots"));
    QString out;
    const qsizetype flag = arguments.indexOf(QStringLiteral("--out"));
    if (flag >= 0 && flag + 1 < arguments.size()) {
        out = arguments.at(flag + 1);
    }

    if (is_check || is_languages || is_camera || is_shots) {
        // No display and no GPU: the software renderer draws into memory.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qInstallMessageHandler(message_handler);
    }
    QGuiApplication application(argc, argv);

    if (is_check) {
        const int result = run_check(out);
        std::fprintf(stderr, result == 0 ? "scan screens: ok\n" : "scan screens: FAILED\n");
        return result;
    }
    if (is_shots) {
        // The house faces, from a directory of .ttf files (assets/fonts): the
        // headless stub shell does not load the application's resource, and the
        // fallback face would change every line's width in the pictures.
        const qsizetype fonts_flag = arguments.indexOf(QStringLiteral("--fonts"));
        if (fonts_flag >= 0 && fonts_flag + 1 < arguments.size()) {
            const QDir fonts(arguments.at(fonts_flag + 1));
            for (const QString& file : fonts.entryList({QStringLiteral("*.ttf")}, QDir::Files)) {
                QFontDatabase::addApplicationFont(fonts.filePath(file));
            }
        }
        const int result = run_shots(out);
        std::fprintf(stderr, result == 0 ? "scan shots: ok\n" : "scan shots: FAILED\n");
        return result;
    }
    if (is_camera) {
        const int result = run_camera_check(out);
        std::fprintf(stderr, result == 0 ? "camera screens: ok\n" : "camera screens: FAILED\n");
        return result;
    }
    if (is_languages) {
        const int result = run_language_check(out);
        std::fprintf(stderr, result == 0 ? "scan languages: ok\n" : "scan languages: FAILED\n");
        return result;
    }

    // Interactive: the two screens, empty, to look at.
    wjs::Dictionary dictionary;
    for (const char* word : {"SAT", "SIT", "PIN", "NIP", "TAP"}) {
        static_cast<void>(dictionary.insert(word));
    }
    wjs::Store store = wjs::Store::in_memory();
    wjs::app::LanguageManager language;
    language.apply(QString());
    wjs::app::ScanModel scan;
    wjs::app::AssistModel assist([&dictionary, &store](wjs::Board board) {
        return std::make_unique<wjs::app::AssistGame>(
            std::move(board), dictionary, store, [] { return wjs::day_from_civil(2026, 10, 19); });
    });
    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("scan"), QVariant::fromValue(&scan)},
        {QStringLiteral("assist"), QVariant::fromValue(&assist)},
        {QStringLiteral("languageManager"), QVariant::fromValue(&language)},
    });
    engine.loadFromModule("WordJewelsSolver", "PreviewHost");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return application.exec();
}
