// SPDX-License-Identifier: GPL-3.0-or-later
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "assist_game.hpp"
#include "assist_model.hpp"
#include "test_support.hpp"
#include "wjs/calendar.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/settings.hpp"
#include "wjs/store.hpp"

// The Family Assistant's model (apps/app/src/scan/assist_model.*): what the screen
// binds to over a confirmed board. The game's rules are AssistGame's case
// (assist_game_test.cpp); this holds down what the model adds: the refusals of
// begin(), that a round is never lost to a board that cannot start, recording on
// leaving, the touch input, the cues and the traced moments, the help, and the
// history and the save status the screen reads.
//
// TEST DATA. The words are made up for these tests and are not content for a
// child.

namespace {

using wjs::app::AssistGame;
using wjs::app::AssistModel;

constexpr wjs::Day today = wjs::day_from_civil(2026, 10, 19);

wjs::Dictionary dictionary_with(std::initializer_list<std::string_view> words) {
    wjs::Dictionary dictionary;
    for (const std::string_view word : words) {
        static_cast<void>(dictionary.insert(word));
    }
    return dictionary;
}

// The 3x3 the cases play, the same as AssistGame's:
//
//   S A T
//   P I N
//   X Y Z
//
// SAT, SIT, PINT, TAP and PIN are traceable on it.
const QStringList the_tiles{"S", "A", "T", "P", "I", "N", "X", "Y", "Z"};

// A model over a store of its own, with the dictionary the board is played in.
// `active` is what counts; the rest of the dictionary only tells a real word from
// a string of letters.
struct Fixture {
    Fixture()
        : dictionary(dictionary_with({"SAT", "SIT", "PINT", "TAP", "PIN", "NIP"})),
          active(dictionary_with({"SAT", "SIT", "PINT"})),
          store(wjs::Store::in_memory()) {
        model = std::make_unique<AssistModel>([this](wjs::Board board) {
            ++made;
            if (refuse) {
                return std::unique_ptr<AssistGame>{};
            }
            return std::make_unique<AssistGame>(std::move(board), dictionary, active, store,
                                                [] { return today; });
        });
    }

    wjs::Dictionary dictionary;
    wjs::Dictionary active;
    wjs::Store store;
    bool refuse = false;
    int made = 0;
    std::unique_ptr<AssistModel> model;
};

struct Signals {
    int game = 0;
    int board = 0;
    int path = 0;
    int found = 0;
    int history = 0;
    int help = 0;
    int save_failed = 0;
    std::vector<QString> cues;
    std::vector<QString> details;
    std::vector<QString> traced_kinds;
    QVariantList last_traced_path;
    QString last_word;
    QString last_tier;
    int last_stars = -1;
    bool last_again = false;

    explicit Signals(AssistModel& model) {
        QObject::connect(&model, &AssistModel::gameChanged, &model, [this] { ++game; });
        QObject::connect(&model, &AssistModel::boardChanged, &model, [this] { ++board; });
        QObject::connect(&model, &AssistModel::pathChanged, &model, [this] { ++path; });
        QObject::connect(&model, &AssistModel::foundChanged, &model, [this] { ++found; });
        QObject::connect(&model, &AssistModel::historyChanged, &model, [this] { ++history; });
        QObject::connect(&model, &AssistModel::helpChanged, &model, [this] { ++help; });
        QObject::connect(&model, &AssistModel::saveFailedChanged, &model,
                         [this] { ++save_failed; });
        QObject::connect(&model, &AssistModel::cue, &model,
                         [this](const QString& kind, const QString& detail) {
                             cues.push_back(kind);
                             details.push_back(detail);
                         });
        QObject::connect(&model, &AssistModel::traced, &model,
                         [this](const QString& kind, const QVariantList& traced_path,
                                const QString& word, const QString& tier, int stars, bool again) {
                             traced_kinds.push_back(kind);
                             last_traced_path = traced_path;
                             last_word = word;
                             last_tier = tier;
                             last_stars = stars;
                             last_again = again;
                         });
    }
};

void tap_all(AssistModel& model, std::initializer_list<int> tiles) {
    for (const int tile : tiles) {
        model.tapTile(tile);
    }
}

}  // namespace

