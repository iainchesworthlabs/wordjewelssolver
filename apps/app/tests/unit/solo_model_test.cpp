// SPDX-License-Identifier: GPL-3.0-or-later
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <utility>
#include <vector>

#include "fake_solo_game.hpp"
#include "solo_game_factory.hpp"
#include "solo_model.hpp"
#include "test_support.hpp"

namespace {

using wjs::app::FakeSoloGame;
using wjs::app::SoloModel;

// Everything a screen is told, in order.
struct Heard {
    struct Traced {
        QString kind;
        QVariantList path;
        QString word;
        QString tier;
        int stars = 0;
        bool again = false;
        bool new_best = false;
        bool cleared = false;
    };
    struct Cue {
        QString kind;
        QString detail;
    };
    std::vector<Traced> traced;
    std::vector<Cue> cues;
    int state_changes = 0;
    // The QML-visible updates, counted exactly: one pathChanged is one rebuild
    // of the path's display, so the pointer's jank is these numbers growing
    // (docs/design/performance.md, "The window's frames").
    int path_changes = 0;
    int pointer_changes = 0;
    int found_changes = 0;
    int lit_changes = 0;
    int board_changes = 0;
};

// A model over the fake game, with the touch geometry set: tiles of 100 and a gap
// of 10, so the tile at (row, column) has its centre at (50 + 110 c, 50 + 110 r).
struct Fixture {
    SoloModel model{std::make_unique<FakeSoloGame>()};
    Heard heard;

    Fixture() {
        model.setGeometry(100, 10);
        QObject::connect(
            &model, &SoloModel::traced,
            [this](const QString& kind, const QVariantList& path, const QString& word,
                   const QString& tier, int stars, bool again, bool new_best, bool cleared) {
                heard.traced.push_back({kind, path, word, tier, stars, again, new_best, cleared});
            });
        QObject::connect(&model, &SoloModel::cue,
                         [this](const QString& kind, const QString& detail) {
                             heard.cues.push_back({kind, detail});
                         });
        QObject::connect(&model, &SoloModel::stateChanged, [this] { ++heard.state_changes; });
        QObject::connect(&model, &SoloModel::pathChanged, [this] { ++heard.path_changes; });
        QObject::connect(&model, &SoloModel::pointerDownChanged,
                         [this] { ++heard.pointer_changes; });
        QObject::connect(&model, &SoloModel::foundChanged, [this] { ++heard.found_changes; });
        QObject::connect(&model, &SoloModel::litPathChanged, [this] { ++heard.lit_changes; });
        QObject::connect(&model, &SoloModel::boardChanged, [this] { ++heard.board_changes; });
    }

    // Into the box's shake and out of it, as the scene does.
    void play() {
        model.shake();
        model.sceneSettled();
    }

    static double x(int column) { return 50 + 110.0 * column; }
    static double y(int row) { return 50 + 110.0 * row; }

    // A finger down on a tile, sliding across the others in turn, and lifted.
    void trace(std::vector<std::pair<int, int>> tiles) {
        model.pressAt(x(tiles.front().second), y(tiles.front().first));
        for (std::size_t i = 1; i < tiles.size(); ++i) {
            const auto [row, column] = tiles[i];
            model.moveTo(x(column), y(row));
        }
        model.releaseAt(x(tiles.back().second), y(tiles.back().first), true);
    }

    // The tile of the first board as the model hands them to QML.
    //   E N S / O A I / M P T
    void sit() { trace({{0, 2}, {1, 2}, {2, 2}}); }
    void sat() { trace({{0, 2}, {1, 1}, {2, 2}}); }
    void tap() { trace({{2, 2}, {1, 1}, {2, 1}}); }
    void pat() { trace({{2, 1}, {1, 1}, {2, 2}}); }
    void nip() { trace({{0, 1}, {1, 2}, {2, 1}}); }

    // The presentation of a find is the screen's to time: it ends it.
    void shown() { model.wordShown(); }
};

QVariantList indices(std::initializer_list<int> values) {
    QVariantList list;
    for (const int value : values) {
        list.append(value);
    }
    return list;
}

}  // namespace

TEST_CASE("the model starts in the box with nothing on the table", "[app][solo]") {
    Fixture f;

    CHECK(f.model.state() == "box");
    CHECK(f.model.boardSize() == 3);
    CHECK(f.model.sizes() == indices({3, 4}));
    CHECK(f.model.tiles().isEmpty());
    CHECK(f.model.faces().isEmpty());
    CHECK(f.model.up().isEmpty());
    CHECK(f.model.rows() == 0);
    CHECK(f.model.found().isEmpty());
    CHECK(f.model.history().isEmpty());
    CHECK(f.model.helpTile() == -1);
    CHECK(f.model.helpAllowed());
    CHECK(f.model.path().isEmpty());
}

