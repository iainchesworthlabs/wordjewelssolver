// SPDX-License-Identifier: GPL-3.0-or-later
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

#include "demo_data.hpp"
#include "parent_controller.hpp"
#include "test_support.hpp"
#include "wjs/calendar.hpp"
#include "wjs/scoring.hpp"
#include "wjs/settings.hpp"
#include "wjs/store.hpp"

namespace {

using wjs::app::ParentController;

constexpr wjs::Day friday = 20742;  // 2026-10-16

// A directory of its own that is gone when the case ends.
using TempDirectory = wjs::app::test::TempDirectory;

// A controller on the made-up play of the demo data, as of a Friday.
struct Fixture {
    wjs::PhaseSet phases = wjs::app::demo::phases();
    wjs::Store store = wjs::app::demo::busy_store(friday);
    ParentController controller;

    Fixture() {
        controller.useCalendar([] { return friday; }, 1);
        controller.useStore(&store);
        controller.usePhases(&phases);
    }
};

void open_gate(ParentController& controller) {
    controller.layoutGate(1000.0, 700.0, 48.0);
    const QVariantList spots = controller.gateSpots();
    REQUIRE(spots.size() == 2);
    controller.gateTouchDown(1, spots.at(0).toMap().value("x").toDouble(),
                             spots.at(0).toMap().value("y").toDouble(), 0);
    controller.gateTouchDown(2, spots.at(1).toMap().value("x").toDouble(),
                             spots.at(1).toMap().value("y").toDouble(), 0);
    controller.gateTick(2000);
}

QVariantMap map(const QVariant& value) {
    return value.toMap();
}

}  // namespace

// ---------------------------------------------------------------------------
// The gate

TEST_CASE("a new controller is locked and holds nothing", "[app][parent]") {
    ParentController controller;

    CHECK_FALSE(controller.unlocked());
    CHECK_FALSE(controller.gateHolding());
    CHECK(controller.gateProgress() == 0.0);
    CHECK(map(controller.dashboard()).value("empty").toBool());
}

TEST_CASE("the gate lays two spots out for the screen it is given", "[app][parent][gate]") {
    ParentController controller;

    controller.layoutGate(1000.0, 700.0, 48.0);

    const QVariantList spots = controller.gateSpots();
    REQUIRE(spots.size() == 2);
    CHECK(map(spots.at(0)).value("x").toDouble() == 250.0);
    CHECK(map(spots.at(1)).value("x").toDouble() == 750.0);
    CHECK(map(spots.at(0)).value("r").toDouble() >= 48.0);
}

TEST_CASE("two fingers held for two seconds open the gate", "[app][parent][gate]") {
    ParentController controller;
    int opened = 0;
    QObject::connect(&controller, &ParentController::unlockedChanged, [&opened] { ++opened; });
    controller.layoutGate(1000.0, 700.0, 48.0);
    const QVariantList spots = controller.gateSpots();
    const auto at = [&spots](int i, const char* axis) {
        return spots.at(i).toMap().value(axis).toDouble();
    };

    controller.gateTouchDown(1, at(0, "x"), at(0, "y"), 100);
    CHECK_FALSE(controller.gateHolding());
    controller.gateTouchDown(2, at(1, "x"), at(1, "y"), 100);
    CHECK(controller.gateHolding());

    controller.gateTick(1100);
    CHECK(controller.gateProgress() == 0.5);
    CHECK_FALSE(controller.unlocked());
    controller.gateTick(2099);
    CHECK_FALSE(controller.unlocked());
    controller.gateTick(2100);
    CHECK(controller.unlocked());
    CHECK(controller.gateProgress() == 1.0);
    CHECK(opened == 1);

    controller.gateTick(9999);
    CHECK(opened == 1);  // told once
}

TEST_CASE("lifting a finger early starts the hold over", "[app][parent][gate]") {
    ParentController controller;
    controller.layoutGate(1000.0, 700.0, 48.0);
    const QVariantList spots = controller.gateSpots();
    const double x0 = spots.at(0).toMap().value("x").toDouble();
    const double y0 = spots.at(0).toMap().value("y").toDouble();
    const double x1 = spots.at(1).toMap().value("x").toDouble();
    const double y1 = spots.at(1).toMap().value("y").toDouble();

    controller.gateTouchDown(1, x0, y0, 0);
    controller.gateTouchDown(2, x1, y1, 0);
    controller.gateTouchUp(2, 1500);
    controller.gateTick(5000);

    CHECK_FALSE(controller.unlocked());
    CHECK_FALSE(controller.gateHolding());
    CHECK(controller.gateProgress() == 0.0);
}