TEST_CASE("begin refuses a grid that is not a board, and nothing starts", "[app][scan][assist]") {
    Fixture fixture;
    AssistModel& model = *fixture.model;

    CHECK_FALSE(model.begin(0, 3, the_tiles));
    CHECK_FALSE(model.begin(3, 0, the_tiles));
    CHECK_FALSE(model.begin(-1, -1, the_tiles));
    CHECK_FALSE(model.begin(3, 3, QStringList{"S", "A"}));  // not rows * columns tiles
    CHECK_FALSE(model.begin(2, 2, the_tiles));
    CHECK_FALSE(model.begin(1000000, 1000000, the_tiles));  // wraps nowhere, makes nothing
    QStringList with_empty = the_tiles;
    with_empty[4] = QString();
    CHECK_FALSE(model.begin(3, 3, with_empty));

    CHECK_FALSE(model.active());
    CHECK(fixture.made == 0);
}

TEST_CASE("a tile holding a space or a slash cannot change the grid's shape",
          "[app][scan][assist]") {
    Fixture fixture;
    AssistModel& model = *fixture.model;

    // "A B" would read as two tiles, and "A/B" as two rows: the board that came
    // back would not be the 1x2 asked for (or would not be a board at all).
    CHECK_FALSE(model.begin(1, 2, QStringList{"A B", "C"}));
    CHECK_FALSE(model.begin(1, 2, QStringList{"A/B", "C"}));
    CHECK_FALSE(model.begin(2, 2, QStringList{"A B", "C", "D", "E"}));
    CHECK_FALSE(model.begin(3, 3, QStringList{"S A", "T", "P", "I", "N", "X", "Y", "Z", "Q"}));
    CHECK_FALSE(model.active());
    CHECK(fixture.made == 0);
}

TEST_CASE("without a maker there is no round", "[app][scan][assist]") {
    AssistModel model{wjs::app::AssistGameMaker{}};
    CHECK_FALSE(model.begin(3, 3, the_tiles));
    CHECK_FALSE(model.active());
}

TEST_CASE("begin makes the round and tells the screen", "[app][scan][assist]") {
    Fixture fixture;
    AssistModel& model = *fixture.model;
    Signals seen(model);

    REQUIRE(model.begin(3, 3, the_tiles));
    CHECK(model.active());
    CHECK(model.rows() == 3);
    CHECK(model.columns() == 3);
    CHECK(model.tiles() == the_tiles);
    CHECK(model.path().isEmpty());
    CHECK(model.found().isEmpty());
    CHECK(model.stars() == 0);
    CHECK(model.litTile() == -1);

    CHECK(seen.game == 1);
    CHECK(seen.board == 1);
    CHECK(seen.path == 1);
    CHECK(seen.found == 1);
    CHECK(seen.history == 1);
    CHECK(seen.help == 1);
}

TEST_CASE("a board that cannot be made leaves the running round as it was", "[app][scan][assist]") {
    Fixture fixture;
    AssistModel& model = *fixture.model;
    REQUIRE(model.begin(3, 3, the_tiles));
    tap_all(model, {0, 1, 2});
    model.done();  // SAT
    REQUIRE(model.found().size() == 1);

    Signals seen(model);
    fixture.refuse = true;
    const QStringList other{"A", "B", "C", "D"};
    CHECK_FALSE(model.begin(2, 2, other));

    // Still the family's board, with its tray, and not yet recorded: it is not
    // finished, and nothing told the screen that anything changed.
    CHECK(model.active());
    CHECK(model.tiles() == the_tiles);
    CHECK(model.found().size() == 1);
    CHECK(fixture.store.data().boards.empty());
    CHECK(seen.game == 0);
    CHECK(seen.board == 0);
    CHECK(seen.found == 0);

    // And the round goes on being played.
    fixture.refuse = false;
    tap_all(model, {0, 1, 4});
    model.done();  // SAI is not a word; the round is alive and answers
    CHECK(model.found().size() == 1);
    CHECK(seen.traced_kinds.back() == "fade");
}

TEST_CASE("a new round records the one it replaces, once", "[app][scan][assist]") {
    Fixture fixture;
    AssistModel& model = *fixture.model;
    REQUIRE(model.begin(3, 3, the_tiles));
    tap_all(model, {0, 1, 2});
    model.done();  // SAT
    REQUIRE(model.found().size() == 1);
    CHECK(fixture.store.data().boards.empty());

    const QStringList next{"A", "B", "C", "D", "E", "F", "G", "H", "I"};
    REQUIRE(model.begin(3, 3, next));
    REQUIRE(fixture.store.data().boards.size() == 1);
    CHECK(model.tiles() == next);
    CHECK(model.found().isEmpty());

    // The history now holds the board that was played, with its word.
    const QVariantList history = model.history();
    REQUIRE(history.size() == 1);
    const QVariantMap entry = history.constFirst().toMap();
    CHECK(entry.value("rows").toInt() == 3);
    CHECK(entry.value("columns").toInt() == 3);
    CHECK(entry.value("tiles").toStringList() == the_tiles);
    const QVariantList words = entry.value("found").toList();
    REQUIRE(words.size() == 1);
    CHECK(words.constFirst().toMap().value("word").toString() == "sat");
    CHECK(words.constFirst().toMap().value("tier").toString() == "spark");
    CHECK(words.constFirst().toMap().value("stars").toInt() > 0);
}

