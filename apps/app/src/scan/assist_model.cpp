// SPDX-License-Identifier: GPL-3.0-or-later
#include "assist_model.hpp"

#include <QVariantMap>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "qt_text.hpp"
#include "wjs/board.hpp"
#include "wjs/profiling.hpp"

namespace wjs::app {
namespace {

[[nodiscard]] QString tier_name(wjs::Tier tier) {
    switch (tier) {
        case wjs::Tier::spark:
            return QStringLiteral("spark");
        case wjs::Tier::power:
            return QStringLiteral("power");
        case wjs::Tier::mega:
            return QStringLiteral("mega");
    }
    return QStringLiteral("spark");
}

[[nodiscard]] QVariantMap found_map(const AssistFound& found) {
    QVariantMap map;
    map.insert(QStringLiteral("word"), to_qstring(found.word));
    map.insert(QStringLiteral("tier"), tier_name(found.tier));
    map.insert(QStringLiteral("stars"), found.stars);
    return map;
}

// The board as Board::parse reads it: spaces between tiles, '/' between rows.
[[nodiscard]] std::optional<wjs::Board> board_from(int rows, int columns,
                                                   const QStringList& letters) {
    if (rows <= 0 || columns <= 0) {
        return std::nullopt;
    }
    // A grid no larger than the library's boards: the product below cannot wrap.
    if (rows > static_cast<int>(wjs::Board::max_dimension) ||
        columns > static_cast<int>(wjs::Board::max_dimension)) {
        return std::nullopt;
    }
    const auto height = static_cast<std::size_t>(rows);
    const auto width = static_cast<std::size_t>(columns);
    if (letters.size() != static_cast<qsizetype>(height * width)) {
        return std::nullopt;
    }
    std::string text;
    for (std::size_t row = 0; row < height; ++row) {
        if (row > 0) {
            text.push_back('/');
        }
        for (std::size_t column = 0; column < width; ++column) {
            const QString& tile = letters.at(static_cast<qsizetype>(row * width + column));
            // Board::parse reads a space as the next tile and a slash as the next
            // row, so a tile holding either would make a different grid.
            if (tile.isEmpty() || tile.contains(u' ') || tile.contains(u'/')) {
                return std::nullopt;
            }
            if (column > 0) {
                text.push_back(' ');
            }
            const QByteArray bytes = tile.toUtf8();
            text.append(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        }
    }
    auto board = wjs::Board::parse(text);
    // The grid that was asked for, and no other.
    if (!board || board->rows() != height || board->columns() != width) {
        return std::nullopt;
    }
    return std::move(*board);
}

}  // namespace

AssistModel::AssistModel(QObject* parent)
    : AssistModel([](wjs::Board board) { return make_assist_game(std::move(board)); }, parent) {}

AssistModel::AssistModel(AssistGameMaker maker, QObject* parent)
    : QObject(parent), maker_(std::move(maker)) {}

AssistModel::~AssistModel() {
    leave();
}

int AssistModel::rows() const {
    return game_ ? static_cast<int>(game_->board().rows()) : 0;
}

int AssistModel::columns() const {
    return game_ ? static_cast<int>(game_->board().columns()) : 0;
}

QStringList AssistModel::tiles() const {
    QStringList list;
    if (!game_) {
        return list;
    }
    const wjs::Board& board = game_->board();
    list.reserve(static_cast<qsizetype>(board.rows() * board.columns()));
    for (std::size_t row = 0; row < board.rows(); ++row) {
        for (std::size_t column = 0; column < board.columns(); ++column) {
            list.append(to_qstring(board.tile({row, column})));
        }
    }
    return list;
}

QVariantList AssistModel::indices() const {
    QVariantList list;
    if (!game_) {
        return list;
    }
    const std::size_t columns = game_->board().columns();
    for (const wjs::Position& position : tracer_.path()) {
        list.append(static_cast<int>(position.row * columns + position.column));
    }
    return list;
}

QString AssistModel::lettersAt(Position position) const {
    if (!game_) {
        return {};
    }
    const wjs::Board& board = game_->board();
    if (!board.contains(position)) {
        return {};
    }
    return to_qstring(board.tile(position));
}

QVariantList AssistModel::path() const {
    return indices();
}

QVariantList AssistModel::found() const {
    QVariantList list;
    if (!game_) {
        return list;
    }
    for (const AssistFound& found : game_->found()) {
        list.append(found_map(found));
    }
    return list;
}

int AssistModel::stars() const {
    return game_ ? game_->stars() : 0;
}

QVariantList AssistModel::history() const {
    QVariantList list;
    if (!game_) {
        return list;
    }
    for (const AssistHistoryEntry& entry : game_->history()) {
        QVariantMap map;
        map.insert(QStringLiteral("rows"), static_cast<int>(entry.rows));
        map.insert(QStringLiteral("columns"), static_cast<int>(entry.columns));
        QStringList tiles;
        tiles.reserve(static_cast<qsizetype>(entry.tiles.size()));
        for (const std::string& tile : entry.tiles) {
            tiles.append(to_qstring(tile));
        }
        map.insert(QStringLiteral("tiles"), tiles);
        QVariantList words;
        for (const AssistFound& found : entry.found) {
            words.append(found_map(found));
        }
        map.insert(QStringLiteral("found"), words);
        list.append(map);
    }
    return list;
}

bool AssistModel::helpAllowed() const {
    return game_ != nullptr && game_->helpAllowed();
}

void AssistModel::resetTracer() {
    if (!game_) {
        tracer_.setBoard(0, 0);
        last_path_size_ = 0;
        return;
    }
    const wjs::Board& board = game_->board();
    std::vector<bool> usable(board.rows() * board.columns(), true);
    tracer_.setBoard(board.rows(), board.columns(), std::move(usable));
    last_path_size_ = 0;
}

void AssistModel::refreshHelp() {
    int tile = -1;
    if (game_) {
        if (const std::optional<wjs::Position> lit = game_->litTile()) {
            tile = static_cast<int>(lit->row * game_->board().columns() + lit->column);
        }
    }
    if (tile != help_tile_) {
        help_tile_ = tile;
        emit helpChanged();
    }
}

bool AssistModel::begin(int rows, int columns, const QStringList& letters) {
    // "Yes, play": the grown-up waits for the whole of this, which is the board's
    // parsing, the game's making (its solve is assist.solve) and the old round's
    // record.
    const auto zone = wjs::profiling::zone<"assist.confirm">();
    if (!maker_) {
        return false;
    }
    std::optional<wjs::Board> board = board_from(rows, columns, letters);
    if (!board) {
        return false;
    }
    // The new game is made first, in a local: the round that is on stays what it
    // is if the maker cannot make one, and nothing is recorded or dropped for a
    // board that never started.
    std::unique_ptr<AssistGame> next = maker_(std::move(*board));
    if (!next || next->board().rows() != static_cast<std::size_t>(rows) ||
        next->board().columns() != static_cast<std::size_t>(columns)) {
        return false;
    }
    // A round that is on is recorded before it is replaced, so the store keeps
    // the board the family played and not only the new one.
    leave();
    game_ = std::move(next);
    resetTracer();
    help_tile_ = -1;
    emit gameChanged();
    emit boardChanged();
    emit pathChanged();
    emit foundChanged();
    emit historyChanged();
    emit helpChanged();
    return true;
}

void AssistModel::leave() {
    if (!game_) {
        return;
    }
    game_->leave();
    if (save_failed_ != game_->saveFailed()) {
        save_failed_ = game_->saveFailed();
        emit saveFailedChanged();
    }
}

void AssistModel::setGeometry(qreal tile, qreal gap) {
    tracer_.setGeometry(tile, gap);
}

void AssistModel::applyEvent(PathTracer::Event event) {
    switch (event) {
        case PathTracer::Event::none:
            return;
        case PathTracer::Event::changed:
        case PathTracer::Event::cancelled: {
            // Each tile that joined sounds its letter (a sound is cut off by the
            // next one).
            const auto size = tracer_.path().size();
            for (std::size_t i = last_path_size_; i < size; ++i) {
                emit cue(QStringLiteral("tile"), lettersAt(tracer_.path()[i]));
            }
            last_path_size_ = size;
            emit pathChanged();
            return;
        }
        case PathTracer::Event::submit:
            submitPath();
            return;
    }
}

void AssistModel::submitPath() {
    if (!game_) {
        tracer_.clear();
        last_path_size_ = 0;
        emit pathChanged();
        return;
    }
    const std::vector<wjs::Position> path = tracer_.path();
    const QVariantList traced_path = indices();
    tracer_.clear();
    last_path_size_ = 0;
    emit pathChanged();

    const AssistTraceOutcome outcome = game_->submit(path);
    switch (outcome.kind) {
        case AssistWordKind::viable:
            emit foundChanged();
            refreshHelp();
            emit traced(QStringLiteral("word"), traced_path, to_qstring(outcome.found.word),
                        tier_name(outcome.found.tier), outcome.found.stars, outcome.alreadyFound);
            emit cue(QStringLiteral("word"), to_qstring(outcome.found.word));
            return;
        case AssistWordKind::realNotTodays:
            // A real word that is not today's: no word in the outcome on purpose.
            emit traced(QStringLiteral("tick"), traced_path, {}, {}, 0, false);
            emit cue(QStringLiteral("tick"), {});
            return;
        case AssistWordKind::notAWord:
            emit traced(QStringLiteral("fade"), traced_path, {}, {}, 0, false);
            return;
    }
}

void AssistModel::pressAt(qreal x, qreal y) {
    if (!game_) {
        return;
    }
    applyEvent(tracer_.press({x, y}));
    emit pathChanged();
}

void AssistModel::moveTo(qreal x, qreal y) {
    if (!game_) {
        return;
    }
    applyEvent(tracer_.move({x, y}));
}

void AssistModel::releaseAt(qreal x, qreal y, bool inside) {
    if (!game_) {
        return;
    }
    applyEvent(tracer_.release({x, y}, inside));
    emit pathChanged();
}

void AssistModel::tapTile(int index) {
    if (!game_ || index < 0 || game_->board().columns() == 0) {
        return;
    }
    const auto i = static_cast<std::size_t>(index);
    if (i >= game_->board().rows() * game_->board().columns()) {
        return;
    }
    applyEvent(tracer_.tap({i / game_->board().columns(), i % game_->board().columns()}));
}

void AssistModel::done() {
    if (!game_) {
        return;
    }
    applyEvent(tracer_.submit());
}

void AssistModel::removeLast() {
    applyEvent(tracer_.removeLast());
}

void AssistModel::clearPath() {
    applyEvent(tracer_.clear());
}

void AssistModel::askHelp() {
    if (!game_ || !game_->helpAllowed()) {
        return;
    }
    game_->showStart();
    refreshHelp();
}

}  // namespace wjs::app