TEST_CASE("the size is chosen in the box and tells the screen", "[app][solo]") {
    Fixture f;
    int changes = 0;
    QObject::connect(&f.model, &SoloModel::sizeChanged, [&] { ++changes; });

    f.model.chooseSize(4);
    CHECK(f.model.boardSize() == 4);
    CHECK(changes == 1);
    f.model.chooseSize(4);  // already: nothing to say
    f.model.chooseSize(7);
    CHECK(changes == 1);

    f.model.shake();
    f.model.chooseSize(3);
    CHECK(f.model.boardSize() == 4);
}

TEST_CASE("a shake deals the board and the scene's end of it starts play", "[app][solo]") {
    Fixture f;
    int dealt = 0;
    QObject::connect(&f.model, &SoloModel::boardChanged, [&] { ++dealt; });

    f.model.shake();

    CHECK(f.model.state() == "shaking");
    CHECK(f.model.rows() == 3);
    CHECK(f.model.columns() == 3);
    CHECK(f.model.tiles() == QStringList{"E", "N", "S", "O", "A", "I", "M", "P", "T"});
    // The fake's board has no dice: the scene draws the letters on the face that
    // is up.
    CHECK(f.model.faces().isEmpty());
    CHECK(f.model.up().isEmpty());
    CHECK(f.model.boardVersion() == 1);
    CHECK(dealt == 1);
    REQUIRE(f.heard.cues.size() == 1);
    CHECK(f.heard.cues[0].kind == "rattle");

    // A second shake does nothing; the scene's word moves the game on.
    f.model.shake();
    CHECK(f.model.boardVersion() == 1);
    f.model.sceneSettled();
    CHECK(f.model.state() == "playing");
    // And a late word from the scene is ignored.
    f.model.sceneSettled();
    CHECK(f.model.state() == "playing");
}

TEST_CASE("nothing is traced before the dice have settled", "[app][solo]") {
    Fixture f;
    f.model.shake();

    f.model.pressAt(Fixture::x(0), Fixture::y(0));
    f.model.releaseAt(Fixture::x(0), Fixture::y(0), true);
    f.model.tapTile(0);

    CHECK(f.model.path().isEmpty());
}

TEST_CASE("a viable word is shown, then the game goes on", "[app][solo]") {
    Fixture f;
    f.play();

    f.sit();

    REQUIRE(f.heard.traced.size() == 1);
    const auto& traced = f.heard.traced[0];
    CHECK(traced.kind == "word");
    CHECK(traced.word == "sit");
    CHECK(traced.tier == "spark");
    CHECK(traced.stars == 1);
    CHECK_FALSE(traced.again);
    CHECK_FALSE(traced.cleared);

    // solo.word-found: the word's path is lit and the path in the making is gone.
    CHECK(f.model.state() == "word-found");
    CHECK(f.model.litPath() == indices({2, 5, 8}));
    CHECK(f.model.path().isEmpty());
    REQUIRE(f.model.found().size() == 1);
    const auto jewel = f.model.found().at(0).toMap();
    CHECK(jewel.value("word").toString() == "sit");
    CHECK(jewel.value("tier").toString() == "spark");
    CHECK(jewel.value("stars").toInt() == 1);

    // The sound of the word is a cue, and the tile letters came first, in order.
    REQUIRE(f.heard.cues.size() >= 5);
    CHECK(f.heard.cues[1].kind == "tile");
    CHECK(f.heard.cues[1].detail == "S");
    CHECK(f.heard.cues[2].detail == "I");
    CHECK(f.heard.cues[3].detail == "T");
    CHECK(f.heard.cues.back().kind == "word");
    CHECK(f.heard.cues.back().detail == "sit");

    // While it is shown the board does not take a new path.
    f.model.pressAt(Fixture::x(0), Fixture::y(0));
    CHECK(f.model.path().isEmpty());

    f.shown();
    CHECK(f.model.state() == "playing");
    CHECK(f.model.litPath().isEmpty());
}