TEST_CASE("leaving records the board once, and the destructor records a round not left",
          "[app][scan][assist]") {
    Fixture fixture;
    REQUIRE(fixture.model->begin(3, 3, the_tiles));
    fixture.model->leave();
    CHECK(fixture.store.data().boards.size() == 1);
    fixture.model->leave();
    CHECK(fixture.store.data().boards.size() == 1);
    // The tray stays readable after the round is left.
    CHECK(fixture.model->active());
    CHECK(fixture.model->tiles() == the_tiles);

    Fixture other;
    REQUIRE(other.model->begin(3, 3, the_tiles));
    CHECK(other.store.data().boards.empty());
    other.model.reset();
    CHECK(other.store.data().boards.size() == 1);
}

TEST_CASE("tapping a tile out of range, and every touch with no round, does nothing",
          "[app][scan][assist]") {
    Fixture fixture;
    AssistModel& model = *fixture.model;
    Signals seen(model);

    // No round: nothing to touch.
    model.setGeometry(50, 10);
    model.pressAt(10, 10);
    model.moveTo(70, 10);
    model.releaseAt(70, 10, true);
    model.tapTile(0);
    model.done();
    model.askHelp();
    CHECK(seen.path == 0);
    CHECK(seen.cues.empty());
    CHECK(seen.traced_kinds.empty());

    REQUIRE(model.begin(3, 3, the_tiles));
    const int paths = seen.path;
    model.tapTile(-1);
    model.tapTile(9);
    model.tapTile(1000);
    CHECK(model.path().isEmpty());
    CHECK(seen.path == paths);
    CHECK(seen.cues.empty());
}

TEST_CASE("each tile that joins the path sounds its letters, and a word cues the word",
          "[app][scan][assist]") {
    Fixture fixture;
    AssistModel& model = *fixture.model;
    REQUIRE(model.begin(3, 3, the_tiles));
    Signals seen(model);

    tap_all(model, {0, 1, 2});
    REQUIRE(seen.cues.size() == 3);
    CHECK(seen.cues[0] == "tile");
    CHECK(seen.details[0] == "S");
    CHECK(seen.details[1] == "A");
    CHECK(seen.details[2] == "T");
    CHECK(model.path() == QVariantList{0, 1, 2});

    model.done();
    // The path is handed in and cleared; the word is a jewel in the tray.
    CHECK(model.path().isEmpty());
    REQUIRE(seen.traced_kinds.size() == 1);
    CHECK(seen.traced_kinds[0] == "word");
    CHECK(seen.last_word == "sat");
    CHECK(seen.last_tier == "spark");
    CHECK(seen.last_stars > 0);
    CHECK_FALSE(seen.last_again);
    CHECK(seen.last_traced_path == QVariantList{0, 1, 2});
    REQUIRE(seen.cues.size() == 4);
    CHECK(seen.cues[3] == "word");
    CHECK(seen.details[3] == "sat");
    REQUIRE(model.found().size() == 1);
    CHECK(model.found().constFirst().toMap().value("word").toString() == "sat");
    CHECK(model.stars() == seen.last_stars);

    // The same word again earns nothing new, and says so.
    tap_all(model, {0, 1, 2});
    model.done();
    CHECK(seen.last_again);
    CHECK(model.found().size() == 1);
}

