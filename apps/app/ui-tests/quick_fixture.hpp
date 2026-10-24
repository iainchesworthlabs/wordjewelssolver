// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <memory>
#include <optional>
#include <vector>

#include "assist_game_factory.hpp"
#include "assist_model.hpp"
#include "demo_data.hpp"
#include "language_manager.hpp"
#include "parent_controller.hpp"
#include "scan_model.hpp"
#include "solo_model.hpp"
#include "wjs/calendar.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/store.hpp"

namespace wjs::app {
class CameraSource;
}

// What the Qt Quick Test suites talk to: the application's own models, made the
// way the application makes them, and a reset for each test function.
//
// It is the C++ half of the harness (quicktest_main.cpp is the entry point). The
// `qmlEngineAvailable` hook, which Quick Test calls once per tst_*.qml file,
// registers these as context properties under the names the suites use:
//
//   soloModel        SoloModel over FakeSoloGame (the application's test game)
//   parentController ParentController over an in-memory store and the demo phases
//   scanModel        ScanModel (no camera on these desktops)
//   assistModel      AssistModel, with a round maker over a test dictionary and store
//   languageManager  LanguageManager, so a suite can apply Hebrew or Yiddish
//   fixture          this object, for reset() and the few things only C++ can do
//
// The Feedback seam's engine (kit/Feedback.qml) is left null here: the suites
// record the cue signal and nothing plays or speaks, which is the quiet engine
// a stub shell has. Main.qml is the only place that installs the real one.
//
// The models are the application's classes and nothing here changes them: a
// suite drives the screens exactly as the program does, with a store and a game
// that hold TEST DATA (apps/app/tests/unit/parent_ui/demo_data.hpp, the fake game's own
// header), never content for a child.
//
// A suite calls fixture.reset() from init() so that one test function's play is
// not the next one's. The old models are kept alive rather than destroyed: a
// context property that a QML object still holds must not dangle, and a
// destroyed screen's Component.onDestruction (AssistScreen calls assist.leave())
// runs after the reset that replaced its model.
class QuickFixture final : public QObject {
    Q_OBJECT

public:
    explicit QuickFixture(QObject* parent = nullptr);
    ~QuickFixture() override;

public slots:
    // Quick Test's two hooks (quicktest.h): the first after QGuiApplication is
    // made, the second once per suite, before the tst_*.qml file is loaded.
    void applicationAvailable();
    void qmlEngineAvailable(QQmlEngine* engine);

public:
    // A fresh game, parent area, scan and assistant, the clock at its start and
    // the interface in English: what a test function begins from.
    Q_INVOKABLE void reset();

    // Applies one of the eight languages (or "" for the device's): the catalogue,
    // which turns the layout in Hebrew and Yiddish.
    Q_INVOKABLE void useLanguage(const QString& setting);

    // The gate's clock, in milliseconds: the screens read the controller's (its
    // now()), and a suite says how much time has passed.
    Q_INVOKABLE void advance(qint64 milliseconds);

    // A parent area with nothing played yet: the dashboard's empty case.
    Q_INVOKABLE void resetParentWithEmptyStore();

    // A draft as the reader would hand it to the check screen: a 2x2 with three
    // doubtful tiles. ScanModel::useReading() is the camera's path and is not
    // invokable from QML, so a suite asks for it here.
    Q_INVOKABLE bool readDoubtfulBoard();

    // A camera source over the test's fake backend, running at once: the scan
    // screen then shows its live view and Capture. No device is opened; the
    // fake's frames are made up or absent. A suite that does not call this gets
    // the no-camera screen, as the program on a desktop with no webcam does.
    Q_INVOKABLE void useCamera();

private:
    // Everything one parent area is made of: the store, the phase data and the
    // controller over them. Held together because the controller points at the
    // other two by address.
    struct ParentData {
        wjs::PhaseSet phases = wjs::app::demo::phases();
        wjs::Store store = wjs::Store::in_memory();
        wjs::app::ParentController controller;
    };

    // The dictionary and store a confirmed Family Assistant board is played
    // over, for as long as the maker that hands them out is installed.
    struct AssistData {
        wjs::Dictionary dictionary;
        wjs::Store store = wjs::Store::in_memory();
        // The day a round records its finds on (the suites' fixed Friday).
        wjs::Day today = wjs::day_from_civil(2026, 10, 16);
    };

    void resetSolo();
    void resetParent(bool busy);
    void resetScan();
    void resetAssist();
    void setContextProperty(const char* name, QObject* object);

    std::unique_ptr<wjs::app::SoloModel> solo_;
    std::vector<std::unique_ptr<wjs::app::SoloModel>> retired_solos_;

    std::unique_ptr<ParentData> parent_;
    std::vector<std::unique_ptr<ParentData>> retired_parents_;

    std::unique_ptr<wjs::app::ScanModel> scan_;
    std::vector<std::unique_ptr<wjs::app::ScanModel>> retired_scans_;
    // Declared before the scans so it is destroyed after them: a model's
    // destructor stops its camera, and the source has to outlive it.
    std::vector<std::unique_ptr<wjs::app::CameraSource>> cameras_;

    std::unique_ptr<wjs::app::AssistModel> assist_;
    std::vector<std::unique_ptr<wjs::app::AssistModel>> retired_assists_;
    std::vector<std::shared_ptr<AssistData>> assist_data_;
    std::optional<wjs::app::ScopedAssistGameMaker> assist_maker_;

    std::unique_ptr<wjs::app::LanguageManager> language_;
    QQmlEngine* engine_ = nullptr;
    qint64 clock_ = 1000;
    wjs::Day today_ = wjs::day_from_civil(2026, 10, 16);
};