TEST_CASE("the accessibility action opens the gate with no hold", "[app][parent][gate]") {
    ParentController controller;

    controller.unlockForAssistiveTechnology();

    CHECK(controller.unlocked());
}

TEST_CASE("leaving shuts the gate again", "[app][parent][gate]") {
    ParentController controller;
    open_gate(controller);
    REQUIRE(controller.unlocked());

    controller.lock();

    CHECK_FALSE(controller.unlocked());
    CHECK(controller.gateProgress() == 0.0);
}

TEST_CASE("opening the gate computes the dashboard as of that moment", "[app][parent][gate]") {
    Fixture f;
    f.controller.lock();
    const QVariantMap before = map(f.controller.dashboard());

    // Something happens behind the screen's back.
    wjs::BoardSnapshot board;
    board.size = 3;
    board.tiles = "Q Q Q/P I N/T A P";
    board.viable_total = 5;
    board.finds = {{"quiz", false, friday, 5}};
    REQUIRE(f.store.record(board, friday).stored);
    CHECK(map(f.controller.dashboard()).value("week").toMap() == before.value("week").toMap());

    open_gate(f.controller);

    CHECK(map(f.controller.dashboard()).value("week").toMap().value("stars").toInt() ==
          before.value("week").toMap().value("stars").toInt() + 5);
}

// ---------------------------------------------------------------------------
// The dashboard

TEST_CASE("the dashboard map has what the dashboard has", "[app][parent][dashboard]") {
    Fixture f;

    const QVariantMap d = map(f.controller.dashboard());

    CHECK_FALSE(d.value("empty").toBool());
    CHECK(d.value("hasPhaseData").toBool());
    CHECK(d.value("phase").toString() == "B");
    CHECK(d.value("sounds").toList().size() == 10);
    CHECK_FALSE(d.value("vault").toList().isEmpty());
    CHECK_FALSE(d.value("best").toList().isEmpty());
    const QVariantMap week = d.value("week").toMap();
    CHECK(week.value("played").toBool());
    CHECK(week.value("boardsCleared").toInt() == 1);
    CHECK(week.value("stars").toInt() > 0);
    CHECK_FALSE(week.value("longestWord").toString().isEmpty());
}

TEST_CASE("a sound's row has its counts", "[app][parent][dashboard]") {
    Fixture f;

    QVariantMap ai;
    for (const QVariant& row : map(f.controller.dashboard()).value("sounds").toList()) {
        if (row.toMap().value("letters").toString() == "ai") {
            ai = row.toMap();
        }
    }

    REQUIRE_FALSE(ai.isEmpty());
    CHECK(ai.value("boardsMet").toInt() == 5);
    CHECK(ai.value("boardsFound").toInt() == 3);
    CHECK(ai.value("wordsFound").toInt() ==
          3);  // pain, stain and paint, each once whatever its finds
    CHECK(ai.value("focus").toBool());
    CHECK_FALSE(ai.value("fewWords").toBool());
}

TEST_CASE("the week follows the calendar the controller was given", "[app][parent][dashboard]") {
    Fixture f;
    // A find on the Sunday before the Friday: the first day of a Sunday-first week
    // and the last of the Monday-first week before.
    wjs::BoardSnapshot sunday;
    sunday.size = 3;
    sunday.tiles = "Q Q Q/P I N/T A P";
    sunday.viable_total = 5;
    sunday.finds = {{"quiz", false, friday - 5, 5}};
    static_cast<void>(f.store.record(sunday, friday - 5));

    f.controller.useCalendar([] { return friday; }, 1);
    const int monday_first =
        map(f.controller.dashboard()).value("week").toMap().value("stars").toInt();

    // Sunday-first: the week of the 16th starts on the 11th, which takes in the five
    // stars of the Sunday's find. A calendar that ignored the first day would give
    // the same number twice.
    f.controller.useCalendar([] { return friday; }, 7);

    CHECK(map(f.controller.dashboard()).value("week").toMap().value("stars").toInt() ==
          monday_first + 5);
    f.controller.useCalendar([] { return friday; }, 1);
    // A calendar that has moved on a week leaves this week without play.
    f.controller.useCalendar([] { return friday + 14; }, 1);
    CHECK_FALSE(map(f.controller.dashboard()).value("week").toMap().value("played").toBool());
}

TEST_CASE("without phase data there are no sounds", "[app][parent][dashboard]") {
    Fixture f;

    f.controller.usePhases(nullptr);

    const QVariantMap d = map(f.controller.dashboard());
    CHECK_FALSE(d.value("hasPhaseData").toBool());
    CHECK(d.value("sounds").toList().isEmpty());
    CHECK(f.controller.focusChoices().isEmpty());
    CHECK_FALSE(f.controller.phonicsAvailable());
}

// ---------------------------------------------------------------------------
// The settings