TEST_CASE("a real word that is not today's gets a soft tick and no word", "[app][solo]") {
    Fixture f;
    f.play();

    // TIP, down the right-hand column and across.
    f.trace({{2, 2}, {1, 2}, {2, 1}});

    REQUIRE(f.heard.traced.size() == 1);
    const auto& traced = f.heard.traced[0];
    CHECK(traced.kind == "tick");
    // Nothing the screen is told can show or read the word.
    CHECK(traced.word.isEmpty());
    CHECK(traced.tier.isEmpty());
    CHECK(traced.stars == 0);
    CHECK(f.model.found().isEmpty());
    CHECK(f.model.state() == "playing");
    CHECK(f.heard.cues.back().kind == "tick");
    CHECK(f.heard.cues.back().detail.isEmpty());
}

TEST_CASE("anything else fades with no sound and no word", "[app][solo]") {
    Fixture f;
    f.play();
    const auto cues = f.heard.cues.size();
    const auto tile_cues = 2;  // the two tiles joined

    f.trace({{0, 0}, {0, 1}});

    REQUIRE(f.heard.traced.size() == 1);
    CHECK(f.heard.traced[0].kind == "fade");
    CHECK(f.heard.traced[0].word.isEmpty());
    CHECK(f.heard.traced[0].path == indices({0, 1}));
    // The path's tiles sounded as they joined and nothing else did.
    CHECK(f.heard.cues.size() == cues + tile_cues);
    CHECK(f.model.found().isEmpty());
}

TEST_CASE("tapping in order and pressing Done hands a word in", "[app][solo]") {
    Fixture f;
    f.play();

    f.model.tapTile(2);
    f.model.tapTile(5);
    f.model.tapTile(8);
    CHECK(f.model.path() == indices({2, 5, 8}));
    CHECK_FALSE(f.model.pointerDown());
    CHECK(f.heard.traced.empty());

    f.model.done();

    REQUIRE(f.heard.traced.size() == 1);
    CHECK(f.heard.traced[0].word == "sit");
    CHECK(f.model.path().isEmpty());
}

TEST_CASE("tapping the last tile again hands a word in, and Escape clears", "[app][solo]") {
    Fixture f;
    f.play();

    f.model.tapTile(2);
    f.model.tapTile(5);
    f.model.tapTile(8);
    f.model.tapTile(8);
    CHECK(f.heard.traced.size() == 1);

    f.shown();
    f.model.tapTile(2);
    CHECK_FALSE(f.model.path().isEmpty());
    f.model.clearPath();
    CHECK(f.model.path().isEmpty());
    CHECK(f.heard.traced.size() == 1);
}

TEST_CASE("a word found again is shown again with no new jewel", "[app][solo]") {
    Fixture f;
    f.play();
    f.sit();
    f.shown();

    f.sit();

    REQUIRE(f.heard.traced.size() == 2);
    CHECK(f.heard.traced[1].again);
    CHECK(f.model.found().size() == 1);
}

TEST_CASE("the start tile is one press, and it goes out when its word is found", "[app][solo]") {
    Fixture f;
    f.play();
    int changes = 0;
    QObject::connect(&f.model, &SoloModel::helpChanged, [&] { ++changes; });

    // Nothing lights by itself.
    CHECK(f.model.helpTile() == -1);

    f.model.askHelp();
    CHECK(f.model.helpTile() == 1);  // N, row 0 column 1: NIP
    CHECK(changes == 1);

    f.sit();
    f.shown();
    CHECK(f.model.helpTile() == 1);

    f.nip();
    f.shown();
    CHECK(f.model.helpTile() == -1);
}

TEST_CASE("the parent's switch takes the start tile away", "[app][solo]") {
    auto game = std::make_unique<FakeSoloGame>();
    game->setHelpAllowed(false);
    SoloModel model{std::move(game)};
    model.shake();
    model.sceneSettled();

    CHECK_FALSE(model.helpAllowed());
    model.askHelp();
    CHECK(model.helpTile() == -1);
}

TEST_CASE("the parent's level starts at the two rungs", "[app][solo]") {
    Fixture f;
    CHECK(f.model.helpLevel() == "two");
    CHECK(f.model.helpAllowed());
}

TEST_CASE("the start-tile level stops at the first rung, and the next press hides", "[app][solo]") {
    Fixture f;
    f.play();
    f.model.setHelpLevel("start");
    CHECK(f.model.helpLevel() == "start");

    f.model.askHelp();
    CHECK(f.model.hintLevel() == 1);
    CHECK(f.model.helpTile() == 1);

    // No second rung at this level: the next press puts it away.
    f.model.askHelp();
    CHECK(f.model.hintLevel() == 0);
    CHECK(f.model.helpTile() == -1);
}