TEST_CASE("a real word that is not today's ticks without a word, and a string fades silently",
          "[app][scan][assist]") {
    Fixture fixture;
    AssistModel& model = *fixture.model;
    REQUIRE(model.begin(3, 3, the_tiles));
    Signals seen(model);

    // TAP is in the dictionary and not in today's set.
    tap_all(model, {2, 1, 3});
    model.done();
    REQUIRE(seen.traced_kinds.size() == 1);
    CHECK(seen.traced_kinds[0] == "tick");
    CHECK(seen.last_word.isEmpty());
    CHECK(seen.last_tier.isEmpty());
    CHECK(seen.last_stars == 0);
    CHECK_FALSE(seen.last_again);
    CHECK(seen.cues.back() == "tick");
    CHECK(model.found().isEmpty());

    const std::size_t cues_before = seen.cues.size();
    tap_all(model, {0, 1, 3});  // S A P is not a word
    const std::size_t after_taps = seen.cues.size();
    model.done();
    REQUIRE(seen.traced_kinds.size() == 2);
    CHECK(seen.traced_kinds[1] == "fade");
    // A fade has no cue of its own: the only cues are the tiles'.
    CHECK(after_taps == cues_before + 3);
    CHECK(seen.cues.size() == after_taps);
}

TEST_CASE("a path can be traced with a finger and removed tile by tile", "[app][scan][assist]") {
    Fixture fixture;
    AssistModel& model = *fixture.model;
    REQUIRE(model.begin(3, 3, the_tiles));
    model.setGeometry(50, 10);
    Signals seen(model);

    // Tile (row, column) is centred at (column * 60 + 25, row * 60 + 25).
    model.pressAt(25, 25);
    CHECK(model.pointerDown());
    model.moveTo(85, 25);
    model.moveTo(145, 25);
    CHECK(model.path() == QVariantList{0, 1, 2});
    model.releaseAt(145, 25, true);
    // Lifting inside the board hands the path in; the finger is up.
    CHECK_FALSE(model.pointerDown());
    REQUIRE(seen.traced_kinds.size() == 1);
    CHECK(seen.traced_kinds[0] == "word");

    tap_all(model, {3, 4});
    CHECK(model.path() == QVariantList{3, 4});
    model.removeLast();
    CHECK(model.path() == QVariantList{3});
    model.clearPath();
    CHECK(model.path().isEmpty());
    // Nothing left to remove is not an error.
    model.removeLast();
    model.clearPath();
    CHECK(model.path().isEmpty());
}

TEST_CASE("the start tile is offered only while the parent allows it", "[app][scan][assist]") {
    Fixture fixture;
    AssistModel& model = *fixture.model;
    REQUIRE(model.begin(3, 3, the_tiles));
    REQUIRE(model.helpAllowed());
    Signals seen(model);

    model.askHelp();
    CHECK(model.litTile() == 0);  // the shortest word's first tile: SAT or SIT, S at the top left
    CHECK(seen.help == 1);
    model.askHelp();
    CHECK(model.litTile() == 0);  // the same tile, no new change

    // A find clears the lit word; the screen is told.
    tap_all(model, {0, 1, 2});
    model.done();
    CHECK(model.litTile() == -1);
    CHECK(seen.help == 2);

    // The parent turns the help off: the model offers nothing and shows nothing.
    wjs::Settings off = fixture.store.settings();
    off.help = wjs::HelpLevel::off;
    fixture.store.update_settings(off);
    CHECK_FALSE(model.helpAllowed());
    const int helps = seen.help;
    model.askHelp();
    CHECK(model.litTile() == -1);
    CHECK(seen.help == helps);
}

TEST_CASE("a board the store cannot keep is told as a save failure, once", "[app][scan][assist]") {
    wjs::app::test::TempDirectory directory{"wjs_assist_model_test"};
    // The store's folder does not exist yet, so the store opens as created and
    // writes when it first has something; a file in the folder's place then makes
    // every write fail.
    const std::filesystem::path blocked = directory.path() / "blocked";
    wjs::OpenResult opened = wjs::Store::open(blocked / "store.txt");
    REQUIRE(opened.store.persistent());
    {
        std::ofstream file(blocked);
        file << "not a folder";
    }

    const wjs::Dictionary dictionary = dictionary_with({"SAT", "SIT"});
    AssistModel model([&](wjs::Board board) {
        return std::make_unique<AssistGame>(std::move(board), dictionary, opened.store,
                                            [] { return today; });
    });
    Signals seen(model);
    REQUIRE(model.begin(3, 3, the_tiles));
    CHECK_FALSE(model.saveFailed());

    model.leave();
    CHECK(model.saveFailed());
    CHECK(seen.save_failed == 1);
    model.leave();
    CHECK(seen.save_failed == 1);  // said once

    // A round on a store that works is not a failure.
    Fixture good;
    REQUIRE(good.model->begin(3, 3, the_tiles));
    good.model->leave();
    CHECK_FALSE(good.model->saveFailed());
}