TEST_CASE("the settings map has the table's defaults", "[app][parent][settings]") {
    ParentController controller;

    const QVariantMap s = controller.settings();

    CHECK(s.value("help").toString() == "two");
    CHECK(s.value("board").toString() == "standard");
    CHECK(s.value("phase").toString() == "A");
    CHECK(s.value("boardSize").toInt() == 3);
    CHECK(s.value("soundItOut").toString() == "full");
    CHECK(s.value("celebrationSounds").toString() == "full");
    CHECK(s.value("sound").toBool());
    CHECK(s.value("vibration").toBool());
    CHECK(s.value("shake").toBool());
    CHECK(s.value("motion").toString() == "system");
    CHECK(s.value("colourScheme").toString() == "system");
    CHECK(s.value("timer").toString() == "off");
    CHECK(s.value("timerLength").toInt() == wjs::default_solo_clock_minutes);
    CHECK(s.value("textSizePercent").toInt() == 100);
    CHECK(s.value("schoolFont").toString().isEmpty());
    CHECK(s.value("language").toString().isEmpty());
    CHECK(s.value("focusSounds").toStringList().isEmpty());
}

TEST_CASE("each setting can be changed by its name and is kept in the store",
          "[app][parent][settings]") {
    Fixture f;

    CHECK(f.controller.setSetting("help", "off"));
    CHECK(f.controller.setSetting("board", "standard"));
    CHECK(f.controller.setSetting("phase", "A"));
    CHECK(f.controller.setSetting("boardSize", 4));
    CHECK(f.controller.setSetting("soundItOut", "word"));
    CHECK(f.controller.setSetting("celebrationSounds", "quiet"));
    CHECK(f.controller.setSetting("sound", false));
    CHECK(f.controller.setSetting("vibration", false));
    CHECK(f.controller.setSetting("shake", false));
    CHECK(f.controller.setSetting("motion", "reduced"));
    CHECK(f.controller.setSetting("colourScheme", "dark"));
    CHECK(f.controller.setSetting("timer", "down"));
    CHECK(f.controller.setSetting("timerLength", 20));
    CHECK(f.controller.setSetting("textSizePercent", 150));
    CHECK(f.controller.setSetting("language", "he"));

    const wjs::Settings& s = f.store.settings();
    CHECK(s.help == wjs::HelpLevel::off);
    CHECK(s.phase == "A");
    CHECK(s.board_size == 4);
    CHECK(s.sound_it_out == wjs::SoundItOut::word_only);
    CHECK(s.celebration_sounds == wjs::CelebrationSounds::quiet);
    CHECK_FALSE(s.sound);
    CHECK_FALSE(s.vibration);
    CHECK_FALSE(s.shake);
    CHECK(s.motion == wjs::MotionSetting::reduced);
    CHECK(s.colour_scheme == wjs::ColourScheme::dark);
    CHECK(s.solo_clock == wjs::SoloClock::count_down);
    CHECK(s.solo_clock_minutes == 20);
    CHECK(s.text_size_percent == 150);
    CHECK(s.language == "he");
    // And the map the screens read is the new one.
    CHECK(f.controller.settings().value("boardSize").toInt() == 4);
    CHECK(f.controller.settings().value("help").toString() == "off");
    CHECK(f.controller.settings().value("colourScheme").toString() == "dark");
    CHECK(f.controller.settings().value("timer").toString() == "down");
    CHECK(f.controller.settings().value("timerLength").toInt() == 20);
    CHECK(f.controller.settings().value("language").toString() == "he");
    CHECK(f.controller.property("language").toString() == "he");
}

TEST_CASE("numbers from QML arrive as doubles and are accepted when whole",
          "[app][parent][settings]") {
    ParentController controller;

    CHECK(controller.setSetting("boardSize", 4.0));
    CHECK(controller.settings().value("boardSize").toInt() == 4);
    CHECK(controller.setSetting("textSizePercent", 125.0));
    CHECK_FALSE(controller.setSetting("boardSize", 3.5));
}