TEST_CASE("the level off takes the help away, rings and all", "[app][solo]") {
    Fixture f;
    f.play();
    f.model.askHelp();
    CHECK(f.model.helpTile() == 1);

    f.model.setHelpLevel("off");
    CHECK_FALSE(f.model.helpAllowed());
    CHECK(f.model.helpTile() == -1);
    CHECK(f.model.hintLevel() == 0);

    f.model.askHelp();
    CHECK(f.model.helpTile() == -1);

    // Back on: the help works again.
    f.model.setHelpLevel("two");
    CHECK(f.model.helpAllowed());
    f.model.askHelp();
    CHECK(f.model.helpTile() == 1);
}

TEST_CASE("a hidden help stays hidden until the next press", "[app][solo]") {
    Fixture f;
    f.play();
    f.model.askHelp();  // rung one
    f.model.askHelp();  // rung two
    f.model.askHelp();  // hides: the rings go out, all of them
    CHECK(f.model.hintLevel() == 0);
    CHECK(f.model.helpTile() == -1);

    // A find away from the lit start does not bring the ring back.
    f.sit();
    f.shown();
    CHECK(f.model.helpTile() == -1);

    // The next press lights a word again.
    f.model.askHelp();
    CHECK(f.model.helpTile() == 1);
}

TEST_CASE("a level the table does not have changes nothing", "[app][solo]") {
    Fixture f;
    f.model.setHelpLevel("everything");
    CHECK(f.model.helpLevel() == "two");
}

TEST_CASE("a find made from the lit start answers the help and says what is left", "[app][solo]") {
    Fixture f;
    f.play();
    f.model.askHelp();
    CHECK(f.model.helpTile() == 1);  // N, row 0 column 1: NIP
    CHECK_FALSE(f.model.hintResolved());

    // NIP is the lit word: the rings go out, and nothing else of the board's
    // begins with that tile, which the panel is told.
    f.nip();
    CHECK(f.model.state() == "word-found");
    CHECK(f.model.hintResolved());
    CHECK(f.model.hintRemaining() == 0);
    CHECK(f.model.helpTile() == -1);
    CHECK(f.model.hintLevel() == 0);
}

TEST_CASE("a different word from the lit start answers the help too", "[app][solo]") {
    Fixture f;
    f.play();
    f.nip();  // found without the help
    f.shown();
    f.model.askHelp();
    CHECK(f.model.helpTile() == 2);  // S, row 0 column 2: SAT
    CHECK(f.model.hintLevel() == 1);

    // SIT, another word that begins at the lit tile: the help is answered,
    // and SAT is still to find there.
    f.sit();
    CHECK(f.model.state() == "word-found");
    CHECK(f.model.hintResolved());
    CHECK(f.model.hintRemaining() == 1);
    CHECK(f.model.helpTile() == -1);
}

TEST_CASE("a find away from the lit start leaves the help where it was", "[app][solo]") {
    Fixture f;
    f.play();
    f.model.askHelp();  // NIP, at tile 1
    f.sit();            // SIT begins at tile 2: not the help's start
    CHECK_FALSE(f.model.hintResolved());
    CHECK(f.model.helpTile() == 1);
    CHECK(f.model.hintLevel() == 1);
    f.shown();
}

TEST_CASE("the second rung resolves with the two tiles it lit", "[app][solo]") {
    Fixture f;
    f.play();
    f.model.askHelp();  // rung one: tile 1
    f.model.askHelp();  // rung two: NIP's first two tiles
    CHECK(f.model.hintLevel() == 2);

    f.nip();
    CHECK(f.model.state() == "word-found");
    CHECK(f.model.hintResolved());
    CHECK(f.model.hintRemaining() == 0);
    CHECK(f.model.helpTiles().isEmpty());
    CHECK(f.model.helpTile() == -1);
}

TEST_CASE("the next press starts a fresh question", "[app][solo]") {
    Fixture f;
    f.play();
    f.model.askHelp();
    f.nip();
    CHECK(f.model.hintResolved());

    // NIP is gone; the next question is a new word, and the old answer is
    // put away.
    f.shown();
    f.model.askHelp();
    CHECK(f.model.helpTile() == 2);  // S: SAT
    CHECK_FALSE(f.model.hintResolved());
    CHECK(f.model.hintRemaining() == -1);
}

