// SPDX-License-Identifier: GPL-3.0-or-later
#include "solo_model.hpp"

#include <QSignalBlocker>
#include <QVariantMap>
#include <utility>

#include "qt_text.hpp"
#include "solo_game_factory.hpp"
#include "wjs/profiling.hpp"

namespace wjs::app {
namespace {

[[nodiscard]] QString tier_name(WordTier tier) {
    switch (tier) {
        case WordTier::spark:
            return QStringLiteral("spark");
        case WordTier::power:
            return QStringLiteral("power");
        case WordTier::mega:
            return QStringLiteral("mega");
    }
    return QStringLiteral("spark");
}

[[nodiscard]] QVariantMap found_map(const SoloFound& found) {
    QVariantMap map;
    map.insert(QStringLiteral("word"), to_qstring(found.word));
    map.insert(QStringLiteral("tier"), tier_name(found.tier));
    map.insert(QStringLiteral("stars"), found.stars);
    map.insert(QStringLiteral("points"), found.points);
    return map;
}

[[nodiscard]] QStringList tile_list(const SoloBoard& board) {
    QStringList tiles;
    tiles.reserve(static_cast<qsizetype>(board.tiles.size()));
    for (const auto& tile : board.tiles) {
        tiles.append(to_qstring(tile));
    }
    return tiles;
}

[[nodiscard]] QVariantList faces_list(const SoloBoard& board) {
    QVariantList list;
    list.reserve(static_cast<qsizetype>(board.faces.size()));
    for (const auto& die : board.faces) {
        QStringList sides;
        sides.reserve(static_cast<qsizetype>(die.size()));
        for (const std::string& face : die) {
            sides.append(to_qstring(face));
        }
        list.append(sides);
    }
    return list;
}

[[nodiscard]] QVariantList up_list(const SoloBoard& board) {
    QVariantList list;
    list.reserve(static_cast<qsizetype>(board.up.size()));
    for (const int face : board.up) {
        list.append(face);
    }
    return list;
}

}  // namespace

SoloModel::SoloModel(QObject* parent) : SoloModel(make_solo_game(), parent) {}

SoloModel::SoloModel(std::unique_ptr<SoloGame> game, QObject* parent)
    : QObject(parent), game_(std::move(game)) {
    save_failed_ = game_->saveFailed();
    refreshHistory();
    // The board's clock counts the time that went by while the screen says it is
    // active (clockActive). The timer only looks at the time: a quarter second
    // keeps the display within a quarter second of the truth, and the seconds are
    // the elapsed time's. A test calls tickClock() or advanceClock() itself and
    // never waits for a second of wall time.
    clock_timer_.setInterval(250);
    clock_timer_.setTimerType(Qt::CoarseTimer);
    QObject::connect(&clock_timer_, &QTimer::timeout, this, &SoloModel::clockTimerFired);
    // A game that is not in the box was brought back from the run before: the board
    // is on the table already, with no shake to deal it, so what a shake sets up is
    // set up here (the touch geometry, the tile the help had lit, and the board's
    // clock).
    if (game_->state() != SoloState::box) {
        ++board_version_;
        resetTracer();
        refreshHelp();
        startClock();
    }
}

SoloModel::~SoloModel() = default;

QString SoloModel::state() const {
    if (presenting_) {
        return QStringLiteral("word-found");
    }
    const auto name = to_string(game_->state());
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

int SoloModel::boardSize() const {
    return static_cast<int>(game_->size());
}

QVariantList SoloModel::sizes() const {
    QVariantList list;
    for (const std::size_t size : game_->sizes()) {
        list.append(static_cast<int>(size));
    }
    return list;
}

int SoloModel::rows() const {
    return static_cast<int>(game_->board().rows);
}

int SoloModel::columns() const {
    return static_cast<int>(game_->board().columns);
}

void SoloModel::ensureBoardCaches() const {
    if (!board_dirty_) {
        return;
    }
    tiles_cache_ = tile_list(game_->board());
    faces_cache_ = faces_list(game_->board());
    up_cache_ = up_list(game_->board());
    board_dirty_ = false;
}

QStringList SoloModel::tiles() const {
    ensureBoardCaches();
    return tiles_cache_;
}

QVariantList SoloModel::faces() const {
    ensureBoardCaches();
    return faces_cache_;
}

QVariantList SoloModel::up() const {
    ensureBoardCaches();
    return up_cache_;
}

uint SoloModel::seed() const {
    return game_->board().seed;
}

QVariantList SoloModel::indices(std::span<const Position> path) const {
    QVariantList list;
    const std::size_t columns = game_->board().columns;
    for (const auto& position : path) {
        list.append(static_cast<int>(position.row * columns + position.column));
    }
    return list;
}

QString SoloModel::lettersAt(Position position) const {
    const auto& board = game_->board();
    if (position.row >= board.rows || position.column >= board.columns) {
        return {};
    }
    return to_qstring(board.tile(position));
}

// The signal, and the cache flag that goes with it: the getter rebuilds the
// list on the first read after this, and unchanged reads are free.
void SoloModel::markPathChanged() {
    path_dirty_ = true;
    blocks_dirty_ = true;
    emit pathChanged();
}

void SoloModel::markBoardChanged() {
    board_dirty_ = true;
    emit boardChanged();
}

void SoloModel::markFoundChanged() {
    found_dirty_ = true;
    emit foundChanged();
}

QVariantList SoloModel::path() const {
    // Built once per change, read many times: the screens read `path` once a
    // tile and once a path line, and a rebuild per read is the pointer's jank.
    // The zone is the one build, which nests inside the QML seam's `ui.trace`.
    if (path_dirty_) {
        const auto zone = wjs::profiling::zone<"solo.path">();
        path_cache_ = indices(tracer_.path());
        path_dirty_ = false;
    }
    return path_cache_;
}

QVariantList SoloModel::blocks() const {
    if (blocks_dirty_) {
        const auto zone = wjs::profiling::zone<"solo.blocks">();
        QVariantList list;
        if (!tracer_.path().empty()) {
            for (const auto& block : game_->blocks(tracer_.path())) {
                QVariantMap map;
                map.insert(QStringLiteral("first"), static_cast<int>(block.first));
                map.insert(QStringLiteral("tiles"), static_cast<int>(block.tiles));
                map.insert(QStringLiteral("grapheme"), to_qstring(block.grapheme));
                list.append(map);
            }
        }
        blocks_cache_ = std::move(list);
        blocks_dirty_ = false;
    }
    return blocks_cache_;
}

QVariantList SoloModel::found() const {
    if (found_dirty_) {
        QVariantList list;
        for (const auto& entry : game_->found()) {
            list.append(found_map(entry));
        }
        found_cache_ = std::move(list);
        found_dirty_ = false;
    }
    return found_cache_;
}

int SoloModel::boardPoints() const {
    int total = 0;
    for (const auto& entry : game_->found()) {
        total += entry.points;
    }
    return total;
}

int SoloModel::boardStars() const {
    int total = 0;
    for (const auto& entry : game_->found()) {
        total += entry.stars;
    }
    return total;
}

bool SoloModel::helpAllowed() const {
    return game_->helpAllowed() && help_level_ != HelpLevel::off;
}

QString SoloModel::helpLevel() const {
    const std::string_view name = to_string(help_level_);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

void SoloModel::setHelpLevel(const QString& level) {
    const std::string name = level.toStdString();
    const auto parsed = wjs::help_level_from_string(name);
    if (!parsed || *parsed == help_level_) {
        return;
    }
    help_level_ = *parsed;
    if (help_level_ == HelpLevel::off) {
        // The parent took the help away: the rings and the card go with it.
        help_tile_ = -1;
        hint_level_ = 0;
        hint_tiles_.clear();
        hint_resolved_ = false;
        hint_remaining_ = -1;
        hint_hidden_ = false;
    }
    emit helpChanged();
}

int SoloModel::helpTile() const {
    return help_tile_;
}

void SoloModel::refreshHistory() {
    // Every stored board is parsed here, on the GUI thread, so the sheet's
    // open is not a stall; the zone is the whole rebuild.
    const auto zone = wjs::profiling::zone<"solo.history">();
    QVariantList list;
    for (const auto& entry : game_->history()) {
        QVariantMap map;
        map.insert(QStringLiteral("rows"), static_cast<int>(entry.board.rows));
        map.insert(QStringLiteral("columns"), static_cast<int>(entry.board.columns));
        map.insert(QStringLiteral("tiles"), tile_list(entry.board));
        QVariantList words;
        for (const auto& found : entry.found) {
            words.append(found_map(found));
        }
        map.insert(QStringLiteral("found"), words);
        list.append(map);
    }
    history_ = std::move(list);
    emit historyChanged();
}

void SoloModel::refreshHelp() {
    int tile = -1;
    // A hidden help stays hidden: the session still lights its word (for the
    // assisted-find record), but the screens show no ring until the next
    // press lights one again.
    if (!hint_hidden_) {
        if (const auto lit = game_->litTile()) {
            tile = static_cast<int>(lit->row * game_->board().columns + lit->column);
        }
    }
    bool changed = false;
    if (tile != help_tile_) {
        help_tile_ = tile;
        changed = true;
    }
    // A find clears the word the help was for. Take the hint away with it, so
    // the next press starts at the first rung on a new word rather than leaving
    // two rings on the board with nothing to explain them.
    if (tile < 0 && (hint_level_ != 0 || !hint_tiles_.isEmpty())) {
        hint_level_ = 0;
        hint_tiles_.clear();
        changed = true;
    }
    if (changed) {
        emit helpChanged();
    }
}

void SoloModel::resetTracer() {
    const auto& board = game_->board();
    std::vector<bool> usable;
    usable.reserve(board.tiles.size());
    for (const auto& tile : board.tiles) {
        usable.push_back(!tile.empty());
    }
    tracer_.setBoard(board.rows, board.columns, std::move(usable));
    last_path_size_ = 0;
    markPathChanged();
}

void SoloModel::refreshSaveStatus() {
    const bool failed = game_->saveFailed();
    if (failed != save_failed_) {
        save_failed_ = failed;
        emit saveFailedChanged();
    }
}

// --- the board's clock --------------------------------------------------------

QString SoloModel::clockMode() const {
    const std::string_view name = to_string(clock_mode_);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

int SoloModel::clockMinutes() const {
    return clock_minutes_;
}

bool SoloModel::clockActive() const {
    return clock_active_;
}

int SoloModel::clockSeconds() const {
    return clock_seconds_;
}

QString SoloModel::clockText() const {
    const int minutes = clock_seconds_ / 60;
    const int seconds = clock_seconds_ % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

namespace {

// The longest count-down the model takes from QML: a day. The product with sixty
// below then cannot overflow, whatever the binding hands in.
constexpr int kMaxClockMinutes = 24 * 60;

}  // namespace

void SoloModel::setClockMode(const QString& mode) {
    const std::string name = mode.toStdString();
    const auto parsed = wjs::solo_clock_from_string(name);
    if (!parsed) {
        return;
    }
    if (*parsed == clock_mode_) {
        return;
    }
    clock_mode_ = *parsed;
    // A board in play keeps the time it has already had: a clock that is turned
    // on mid-round starts from the beginning only because nothing was counted
    // while it was off.
    showClock();
    emit clockChanged();
}

void SoloModel::setClockMinutes(int minutes) {
    if (minutes <= 0 || minutes > kMaxClockMinutes || minutes == clock_minutes_) {
        return;
    }
    clock_minutes_ = minutes;
    showClock();
    emit clockChanged();
}

void SoloModel::setClockActive(bool active) {
    if (active == clock_active_) {
        return;
    }
    if (!active && clock_timer_.isActive()) {
        // Time that went by since the timer last looked is the board's.
        advanceClock(clock_since_.elapsed());
    }
    clock_active_ = active;
    if (clock_active_ && clock_mode_ != SoloClock::off && !clock_expired_) {
        clock_since_.restart();
        clock_timer_.start();
    } else {
        clock_timer_.stop();
    }
    emit clockChanged();
}

// A new board: the clock reads the start of the round (nothing played, or the
// whole count-down still ahead), and an ended count-down can end again.
void SoloModel::startClock() {
    clock_expired_ = false;
    clock_elapsed_ms_ = 0;
    if (clock_active_ && clock_mode_ != SoloClock::off) {
        clock_since_.restart();
        clock_timer_.start();
    } else {
        clock_timer_.stop();
    }
    showClock();
}

// What the clock shows for the time played: the whole seconds played, or the
// seconds left rounded up (so that it reads zero only when the time is out).
void SoloModel::showClock() {
    int seconds = 0;
    if (clock_mode_ == SoloClock::count_up) {
        seconds = static_cast<int>(clock_elapsed_ms_ / 1000);
    } else if (clock_mode_ == SoloClock::count_down) {
        const qint64 total = static_cast<qint64>(clock_minutes_) * 60 * 1000;
        const qint64 left = total > clock_elapsed_ms_ ? total - clock_elapsed_ms_ : 0;
        seconds = static_cast<int>((left + 999) / 1000);
    }
    const bool changed = seconds != clock_seconds_;
    clock_seconds_ = seconds;
    if (changed) {
        emit clockSecondsChanged();
    }
    if (clock_mode_ == SoloClock::count_down && clock_seconds_ <= 0 && !clock_expired_) {
        clock_expired_ = true;
        clock_timer_.stop();
        emit clockExpired();
    }
}

void SoloModel::advanceClock(qint64 milliseconds) {
    if (!clock_active_ || clock_mode_ == SoloClock::off || clock_expired_ || milliseconds <= 0) {
        return;
    }
    clock_elapsed_ms_ += milliseconds;
    showClock();
}

void SoloModel::clockTimerFired() {
    advanceClock(clock_since_.restart());
}

void SoloModel::tickClock() {
    advanceClock(1000);
}

// --- the box ----------------------------------------------------------------

void SoloModel::chooseSize(int size) {
    if (size < 0 || game_->state() != SoloState::box) {
        return;
    }
    const auto before = game_->size();
    game_->chooseSize(static_cast<std::size_t>(size));
    if (game_->size() != before) {
        emit sizeChanged();
    }
    refreshSaveStatus();
}

void SoloModel::shake() {
    if (game_->state() != SoloState::box || !game_->shake()) {
        return;
    }
    presenting_ = false;
    lit_path_.clear();
    help_tile_ = -1;
    hint_level_ = 0;
    hint_tiles_.clear();
    hint_resolved_ = false;
    hint_remaining_ = -1;
    hint_hidden_ = false;
    ++board_version_;
    resetTracer();
    startClock();
    markBoardChanged();
    markFoundChanged();
    emit helpChanged();
    emit litPathChanged();
    emit stateChanged();
    emit cue(QStringLiteral("rattle"), {});
    refreshSaveStatus();
}

void SoloModel::sceneSettled() {
    if (game_->state() != SoloState::shaking && game_->state() != SoloState::dropping) {
        return;
    }
    game_->settled();
    emit stateChanged();
    refreshSaveStatus();
}

// --- making a word ------------------------------------------------------------

void SoloModel::setGeometry(qreal tile, qreal gap) {
    tracer_.setGeometry(tile, gap);
}

void SoloModel::applyEvent(PathTracer::Event event) {
    switch (event) {
        case PathTracer::Event::none:
            return;
        case PathTracer::Event::changed:
        case PathTracer::Event::cancelled: {
            // Each tile that joined sounds its letter (a sound is cut off by the next).
            const auto size = tracer_.path().size();
            for (std::size_t i = last_path_size_; i < size; ++i) {
                emit cue(QStringLiteral("tile"), lettersAt(tracer_.path()[i]));
            }
            last_path_size_ = size;
            markPathChanged();
            return;
        }
        case PathTracer::Event::submit:
            submitPath();
            return;
    }
}

void SoloModel::submitPath() {
    // The whole synchronous moment of a word handed in: the tracer, the game's
    // answer and (in the real game) the store's writes, which the profile of
    // this zone shows. The value is the path's tiles.
    auto zone = wjs::profiling::zone<"solo.submit">();
    const std::vector<Position> path = tracer_.path();
    const QVariantList traced_path = indices(path);
    zone.Value(static_cast<std::uint64_t>(traced_path.size()));
    tracer_.clear();
    last_path_size_ = 0;
    markPathChanged();

    const auto outcome = game_->submit(path);
    refreshSaveStatus();
    switch (outcome.kind) {
        case WordKind::viable: {
            presenting_ = true;
            lit_path_ = traced_path;
            emit litPathChanged();
            markFoundChanged();
            // The help: a find made from its start answers it - the rings are
            // put away, and the panel says whether the start still leads to
            // more of the board's words (a count, never a word).
            const bool from_hint_start = hint_level_ >= 1 && help_tile_ >= 0 &&
                                         !traced_path.isEmpty() &&
                                         traced_path.constFirst().toInt() == help_tile_;
            if (from_hint_start) {
                std::vector<Position> prefix;
                const std::size_t columns = game_->board().columns;
                const auto position_of = [columns](const QVariant& tile) {
                    const int index = tile.toInt();
                    return Position{static_cast<std::size_t>(index) / columns,
                                    static_cast<std::size_t>(index) % columns};
                };
                prefix.push_back(position_of(help_tile_));
                if (hint_level_ >= 2 && hint_tiles_.size() >= 2) {
                    prefix.push_back(position_of(hint_tiles_.at(1)));
                }
                hint_remaining_ = static_cast<int>(game_->wordsWithStart(prefix));
                hint_resolved_ = true;
                hint_hidden_ = false;
                game_->dismissHelp();
            }
            refreshHelp();
            emit stateChanged();
            emit traced(QStringLiteral("word"), traced_path, to_qstring(outcome.found.word),
                        tier_name(outcome.found.tier), outcome.found.stars, outcome.alreadyFound,
                        outcome.newBest, outcome.cleared);
            emit cue(QStringLiteral("word"), to_qstring(outcome.found.word));
            if (outcome.newBest) {
                emit cue(QStringLiteral("chime"), {});
            }
            return;
        }
        case WordKind::realNotTodays:
            // A real word that is not today's: no word anywhere in the outcome.
            emit traced(QStringLiteral("tick"), traced_path, {}, {}, 0, false, false, false);
            emit cue(QStringLiteral("tick"), {});
            return;
        case WordKind::notAWord:
            emit traced(QStringLiteral("fade"), traced_path, {}, {}, 0, false, false, false);
            return;
    }
}

void SoloModel::pressAt(qreal x, qreal y) {
    if (game_->state() != SoloState::playing || presenting_) {
        return;
    }
    // The finger going down is pointerDownChanged, not pathChanged: a press
    // that adds a tile is told once by applyEvent, and a press that adds
    // nothing does not rebuild the path's display at all.
    const bool was_down = tracer_.pointerDown();
    applyEvent(tracer_.press({x, y}));
    if (tracer_.pointerDown() != was_down) {
        emit pointerDownChanged();
    }
}

void SoloModel::moveTo(qreal x, qreal y) {
    if (game_->state() != SoloState::playing || presenting_) {
        return;
    }
    applyEvent(tracer_.move({x, y}));
}

void SoloModel::releaseAt(qreal x, qreal y, bool inside) {
    const bool was_down = tracer_.pointerDown();
    if (game_->state() != SoloState::playing || presenting_) {
        // A finger lifted over a word being shown still lets go of the tracer.
        applyEvent(tracer_.release({x, y}, false));
    } else {
        applyEvent(tracer_.release({x, y}, inside));
    }
    if (tracer_.pointerDown() != was_down) {
        emit pointerDownChanged();
    }
}

void SoloModel::tapTile(int index) {
    const auto& board = game_->board();
    if (index < 0 || board.columns == 0 || game_->state() != SoloState::playing || presenting_) {
        return;
    }
    const auto i = static_cast<std::size_t>(index);
    applyEvent(tracer_.tap({i / board.columns, i % board.columns}));
}

void SoloModel::done() {
    if (game_->state() != SoloState::playing || presenting_) {
        return;
    }
    applyEvent(tracer_.submit());
}

void SoloModel::removeLast() {
    applyEvent(tracer_.removeLast());
}

void SoloModel::clearPath() {
    const bool was_down = tracer_.pointerDown();
    applyEvent(tracer_.clear());
    if (tracer_.pointerDown() != was_down) {
        emit pointerDownChanged();
    }
}

// --- the moments the screens time ----------------------------------------------

void SoloModel::wordShown() {
    if (!presenting_) {
        return;
    }
    presenting_ = false;
    lit_path_.clear();
    emit litPathChanged();
    emit stateChanged();
    if (game_->state() == SoloState::cleared) {
        emit cue(QStringLiteral("cleared"), {});
    }
}

void SoloModel::celebrated() {
    if (game_->state() != SoloState::cleared || presenting_) {
        return;
    }
    game_->celebrated();
    refreshHistory();
    emit stateChanged();
    refreshSaveStatus();
}

// --- help, and the ways out ----------------------------------------------------

void SoloModel::askHelp() {
    if (game_->state() != SoloState::playing || presenting_ || !helpAllowed()) {
        return;
    }

    // A new press starts a new question: the answer of the last one is put away.
    hint_resolved_ = false;
    hint_remaining_ = -1;

    if (hint_level_ == 0) {
        // Rung one: the start tile. The library chooses the word; the model
        // only says how much of it is lit. The tile and the rung are one
        // change to the screen, so the refresh's own notification is held
        // back and the one below covers both.
        hint_hidden_ = false;
        const auto tile = game_->startTile();
        refreshSaveStatus();
        {
            const QSignalBlocker blocker(this);
            refreshHelp();
        }
        if (tile) {
            hint_level_ = 1;
            hint_tiles_.clear();
            hint_tiles_.append(static_cast<int>(tile->row * game_->board().columns + tile->column));
        }
        emit helpChanged();
        return;
    }

    if (hint_level_ == 1 && help_level_ == HelpLevel::two_letters) {
        // Rung two: the first two tiles of the same word. This is deliberately
        // not the word: the child still has to see the rest and trace it.
        const std::vector<Position> path = game_->helpPath();
        if (path.size() >= 2) {
            hint_level_ = 2;
            hint_tiles_ = indices(path).mid(0, 2);
            emit helpChanged();
        }
        return;
    }

    // Put it away: the child asked for help, looked, and wants the board quiet
    // again - or the parent's level stops at the start tile, so the next press
    // hides it. The session's lit word stays for the assisted-find record, but
    // the rings and the card go, and stay gone until the next press.
    hint_hidden_ = true;
    hint_level_ = 0;
    hint_tiles_.clear();
    help_tile_ = -1;
    emit helpChanged();
}

void SoloModel::lidOn() {
    if (game_->state() != SoloState::playing && game_->state() != SoloState::shaking &&
        game_->state() != SoloState::dropping) {
        return;
    }
    presenting_ = false;
    lit_path_.clear();
    help_tile_ = -1;
    hint_level_ = 0;
    hint_tiles_.clear();
    hint_resolved_ = false;
    hint_remaining_ = -1;
    hint_hidden_ = false;
    const bool was_down = tracer_.pointerDown();
    tracer_.clear();
    game_->lidOn();
    refreshHistory();
    refreshSaveStatus();
    // The board is over: its clock is put away with it.
    clock_timer_.stop();
    clock_seconds_ = 0;
    emit clockSecondsChanged();
    markPathChanged();
    if (tracer_.pointerDown() != was_down) {
        emit pointerDownChanged();
    }
    emit litPathChanged();
    emit helpChanged();
    emit stateChanged();
    renewGameIfDue();
}

void SoloModel::again() {
    if (game_->state() != SoloState::resting) {
        return;
    }
    presenting_ = false;
    help_tile_ = -1;
    hint_level_ = 0;
    hint_tiles_.clear();
    hint_resolved_ = false;
    hint_remaining_ = -1;
    hint_hidden_ = false;
    game_->again();
    refreshHistory();
    refreshSaveStatus();
    emit helpChanged();
    emit stateChanged();
    renewGameIfDue();
}

// --- what the parent changes ----------------------------------------------------

void SoloModel::storeEdited() {
    renew_due_ = true;
    renewGameIfDue();
}

void SoloModel::renewGameIfDue() {
    if (!renew_due_ || game_->state() != SoloState::box) {
        return;
    }
    renew_due_ = false;
    // The game is made from the store as it is now (the maker is the program's), and
    // shown as a new game is: the box, empty, with the size and the help the parent
    // has chosen and the history the store keeps. One that is not in the box was
    // brought back, as at the start (the constructor): its board is on the table.
    game_ = make_solo_game();
    save_failed_ = game_->saveFailed();
    emit saveFailedChanged();
    presenting_ = false;
    help_tile_ = -1;
    hint_level_ = 0;
    hint_tiles_.clear();
    hint_resolved_ = false;
    hint_remaining_ = -1;
    hint_hidden_ = false;
    lit_path_.clear();
    if (game_->state() != SoloState::box) {
        ++board_version_;
        startClock();
    }
    resetTracer();
    refreshHelp();
    refreshHistory();
    emit sizeChanged();
    markBoardChanged();
    markFoundChanged();
    emit helpChanged();
    emit litPathChanged();
    emit stateChanged();
}

}  // namespace wjs::app