TEST_CASE("a value the table does not have is refused and nothing changes",
          "[app][parent][settings]") {
    Fixture f;
    const QVariantMap before = f.controller.settings();

    CHECK_FALSE(f.controller.setSetting("nonsense", true));
    CHECK_FALSE(f.controller.setSetting("help", "yes"));
    CHECK_FALSE(f.controller.setSetting("help", 1));
    CHECK_FALSE(f.controller.setSetting("board", "cubist"));
    CHECK_FALSE(f.controller.setSetting("phase", "Z"));
    // The standard board plays 3x3 to 6x6, so 5 and 6 are values it has; 7 is not.
    CHECK_FALSE(f.controller.setSetting("boardSize", 7));
    CHECK_FALSE(f.controller.setSetting("boardSize", "3"));
    CHECK_FALSE(f.controller.setSetting("soundItOut", "loud"));
    CHECK_FALSE(f.controller.setSetting("celebrationSounds", ""));
    CHECK_FALSE(f.controller.setSetting("motion", true));
    CHECK_FALSE(f.controller.setSetting("colourScheme", "purple"));
    CHECK_FALSE(f.controller.setSetting("colourScheme", true));
    CHECK_FALSE(f.controller.setSetting("timer", "sideways"));
    CHECK_FALSE(f.controller.setSetting("timer", true));
    CHECK_FALSE(f.controller.setSetting("timerLength", 4));
    CHECK_FALSE(f.controller.setSetting("timerLength", 3.5));
    CHECK_FALSE(f.controller.setSetting("textSizePercent", 130));
    CHECK_FALSE(f.controller.setSetting("schoolFont", "Some Font"));
    CHECK_FALSE(f.controller.setSetting("language", "tlh"));
    CHECK_FALSE(f.controller.setSetting("language", "en_US"));
    CHECK_FALSE(f.controller.setSetting("language", true));

    CHECK(f.controller.settings() == before);
}

TEST_CASE("the help is off, the start tile, or the start and two letters",
          "[app][parent][settings]") {
    ParentController controller;
    CHECK(controller.helpLevel() == "two");
    CHECK(controller.settings().value("help").toString() == "two");

    REQUIRE(controller.setSetting("help", "start"));
    CHECK(controller.helpLevel() == "start");
    CHECK(controller.settings().value("help").toString() == "start");

    REQUIRE(controller.setSetting("help", "off"));
    CHECK(controller.helpLevel() == "off");

    REQUIRE(controller.setSetting("help", "two"));
    CHECK(controller.helpLevel() == "two");
}

TEST_CASE("the language is one of the eight codes, or the system's", "[app][parent][settings]") {
    ParentController controller;
    CHECK(controller.language().isEmpty());  // the system's

    REQUIRE(controller.setSetting("language", "fr"));
    CHECK(controller.language() == "fr");
    CHECK(controller.settings().value("language").toString() == "fr");
    // Upper case is the same code.
    REQUIRE(controller.setSetting("language", "HE"));
    CHECK(controller.language() == "he");
    // Back to the system's.
    REQUIRE(controller.setSetting("language", ""));
    CHECK(controller.language().isEmpty());
}

TEST_CASE("no school font is offered, because none can be bundled", "[app][parent][settings]") {
    ParentController controller;

    CHECK(controller.schoolFonts().isEmpty());
    CHECK(controller.setSetting("schoolFont", ""));
}

TEST_CASE("the phonics board cannot be chosen without its words", "[app][parent][settings]") {
    ParentController controller;
    CHECK_FALSE(controller.phonicsAvailable());
    CHECK_FALSE(controller.setSetting("board", "phonics"));

    wjs::PhaseSet phases = wjs::app::demo::phases();
    controller.usePhases(&phases);

    CHECK(controller.phonicsAvailable());
    CHECK(controller.setSetting("board", "phonics"));
    CHECK(controller.settings().value("board").toString() == "phonics");
}

TEST_CASE("the phases on offer are the data's, or two generic names", "[app][parent][settings]") {
    ParentController controller;
    CHECK(controller.phases() == QStringList{"A", "B"});

    wjs::PhaseSet phases = wjs::app::demo::phases();
    controller.usePhases(&phases);

    CHECK(controller.phases() == QStringList{"A", "B"});
}

TEST_CASE("only sounds of the phase can be ticked", "[app][parent][settings]") {
    Fixture f;

    CHECK(f.controller.toggleFocusSound("s"));
    CHECK(f.controller.settings().value("focusSounds").toStringList().contains("s"));
    CHECK_FALSE(f.controller.toggleFocusSound("ee"));
    CHECK_FALSE(f.controller.toggleFocusSound(""));
    CHECK_FALSE(f.controller.settings().value("focusSounds").toStringList().contains("ee"));

    // Ticking again unticks.
    CHECK(f.controller.toggleFocusSound("s"));
    CHECK_FALSE(f.controller.settings().value("focusSounds").toStringList().contains("s"));
}

TEST_CASE("changing the phase drops the ticked sounds the new phase does not have",
          "[app][parent][settings]") {
    Fixture f;
    REQUIRE(f.controller.settings().value("focusSounds").toStringList().contains("ai"));

    CHECK(f.controller.setSetting("phase", "A"));

    // "ai" is phase B's.
    CHECK_FALSE(f.controller.settings().value("focusSounds").toStringList().contains("ai"));
    CHECK(f.controller.settings().value("focusSounds").toStringList().contains("z"));
}