TEST_CASE("tiles that make one sound are one block as the path grows", "[app][solo]") {
    Fixture f;
    f.play();

    f.model.pressAt(Fixture::x(1), Fixture::y(1));  // A
    f.model.moveTo(Fixture::x(2), Fixture::y(1));   // I
    const auto blocks = f.model.blocks();

    REQUIRE(blocks.size() == 1);
    const auto block = blocks.at(0).toMap();
    CHECK(block.value("first").toInt() == 0);
    CHECK(block.value("tiles").toInt() == 2);
    CHECK(block.value("grapheme").toString() == "ai");
    CHECK(f.model.pointerDown());

    f.model.releaseAt(Fixture::x(2), Fixture::y(1), false);
    CHECK(f.model.path().isEmpty());
    CHECK(f.model.blocks().isEmpty());
}

TEST_CASE("a dead jewel cannot be touched", "[app][solo]") {
    Fixture f;
    f.model.chooseSize(4);
    f.model.shake();
    f.model.sceneSettled();
    f.model.setGeometry(100, 10);

    const auto tiles = f.model.tiles();
    const int dead = static_cast<int>(tiles.indexOf(QString{}));
    REQUIRE(dead >= 0);
    const int row = dead / 4;
    const int column = dead % 4;
    f.model.pressAt(Fixture::x(column), Fixture::y(row));
    f.model.releaseAt(Fixture::x(column), Fixture::y(row), true);
    f.model.tapTile(dead);

    CHECK(f.model.path().isEmpty());
}

TEST_CASE("the last word clears the board, which celebrates once and then rests", "[app][solo]") {
    Fixture f;
    f.play();
    for (auto word : {&Fixture::sit, &Fixture::sat, &Fixture::tap, &Fixture::pat}) {
        (f.*word)();
        f.shown();
        CHECK(f.model.state() == "playing");
    }

    f.nip();

    REQUIRE(f.heard.traced.size() == 5);
    CHECK(f.heard.traced.back().cleared);
    CHECK(f.heard.traced.back().new_best);
    // The word is shown first; the quiet celebration follows it.
    CHECK(f.model.state() == "word-found");
    f.shown();
    CHECK(f.model.state() == "cleared");
    CHECK(f.heard.cues.back().kind == "cleared");

    // The celebration cannot be cut short from outside its own end.
    f.model.celebrated();
    CHECK(f.model.state() == "resting");

    // Resting saves the board to history, which the child may open.
    REQUIRE(f.model.history().size() == 1);
    CHECK(f.model.history().at(0).toMap().value("found").toList().size() == 5);

    // Nothing on the resting screen comes back by itself: only Again leaves it.
    f.model.shake();
    CHECK(f.model.state() == "resting");
    f.model.again();
    CHECK(f.model.state() == "box");
}

TEST_CASE("putting the lid back on saves the board and clears what was in the making",
          "[app][solo]") {
    Fixture f;
    f.play();
    f.sit();
    f.shown();
    f.model.askHelp();
    f.model.tapTile(0);

    f.model.lidOn();

    CHECK(f.model.state() == "box");
    CHECK(f.model.path().isEmpty());
    CHECK(f.model.helpTile() == -1);
    REQUIRE(f.model.history().size() == 1);
    const auto entry = f.model.history().at(0).toMap();
    CHECK(entry.value("rows").toInt() == 3);
    CHECK(entry.value("tiles").toStringList().size() == 9);
    CHECK(entry.value("found").toList().size() == 1);
}

TEST_CASE("the lid can go back on mid-shake, and nothing is saved", "[app][solo]") {
    Fixture f;
    f.model.shake();

    f.model.lidOn();

    CHECK(f.model.state() == "box");
    CHECK(f.model.history().isEmpty());
}

// --- the board's clock ---------------------------------------------------------------

TEST_CASE("the board's clock is off until the parent sets one, and ticks only while active",
          "[app][solo][clock]") {
    Fixture f;
    CHECK(f.model.clockMode() == "off");
    CHECK(f.model.clockMinutes() == wjs::default_solo_clock_minutes);
    CHECK(f.model.clockText() == "0:00");

    f.model.setClockMode("up");
    CHECK(f.model.clockMode() == "up");
    f.play();
    // Not active: a tick the screen never sent changes nothing.
    f.model.tickClock();
    CHECK(f.model.clockSeconds() == 0);

    f.model.setClockActive(true);
    f.model.tickClock();
    f.model.tickClock();
    CHECK(f.model.clockSeconds() == 2);
    CHECK(f.model.clockText() == "0:02");

    // Inactive again: the seconds hold, and no more tick.
    f.model.setClockActive(false);
    f.model.tickClock();
    CHECK(f.model.clockSeconds() == 2);
}

