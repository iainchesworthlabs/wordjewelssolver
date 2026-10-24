// SPDX-License-Identifier: GPL-3.0-or-later
#include "quick_fixture.hpp"

#include <QQmlContext>
#include <utility>

#include "../tests/unit/fake_camera.hpp"
#include "assist_game.hpp"
#include "camera_source.hpp"
#include "fake_solo_game.hpp"
#include "wjs/scan/reading.hpp"

namespace {

// The dictionary a confirmed Family Assistant board is played over in the
// suites: the five test words of apps/app/tests/unit/scan_ui/preview_main.cpp, so a typed
// 4x4 of SAT/PIN... holds a word and the tray has a jewel. TEST DATA.
void fill_test_dictionary(wjs::Dictionary& dictionary) {
    for (const char* word : {"SAT", "SIT", "PIN", "NIP", "TAP"}) {
        static_cast<void>(dictionary.insert(word));
    }
}

}  // namespace

QuickFixture::QuickFixture(QObject* parent) : QObject(parent) {}

QuickFixture::~QuickFixture() = default;

void QuickFixture::applicationAvailable() {
    // One manager for the run: apply() swaps the installed catalogue, and a
    // manager destroyed with a translator still installed would leave it behind
    // for every later suite. reset() applies English again.
    language_ = std::make_unique<wjs::app::LanguageManager>();
    language_->apply(QStringLiteral("en"));
}

void QuickFixture::qmlEngineAvailable(QQmlEngine* engine) {
    engine_ = engine;
    setContextProperty("fixture", this);
    setContextProperty("languageManager", language_.get());
    reset();
}

void QuickFixture::reset() {
    clock_ = 1000;
    useLanguage(QStringLiteral("en"));
    resetSolo();
    resetParent(true);
    resetScan();
    resetAssist();
}

void QuickFixture::useLanguage(const QString& setting) {
    if (language_ != nullptr) {
        language_->apply(setting);
    }
}

void QuickFixture::advance(qint64 milliseconds) {
    clock_ += milliseconds;
}

void QuickFixture::resetSolo() {
    retired_solos_.push_back(std::move(solo_));
    solo_ = std::make_unique<wjs::app::SoloModel>(std::make_unique<wjs::app::FakeSoloGame>());
    setContextProperty("soloModel", solo_.get());
}

void QuickFixture::resetParent(bool busy) {
    auto data = std::make_unique<ParentData>();
    if (busy) {
        data->store = wjs::app::demo::busy_store(today_);
    }
    data->controller.useCalendar([this] { return today_; }, 1);
    data->controller.useClock([this] { return clock_; });
    data->controller.useStore(&data->store);
    data->controller.usePhases(&data->phases);
    retired_parents_.push_back(std::move(parent_));
    parent_ = std::move(data);
    setContextProperty("parentController", &parent_->controller);
}

void QuickFixture::resetParentWithEmptyStore() {
    resetParent(false);
}

void QuickFixture::resetScan() {
    retired_scans_.push_back(std::move(scan_));
    scan_ = std::make_unique<wjs::app::ScanModel>();
    setContextProperty("scanModel", scan_.get());
}

void QuickFixture::resetAssist() {
    auto data = std::make_shared<AssistData>();
    fill_test_dictionary(data->dictionary);
    assist_data_.push_back(data);

    // The maker the model asks for a round: the application installs one over
    // its dictionary and store in main.cpp, and this is the same seam. It holds
    // the data it makes rounds from for as long as a round can be alive.
    assist_maker_.reset();
    assist_maker_.emplace([data](wjs::Board board) {
        return std::make_unique<wjs::app::AssistGame>(std::move(board), data->dictionary,
                                                      data->store, [data] { return data->today; });
    });

    retired_assists_.push_back(std::move(assist_));
    assist_ = std::make_unique<wjs::app::AssistModel>();
    setContextProperty("assistModel", assist_.get());
}

bool QuickFixture::readDoubtfulBoard() {
    if (scan_ == nullptr) {
        return false;
    }
    // What the reader makes of a frame whose middle tiles it could not trust: a
    // 2x2 with one letter seen, one seen with an alternative, one seen nothing
    // in, and one seen but doubted.
    wjs::scan::BoardReading reading;
    reading.rows = 2;
    reading.columns = 2;
    reading.tiles.resize(4);
    reading.tiles[0] = {};
    reading.tiles[0].letters = "C";
    reading.tiles[0].confidence = 0.9;
    reading.tiles[0].doubtful = false;
    reading.tiles[1] = {};
    reading.tiles[1].letters = "A";
    reading.tiles[1].confidence = 0.4;
    reading.tiles[1].doubtful = true;
    reading.tiles[1].alternatives[0].letters = "O";
    reading.tiles[1].alternatives[0].score = 0.4;
    reading.tiles[2] = {};  // the reader saw nothing here
    reading.tiles[2].doubtful = true;
    reading.tiles[3] = {};
    reading.tiles[3].letters = "T";
    reading.tiles[3].confidence = 0.5;
    reading.tiles[3].doubtful = true;
    return scan_->useReading(reading);
}

void QuickFixture::useCamera() {
    auto backend = std::make_unique<wjs::app::test::FakeCameraBackend>();
    backend->setAvailable(true);
    backend->setProblem(QString());
    // The source is kept for the fixture's lifetime: a model points at it, and
    // a destroyed screen's destruction still calls close() on the model.
    cameras_.push_back(std::make_unique<wjs::app::CameraSource>(std::move(backend)));
    if (scan_ != nullptr) {
        scan_->setCameraSource(cameras_.back().get());
    }
}

void QuickFixture::setContextProperty(const char* name, QObject* object) {
    if (engine_ != nullptr && object != nullptr) {
        engine_->rootContext()->setContextProperty(QString::fromLatin1(name), object);
    }
}