TEST_CASE("the screens read the text size and the motion from the settings",
          "[app][parent][settings]") {
    ParentController controller;
    CHECK(controller.textScale() == 1.0);
    CHECK_FALSE(controller.reducedMotion());

    controller.setSetting("textSizePercent", 125);
    controller.setSetting("motion", "reduced");

    CHECK(controller.textScale() == 1.25);
    CHECK(controller.reducedMotion());
}

// The window decides what System comes to (it alone knows what the system asks
// for), so the controller must say which of the two the parent chose.
TEST_CASE("the motion setting is told as a name, and System is not a choice for reduced",
          "[app][parent][settings]") {
    ParentController controller;
    CHECK(controller.motion() == "system");
    CHECK_FALSE(controller.reducedMotion());

    int changes = 0;
    QObject::connect(&controller, &ParentController::settingsChanged, [&changes] { ++changes; });

    REQUIRE(controller.setSetting("motion", "reduced"));
    CHECK(controller.motion() == "reduced");
    CHECK(controller.reducedMotion());
    CHECK(changes >= 1);

    REQUIRE(controller.setSetting("motion", "system"));
    CHECK(controller.motion() == "system");
    CHECK_FALSE(controller.reducedMotion());

    // The property the window binds to carries the same name.
    CHECK(controller.property("motion").toString() == "system");
}

// The window applies the scheme to every screen's chrome; the controller only
// keeps the parent's choice and says which it is (qml/Main.qml).
TEST_CASE("the colour scheme is told as a name, and System is the default",
          "[app][parent][settings]") {
    ParentController controller;
    CHECK(controller.colourScheme() == "system");
    CHECK(controller.property("colourScheme").toString() == "system");

    int changes = 0;
    QObject::connect(&controller, &ParentController::settingsChanged, [&changes] { ++changes; });

    REQUIRE(controller.setSetting("colourScheme", "dark"));
    CHECK(controller.colourScheme() == "dark");
    CHECK(changes >= 1);

    REQUIRE(controller.setSetting("colourScheme", "light"));
    CHECK(controller.colourScheme() == "light");

    REQUIRE(controller.setSetting("colourScheme", "system"));
    CHECK(controller.colourScheme() == "system");
}

// The parent's board-size default is one the mode's board is played at: the
// standard board plays 3x3 to 6x6, the phonics board 3x3 and 4x4.
TEST_CASE("the board size the parent offers is the mode's", "[app][parent][settings]") {
    ParentController controller;  // the defaults are the standard board

    CHECK(controller.setSetting("boardSize", 6));
    CHECK(controller.settings().value("boardSize").toInt() == 6);
    CHECK_FALSE(controller.setSetting("boardSize", 7));

    wjs::PhaseSet phases = wjs::app::demo::phases();
    controller.usePhases(&phases);
    REQUIRE(controller.setSetting("board", "phonics"));
    // Phonics: 4 is its largest, and 5 is refused rather than clamped.
    CHECK(controller.setSetting("boardSize", 4));
    CHECK_FALSE(controller.setSetting("boardSize", 5));
    CHECK(controller.settings().value("boardSize").toInt() == 4);
}

// ---------------------------------------------------------------------------
// The two resets

TEST_CASE("the first reset clears boards and finds and the second clears stars and stickers",
          "[app][parent][reset]") {
    Fixture f;
    REQUIRE_FALSE(f.store.data().boards.empty());

    f.controller.resetBoardsAndFinds();
    CHECK(f.store.data().boards.empty());
    CHECK(f.store.data().finds.empty());
    CHECK(f.store.data().stars > 0);
    CHECK_FALSE(map(f.controller.dashboard()).value("vault").toList().isEmpty());
    // The days' tallies went with the boards and finds they were made of.
    CHECK(map(f.controller.dashboard()).value("week").toMap().value("boardsCleared").toInt() == 0);
    CHECK(map(f.controller.dashboard()).value("week").toMap().value("stars").toInt() == 0);

    f.controller.resetStarsAndStickers();
    CHECK(f.store.data().stars == 0);
    CHECK(map(f.controller.dashboard()).value("vault").toList().isEmpty());
    CHECK(map(f.controller.dashboard()).value("empty").toBool());
}

TEST_CASE("a reset keeps the settings", "[app][parent][reset]") {
    Fixture f;
    f.controller.setSetting("vibration", false);

    f.controller.resetBoardsAndFinds();
    f.controller.resetStarsAndStickers();

    CHECK_FALSE(f.store.settings().vibration);
}

// ---------------------------------------------------------------------------
// What the game is told of an edit