TEST_CASE("a count-down board starts with its minutes and ends the round at zero",
          "[app][solo][clock]") {
    Fixture f;
    int expired = 0;
    QObject::connect(&f.model, &SoloModel::clockExpired, [&] { ++expired; });

    f.model.setClockMode("down");
    f.model.setClockMinutes(1);
    f.play();
    CHECK(f.model.clockSeconds() == 60);
    CHECK(f.model.clockText() == "1:00");

    f.model.setClockActive(true);
    for (int i = 0; i < 59; ++i) {
        f.model.tickClock();
    }
    CHECK(f.model.clockSeconds() == 1);
    CHECK(expired == 0);

    f.model.tickClock();
    CHECK(f.model.clockSeconds() == 0);
    CHECK(expired == 1);

    // Ended once, it does not end again.
    f.model.tickClock();
    CHECK(expired == 1);
}

TEST_CASE("each new board starts its clock again", "[app][solo][clock]") {
    Fixture f;
    f.model.setClockMode("up");
    f.play();
    f.model.setClockActive(true);
    f.model.tickClock();
    CHECK(f.model.clockSeconds() == 1);

    // The lid puts the board, and its clock, away.
    f.model.lidOn();
    CHECK(f.model.clockSeconds() == 0);

    f.model.shake();
    f.model.sceneSettled();
    f.model.tickClock();
    CHECK(f.model.clockSeconds() == 1);
}

TEST_CASE("a clock set while a board is in play starts from the board's beginning",
          "[app][solo][clock]") {
    Fixture f;
    f.play();
    f.model.setClockActive(true);
    f.model.tickClock();
    CHECK(f.model.clockSeconds() == 0);  // off: nothing to count

    f.model.setClockMode("up");
    CHECK(f.model.clockSeconds() == 0);
    f.model.tickClock();
    CHECK(f.model.clockSeconds() == 1);
}

TEST_CASE("the clock's own timer counts the time that really went by", "[app][solo][clock]") {
    wjs::app::test::CoreApplication application;
    Fixture f;
    f.model.setClockMode("up");
    f.play();
    int changes = 0;
    QObject::connect(&f.model, &SoloModel::clockSecondsChanged, [&changes] { ++changes; });

    // Not active: the timer is not running and the clock does not move.
    CHECK_FALSE(wjs::app::test::spin_until([&f] { return f.model.clockSeconds() > 0; }, 1300));
    CHECK(changes == 0);

    // Active: the model's own timer counts seconds of wall time, with no call to
    // tickClock. One real second and a margin is the cost of proving it.
    f.model.setClockActive(true);
    REQUIRE(wjs::app::test::spin_until([&f] { return f.model.clockSeconds() >= 1; }, 3000));
    CHECK(f.model.clockSeconds() == 1);
    CHECK(changes == 1);
    CHECK(f.model.clockText() == "0:01");

    // Inactive again: it stops, and the seconds stay.
    f.model.setClockActive(false);
    const int held = f.model.clockSeconds();
    CHECK_FALSE(
        wjs::app::test::spin_until([&f, held] { return f.model.clockSeconds() != held; }, 1300));
}

TEST_CASE("a stalled interface does not lose the clock's time", "[app][solo][clock]") {
    Fixture f;
    f.model.setClockMode("up");
    f.play();
    f.model.setClockActive(true);

    // The timer fires late, once, after three and a half seconds: the clock reads
    // what went by and not the one tick it was told of.
    f.model.advanceClock(3500);
    CHECK(f.model.clockSeconds() == 3);
    f.model.advanceClock(600);
    CHECK(f.model.clockSeconds() == 4);

    // A count-down reads the time left rounded up, and ends when none is.
    Fixture g;
    int expired = 0;
    QObject::connect(&g.model, &SoloModel::clockExpired, [&expired] { ++expired; });
    g.model.setClockMode("down");
    g.model.setClockMinutes(1);
    g.play();
    g.model.setClockActive(true);
    g.model.advanceClock(500);
    CHECK(g.model.clockSeconds() == 60);
    g.model.advanceClock(1500);
    CHECK(g.model.clockSeconds() == 58);
    g.model.advanceClock(1000000);
    CHECK(g.model.clockSeconds() == 0);
    CHECK(expired == 1);
}

TEST_CASE("changing the clock mid-round keeps the time already played", "[app][solo][clock]") {
    Fixture f;
    f.model.setClockMode("up");
    f.play();
    f.model.setClockActive(true);
    f.model.advanceClock(30000);
    REQUIRE(f.model.clockSeconds() == 30);

    // The parent's count-down takes over the same board: 30 of its 60 seconds are
    // gone, and the clock does not start again from the full minute.
    f.model.setClockMode("down");
    f.model.setClockMinutes(1);
    CHECK(f.model.clockSeconds() == 30);

    // A length that cannot be a day of seconds is refused rather than overflowing.
    f.model.setClockMinutes(2147483647);
    CHECK(f.model.clockMinutes() == 1);
    f.model.setClockMinutes(-5);
    CHECK(f.model.clockMinutes() == 1);
}

TEST_CASE("a model made with no game is a box that cannot be shaken", "[app][solo]") {
    SoloModel model;

    CHECK(model.state() == "box");
    CHECK(model.sizes() == indices({3}));
    model.shake();
    CHECK(model.state() == "box");
    CHECK(model.rows() == 0);
    CHECK_FALSE(model.helpAllowed());
    CHECK_FALSE(model.saveFailed());
}

// --- what the parent changes -------------------------------------------------------

namespace {

// What a program's maker makes after the parent switched the help off and chose the
// 4x4 to be offered first, and how many it has made.
struct ParentChoices {
    int made = 0;
    wjs::app::ScopedSoloGameMaker installed{[this] {
        ++made;
        auto game = std::make_unique<FakeSoloGame>();
        game->setHelpAllowed(false);
        game->chooseSize(4);
        return game;
    }};
};

}  // namespace

TEST_CASE("an edit by the parent gives a new game at once when the box has its lid on",
          "[app][solo][edited]") {
    ParentChoices chosen;
    Fixture f;
    f.play();
    f.model.lidOn();
    REQUIRE(f.model.state() == "box");
    CHECK(f.model.helpAllowed());
    CHECK(f.model.boardSize() == 3);
    int states = 0;
    QObject::connect(&f.model, &SoloModel::stateChanged, [&] { ++states; });

    f.model.storeEdited();

    CHECK(chosen.made == 1);
    CHECK_FALSE(f.model.helpAllowed());
    CHECK(f.model.boardSize() == 4);
    CHECK(f.model.state() == "box");
    CHECK(f.model.tiles().isEmpty());
    CHECK(f.model.found().isEmpty());
    CHECK(states == 1);
    // And it plays like any other.
    f.model.shake();
    CHECK(f.model.state() == "shaking");
    CHECK(f.model.rows() == 4);
}

TEST_CASE("a game in play is not taken from a child: the next one is made when the lid is on",
          "[app][solo][edited]") {
    ParentChoices chosen;
    Fixture f;
    f.play();
    const QStringList tiles = f.model.tiles();

    f.model.storeEdited();

    CHECK(chosen.made == 0);
    CHECK(f.model.state() == "playing");
    CHECK(f.model.tiles() == tiles);
    CHECK(f.model.helpAllowed());

    f.model.lidOn();

    CHECK(chosen.made == 1);
    CHECK(f.model.state() == "box");
    CHECK_FALSE(f.model.helpAllowed());
}

TEST_CASE("a game that is resting is replaced by Again", "[app][solo][edited]") {
    ParentChoices chosen;
    Fixture f;
    f.play();
    for (auto word : {&Fixture::sit, &Fixture::sat, &Fixture::tap, &Fixture::pat, &Fixture::nip}) {
        (f.*word)();
        f.shown();
    }
    f.model.celebrated();
    REQUIRE(f.model.state() == "resting");

    f.model.storeEdited();
    CHECK(chosen.made == 0);
    CHECK(f.model.state() == "resting");

    f.model.again();
    CHECK(chosen.made == 1);
    CHECK(f.model.state() == "box");
    CHECK_FALSE(f.model.helpAllowed());
}

TEST_CASE("with no edit the game goes on as it is, whatever the maker would make",
          "[app][solo][edited]") {
    ParentChoices chosen;
    Fixture f;
    f.play();
    f.model.lidOn();
    f.model.shake();
    f.model.lidOn();

    CHECK(chosen.made == 0);
    CHECK(f.model.helpAllowed());
}

TEST_CASE("a new game that was brought back is shown as the constructor shows one",
          "[app][solo][edited]") {
    int made = 0;
    const wjs::app::ScopedSoloGameMaker installed([&made] {
        ++made;
        auto game = std::make_unique<FakeSoloGame>();
        game->shake();
        game->settled();
        return game;
    });
    Fixture f;
    const int version = f.model.boardVersion();

    f.model.storeEdited();

    CHECK(made == 1);
    CHECK(f.model.state() == "playing");
    CHECK(f.model.rows() == 3);
    CHECK(f.model.tiles().size() == 9);
    CHECK(f.model.boardVersion() == version + 1);
}