TEST_CASE("an edit of the store is told, and a read of it is not", "[app][parent][edited]") {
    Fixture f;
    int edited = 0;
    QObject::connect(&f.controller, &ParentController::storeEdited, [&edited] { ++edited; });

    // Opening a screen only reads.
    f.controller.refresh();
    CHECK(edited == 0);

    CHECK(f.controller.setSetting("help", "start"));
    CHECK(edited == 1);

    // The value it already has is no edit, and a value refused is none either.
    CHECK(f.controller.setSetting("help", "start"));
    CHECK_FALSE(f.controller.setSetting("boardSize", 7));
    CHECK(edited == 1);

    CHECK(f.controller.toggleFocusSound("s"));
    CHECK(edited == 2);

    f.controller.resetBoardsAndFinds();
    CHECK(edited == 3);
    f.controller.resetStarsAndStickers();
    CHECK(edited == 4);
}

// ---------------------------------------------------------------------------
// The store, and what the parent is told of it

TEST_CASE("a store in memory is said not to be saved", "[app][parent][store]") {
    ParentController controller;

    CHECK(controller.storeNote() == "unsaved");
}

TEST_CASE("a store in a file is kept, and what is set is there next time", "[app][parent][store]") {
    const TempDirectory dir;
    const auto file = dir.path() / "store.wjs";
    {
        ParentController controller;
        controller.openStore(file);
        CHECK(controller.storeNote().isEmpty());
        CHECK(controller.setSetting("boardSize", 4));
    }

    ParentController again;
    again.openStore(file);

    CHECK(again.settings().value("boardSize").toInt() == 4);
    CHECK(again.storeNote().isEmpty());
}

TEST_CASE("a store that could not be read says so, and the old file is kept",
          "[app][parent][store]") {
    const TempDirectory dir;
    const auto file = dir.path() / "store.wjs";
    {
        std::ofstream out(file, std::ios::binary);
        out << "not a store";
    }

    ParentController controller;
    controller.openStore(file);

    CHECK(controller.storeNote() == "unreadable");
    CHECK(std::filesystem::exists(dir.path() / "store.wjs.unreadable"));
}