// --- the shape of the updates (what the screens rebuild) ---------------------------
// One pathChanged is one rebuild of the path's display: the path lines, each
// tile's lit test and the action bar's word. The pointer's jank is these
// numbers growing, so they are held exactly (docs/design/performance.md).

TEST_CASE("a trace tells the path once per change and once at the hand-in", "[app][solo]") {
    Fixture f;
    f.play();
    const int path = f.heard.path_changes;
    const int pointer = f.heard.pointer_changes;
    const int found = f.heard.found_changes;

    f.sit();  // press on S, two moves, lift: tiles 2, 5, 8.

    // One a tile (3), one for the tracer's clear at the hand-in (1); a press
    // told the path once and the finger's down and up told the pointer only.
    CHECK(f.heard.path_changes - path == 4);
    CHECK(f.heard.pointer_changes - pointer == 2);
    // A find is one tray update, one lit path, one state, one trace.
    CHECK(f.heard.found_changes - found == 1);
    REQUIRE(f.heard.traced.size() == 1);
    CHECK(f.heard.traced[0].word == "sit");
}

TEST_CASE("a press on nothing starts no trace and rebuilds nothing", "[app][solo]") {
    Fixture f;
    f.play();
    const int path = f.heard.path_changes;
    const int pointer = f.heard.pointer_changes;

    // Far outside every tile: the tracer ignores the press, so the finger is
    // never down and neither the path's display nor the pointer's is rebuilt.
    f.model.pressAt(-500, -500);
    CHECK(f.heard.path_changes - path == 0);
    CHECK(f.heard.pointer_changes - pointer == 0);

    f.model.releaseAt(-500, -500, false);
    CHECK(f.heard.path_changes - path == 0);
    CHECK(f.heard.pointer_changes - pointer == 0);
    CHECK(f.model.path().isEmpty());
}

TEST_CASE("a move that changes nothing rebuilds nothing", "[app][solo]") {
    Fixture f;
    f.play();
    const int path = f.heard.path_changes;
    const int pointer = f.heard.pointer_changes;

    f.model.pressAt(Fixture::x(0), Fixture::y(0));  // tile 0
    const int after_press = f.heard.path_changes;
    CHECK(after_press - path == 1);  // one tile, told once

    // A wobble inside the tile's stay circle: no new tile, no signal.
    f.model.moveTo(Fixture::x(0) + 3, Fixture::y(0));
    CHECK(f.heard.path_changes - after_press == 0);
    CHECK(f.heard.pointer_changes - pointer == 1);

    // A tile that does not touch the last is ignored, as a non-change.
    f.model.moveTo(Fixture::x(1), Fixture::y(1));
    CHECK(f.heard.path_changes - after_press == 1);  // the diagonal tile joins
}

TEST_CASE("a tapped word is told once a tile, and the tray once at Done", "[app][solo]") {
    Fixture f;
    f.play();
    const int path = f.heard.path_changes;
    const int pointer = f.heard.pointer_changes;
    const int found = f.heard.found_changes;

    f.model.tapTile(2);
    f.model.tapTile(5);
    f.model.tapTile(8);
    CHECK(f.heard.path_changes - path == 3);
    CHECK(f.heard.pointer_changes - pointer == 0);

    f.model.done();
    CHECK(f.heard.path_changes - path == 4);  // the tracer's clear
    CHECK(f.heard.found_changes - found == 1);
    CHECK(f.model.state() == "word-found");
}

TEST_CASE("the path, the tiles and the tray are built once and read many times", "[app][solo]") {
    Fixture f;
    f.play();
    f.model.tapTile(2);
    f.model.tapTile(5);

    // Two reads give the same stored list, not two rebuilds: the screens read
    // the path once a tile and once a path line per move, and a rebuild a read
    // is the pointer's jank.
    const QVariantList first_path = f.model.path();
    const QVariantList second_path = f.model.path();
    CHECK(first_path.constData() == second_path.constData());

    const QStringList first_tiles = f.model.tiles();
    const QStringList second_tiles = f.model.tiles();
    CHECK(first_tiles.constData() == second_tiles.constData());

    const QVariantList first_found = f.model.found();
    const QVariantList second_found = f.model.found();
    CHECK(first_found.constData() == second_found.constData());

    // And the cached path is the path: content is not sacrificed for the cache.
    CHECK(f.model.path() == indices({2, 5}));
    f.model.tapTile(8);
    CHECK(f.model.path() == indices({2, 5, 8}));
}