TEST_CASE("a store from a newer version says so, and is left alone", "[app][parent][store]") {
    const TempDirectory dir;
    const auto file = dir.path() / "store.wjs";
    const std::string later = "wjs-store 9\nsomething new\nend 0000000000000000\n";
    {
        std::ofstream out(file, std::ios::binary);
        out << later;
    }

    ParentController controller;
    controller.openStore(file);
    controller.setSetting("help", "off");

    CHECK(controller.storeNote() == "newer");
    std::ifstream in(file, std::ios::binary);
    const std::string now((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(now == later);
}

TEST_CASE("the version is the project's, with the build's provenance", "[app][parent]") {
    ParentController controller;

    CHECK_FALSE(controller.version().isEmpty());
    const QString details = controller.versionDetails();
    CHECK(details.contains(controller.version()));
    CHECK(details.contains(QStringLiteral("commit:")));
    CHECK(details.contains(QStringLiteral("branch:")));
    CHECK(details.contains(QStringLiteral("target:")));
}

// ---------------------------------------------------------------------------
// Numbers from QML that are not numbers

TEST_CASE("a number that is not a whole int is refused, not converted", "[app][parent][settings]") {
    ParentController controller;
    const QVariantMap before = controller.settings();

    // Converting these to an int is undefined; they are refused before it.
    CHECK_FALSE(controller.setSetting("boardSize", std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(controller.setSetting("boardSize", std::numeric_limits<double>::infinity()));
    CHECK_FALSE(controller.setSetting("boardSize", -std::numeric_limits<double>::infinity()));
    CHECK_FALSE(controller.setSetting("boardSize", 1e20));
    CHECK_FALSE(controller.setSetting("boardSize", -1e20));
    CHECK_FALSE(controller.setSetting("timerLength", 2147483648.0));
    CHECK_FALSE(controller.setSetting("textSizePercent", 1e300));
    CHECK(controller.settings() == before);

    // The edge of the range is still a number, and a refused one for the table.
    CHECK_FALSE(controller.setSetting("boardSize", 2147483647.0));
    CHECK_FALSE(controller.setSetting("boardSize", -2147483648.0));
}

// ---------------------------------------------------------------------------
// The gate, further

TEST_CASE("a finger dragged off its spot cancels the hold", "[app][parent][gate]") {
    ParentController controller;
    controller.layoutGate(1000.0, 700.0, 48.0);
    const QVariantList spots = controller.gateSpots();
    const double x0 = spots.at(0).toMap().value("x").toDouble();
    const double y0 = spots.at(0).toMap().value("y").toDouble();
    const double x1 = spots.at(1).toMap().value("x").toDouble();
    const double y1 = spots.at(1).toMap().value("y").toDouble();

    controller.gateTouchDown(1, x0, y0, 0);
    controller.gateTouchDown(2, x1, y1, 0);
    REQUIRE(controller.gateHolding());

    controller.gateTouchMove(2, 5.0, 5.0, 500);
    CHECK_FALSE(controller.gateHolding());
    CHECK(controller.gateProgress() == 0.0);
    controller.gateTick(5000);
    CHECK_FALSE(controller.unlocked());
}

TEST_CASE("laying the gate out again while it is held starts the hold over",
          "[app][parent][gate]") {
    ParentController controller;
    controller.layoutGate(1000.0, 700.0, 48.0);
    const QVariantList spots = controller.gateSpots();
    controller.gateTouchDown(1, spots.at(0).toMap().value("x").toDouble(),
                             spots.at(0).toMap().value("y").toDouble(), 0);
    controller.gateTouchDown(2, spots.at(1).toMap().value("x").toDouble(),
                             spots.at(1).toMap().value("y").toDouble(), 0);
    controller.gateTick(1000);
    REQUIRE(controller.gateProgress() == 0.5);

    int moved = 0;
    QObject::connect(&controller, &ParentController::gateSpotsChanged, [&moved] { ++moved; });
    controller.layoutGate(700.0, 1000.0, 48.0);

    // The window turned: the spots are elsewhere and the fingers are no longer
    // on them, so the hold is not carried over.
    CHECK(moved == 1);
    CHECK_FALSE(controller.gateHolding());
    CHECK(controller.gateProgress() == 0.0);
    CHECK_FALSE(controller.unlocked());
}

TEST_CASE("the controller reads the clock it is given, or its own", "[app][parent][gate]") {
    ParentController controller;
    const qint64 own = controller.now();
    CHECK(controller.now() >= own);

    qint64 fixed = 1234;
    controller.useClock([&fixed] { return fixed; });
    CHECK(controller.now() == 1234);
    fixed = 99999;
    CHECK(controller.now() == 99999);
}

// ---------------------------------------------------------------------------
// The child-facing totals

TEST_CASE("the totals the collection screen shows are the store's", "[app][parent][totals]") {
    Fixture f;

    CHECK(f.controller.lifetimeStars() == static_cast<int>(f.store.data().stars));
    CHECK(f.controller.lifetimeStars() > 0);
    CHECK(f.controller.lifetimeWords() == static_cast<int>(f.store.data().vault.size()));
    CHECK(f.controller.boardsKept() == static_cast<int>(f.store.data().boards.size()));
    CHECK(f.controller.boardsKept() > 0);

    int points = 0;
    for (const wjs::VaultWord& word : f.store.data().vault) {
        points += wjs::word_points(word.word) * static_cast<int>(word.found);
    }
    CHECK(f.controller.lifetimePoints() == points);
    CHECK(f.controller.lifetimePoints() > 0);

    ParentController empty;
    CHECK(empty.lifetimeStars() == 0);
    CHECK(empty.lifetimePoints() == 0);
    CHECK(empty.lifetimeWords() == 0);
    CHECK(empty.boardsKept() == 0);
    CHECK(empty.longestWord().isEmpty());
    CHECK(empty.recentBoards().isEmpty());
}

TEST_CASE("the longest word is the longest, and the first alphabetically of equals",
          "[app][parent][totals]") {
    wjs::Store store = wjs::app::demo::empty_store();
    ParentController controller;
    controller.useStore(&store);

    wjs::BoardSnapshot board;
    board.size = 3;
    board.tiles = "A A A/B B B/C C C";
    board.viable_total = 9;
    board.finds = {{"pint", false, friday, 4},
                   {"plate", false, friday, 5},
                   {"crane", false, friday, 5},
                   {"cat", false, friday, 3}};
    static_cast<void>(store.record(board, friday));
    controller.refresh();

    // plate and crane are both five: crane comes first.
    CHECK(controller.longestWord().toLower() == "crane");
}

TEST_CASE("the boards kept are listed newest first, with their words and tiers",
          "[app][parent][totals]") {
    wjs::Store store = wjs::app::demo::empty_store();
    ParentController controller;
    controller.useStore(&store);

    wjs::BoardSnapshot first;
    first.size = 3;
    first.tiles = "S A T/P I N/X Y Z";
    first.viable_total = 4;
    first.finds = {
        {"sat", false, friday, 1}, {"pint", true, friday, 2}, {"paints", false, friday, 3}};
    static_cast<void>(store.record(first, friday));
    wjs::BoardSnapshot second;
    second.size = 3;
    second.tiles = "T A P/S I N/Z Y X";
    second.viable_total = 2;
    static_cast<void>(store.record(second, friday + 1));
    controller.refresh();

    const QVariantList boards = controller.recentBoards();
    REQUIRE(boards.size() == 2);
    // Newest first.
    CHECK(boards.at(0).toMap().value("tiles").toStringList().size() == 9);
    CHECK(boards.at(0).toMap().value("words").toList().isEmpty());
    const QVariantMap older = boards.at(1).toMap();
    CHECK(older.value("rows").toInt() == 3);
    CHECK(older.value("columns").toInt() == 3);
    const QVariantList words = older.value("words").toList();
    REQUIRE(words.size() == 3);
    QMap<QString, QVariantMap> by_word;
    for (const QVariant& word : words) {
        by_word.insert(word.toMap().value("word").toString().toLower(), word.toMap());
    }
    // The tier is the library's: three letters spark, four power, five and more mega.
    CHECK(by_word.value("sat").value("tier").toString() == "spark");
    CHECK(by_word.value("pint").value("tier").toString() == "power");
    CHECK(by_word.value("paints").value("tier").toString() == "mega");
    CHECK(by_word.value("pint").value("assisted").toBool());
    CHECK_FALSE(by_word.value("sat").value("assisted").toBool());
    CHECK(by_word.value("sat").value("points").toInt() == wjs::word_points("sat"));
}

TEST_CASE("the timer lengths on offer are the table's", "[app][parent][settings]") {
    ParentController controller;

    const QVariantList choices = controller.timerLengthChoices();
    REQUIRE(choices.size() == static_cast<qsizetype>(wjs::solo_clock_minutes_choices.size()));
    for (qsizetype i = 0; i < choices.size(); ++i) {
        CHECK(choices.at(i).toInt() ==
              wjs::solo_clock_minutes_choices[static_cast<std::size_t>(i)]);
    }
    CHECK(controller.soloClock() == "off");
    CHECK(controller.soloClockMinutes() == wjs::default_solo_clock_minutes);
}

TEST_CASE("the licence notices are the installed text, or say where it is", "[app][parent]") {
    ParentController controller;

    // The app tests carry no resources, so the fallback is what is read; it names
    // the file a person can open.
    const QString notices = controller.licenceNotices();
    CHECK_FALSE(notices.isEmpty());
    CHECK(notices.contains(QStringLiteral("NOTICES.txt")));
}

TEST_CASE("a null store is not taken, and the old one stays", "[app][parent][store]") {
    Fixture f;
    const QVariantMap before = f.controller.settings();

    f.controller.useStore(nullptr);

    CHECK(f.controller.settings() == before);
    CHECK(f.controller.boardsKept() == static_cast<int>(f.store.data().boards.size()));
}

// ---------------------------------------------------------------------------
// A save that fails, and trying again

TEST_CASE("a save that fails is said, and trying again says when it works",
          "[app][parent][store]") {
    const TempDirectory dir;
    const auto folder = dir.path() / "kept";
    ParentController controller;
    controller.openStore(folder / "store.wjs");
    REQUIRE(controller.storeNote().isEmpty());

    // A file where the folder should be: every write after this fails.
    {
        std::ofstream out(folder, std::ios::binary);
        out << "in the way";
    }
    int notes = 0;
    QObject::connect(&controller, &ParentController::storeNoteChanged, [&notes] { ++notes; });

    REQUIRE(controller.setSetting("boardSize", 4));
    CHECK(controller.storeNote() == "unsaved");
    CHECK(notes == 1);
    // The setting took in memory, and the person is told it was not kept.
    CHECK(controller.settings().value("boardSize").toInt() == 4);

    CHECK_FALSE(controller.retrySave());
    CHECK(controller.storeNote() == "unsaved");
    CHECK(notes == 1);  // said once

    // The obstacle goes; the next try writes, and the note goes with it.
    std::filesystem::remove(folder);
    CHECK(controller.retrySave());
    CHECK(controller.storeNote().isEmpty());
    CHECK(notes == 2);
}

TEST_CASE("a setting that is already what it is does not touch the file", "[app][parent][store]") {
    const TempDirectory dir;
    const auto file = dir.path() / "store.wjs";
    ParentController controller;
    controller.openStore(file);
    REQUIRE(controller.setSetting("help", "start"));
    const auto written = std::filesystem::last_write_time(file);
    const auto size = std::filesystem::file_size(file);

    // The same value again asks nothing of the disk: remove the file and see that
    // it is not written back.
    std::filesystem::remove(file);
    CHECK(controller.setSetting("help", "start"));
    CHECK_FALSE(std::filesystem::exists(file));

    // A different one does.
    CHECK(controller.setSetting("help", "off"));
    CHECK(std::filesystem::exists(file));
    static_cast<void>(written);
    static_cast<void>(size);
}
