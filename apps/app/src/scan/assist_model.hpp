// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <memory>

#include "assist_game.hpp"
#include "assist_game_factory.hpp"
#include "path_tracer.hpp"

namespace wjs::app {

// What the Family Assistant screen (`assist.playing`) binds to: the confirmed
// board's tiles, the path being traced, the tray, the start tile and the cues.
// It decides nothing about words (AssistGame does) and plays nothing: every
// moment that wants a sound is a `cue`, as in Solo Play, and nothing listens
// yet.
//
// It is made by QML with no arguments and asks make_assist_game() when a board
// is confirmed, so the program's dictionary and store are installed once in
// main.cpp (assist_game_factory.hpp). With no maker installed begin() answers
// false, and the screen stays on the check screen.
//
// The board arrives from the check screen as plain values, because QML carries
// plain values: begin(rows, columns, letters) makes the wjs::Board that the
// grown-up's "Yes, play" confirmed. That is the only way a board gets here: a
// reading is never a board (wjs::scan::BoardDraft::confirm() is the check).
//
// Tile indices are row * columns + column, from the top left.
class AssistModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Whether a round is on: a board was confirmed and begin() made the game.
    Q_PROPERTY(bool active READ active NOTIFY gameChanged)

    // The confirmed board. Empty before begin().
    Q_PROPERTY(int rows READ rows NOTIFY boardChanged)
    Q_PROPERTY(int columns READ columns NOTIFY boardChanged)
    Q_PROPERTY(QStringList tiles READ tiles NOTIFY boardChanged)

    // The path being made, as tile indices.
    Q_PROPERTY(QVariantList path READ path NOTIFY pathChanged)
    Q_PROPERTY(bool pointerDown READ pointerDown NOTIFY pathChanged)

    // The tray: one map for each word found: word (lower case), tier ("spark",
    // "power" or "mega") and stars.
    Q_PROPERTY(QVariantList found READ found NOTIFY foundChanged)
    Q_PROPERTY(int stars READ stars NOTIFY foundChanged)

    // Earlier boards, newest first, as the tray's maps: rows, columns, tiles and
    // found. The board in play joins them when the round is left.
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)

    // The last board left could not be kept in the history: the store refused it or
    // its file could not be written. The grown-up is told (the screen words it).
    Q_PROPERTY(bool saveFailed READ saveFailed NOTIFY saveFailedChanged)

    // The start tile lit now (-1: none), and whether the parent allows the help.
    Q_PROPERTY(int litTile READ litTile NOTIFY helpChanged)
    Q_PROPERTY(bool helpAllowed READ helpAllowed NOTIFY helpChanged)

public:
    explicit AssistModel(QObject* parent = nullptr);
    explicit AssistModel(AssistGameMaker maker, QObject* parent = nullptr);
    ~AssistModel() override;

    [[nodiscard]] bool active() const noexcept { return game_ != nullptr; }
    [[nodiscard]] int rows() const;
    [[nodiscard]] int columns() const;
    [[nodiscard]] QStringList tiles() const;
    [[nodiscard]] QVariantList path() const;
    [[nodiscard]] bool pointerDown() const noexcept { return tracer_.pointerDown(); }
    [[nodiscard]] QVariantList found() const;
    [[nodiscard]] int stars() const;
    [[nodiscard]] QVariantList history() const;
    [[nodiscard]] bool saveFailed() const noexcept { return save_failed_; }
    [[nodiscard]] int litTile() const noexcept { return help_tile_; }
    [[nodiscard]] bool helpAllowed() const;

    // "Yes, play": a board the check screen confirmed, as plain values. Replaces
    // a round that was on (and records it first). False, and the round that was on
    // untouched, with no maker installed, for a grid that is not rows * columns
    // tiles of one to three letters (a tile holding a space or a slash would
    // change the grid's shape), and when the game cannot be made.
    Q_INVOKABLE bool begin(int rows, int columns, const QStringList& letters);

    // The round is left (Back, New board, the screen going): the board is
    // recorded to the store, once. The tray stays readable until the screen is
    // gone.
    Q_INVOKABLE void leave();

    // --- the board, traced or tapped. Positions are in pixels from its top left.
    Q_INVOKABLE void setGeometry(qreal tile, qreal gap);
    Q_INVOKABLE void pressAt(qreal x, qreal y);
    Q_INVOKABLE void moveTo(qreal x, qreal y);
    Q_INVOKABLE void releaseAt(qreal x, qreal y, bool inside);
    Q_INVOKABLE void tapTile(int index);
    Q_INVOKABLE void done();
    Q_INVOKABLE void removeLast();
    Q_INVOKABLE void clearPath();

    // "Show me a start": one press lights the start tile of one viable word not
    // yet found (AssistGame::showStart). Nothing when the help is off.
    Q_INVOKABLE void askHelp();

signals:
    void gameChanged();
    void boardChanged();
    void pathChanged();
    void foundChanged();
    void historyChanged();
    void saveFailedChanged();
    void helpChanged();

    // A path was handed in. `kind` is "word" (a viable word: `word`, `tier` and
    // `stars` say which; `again` is true when it was on the tray already),
    // "tick" (a real word that is not today's: no word is given), or "fade"
    // (anything else). `path` is the tiles, for the fade and the tick.
    void traced(const QString& kind, const QVariantList& path, const QString& word,
                const QString& tier, int stars, bool again);

    // A moment that wants a sound: "tile" (the tile's letters, for its sound),
    // "word", "tick".
    void cue(const QString& kind, const QString& detail);

private:
    void applyEvent(PathTracer::Event event);
    void submitPath();
    void resetTracer();
    void refreshHelp();
    [[nodiscard]] QVariantList indices() const;
    [[nodiscard]] QString lettersAt(Position position) const;

    AssistGameMaker maker_;
    std::unique_ptr<AssistGame> game_;
    PathTracer tracer_;
    int help_tile_ = -1;
    bool save_failed_ = false;
    std::size_t last_path_size_ = 0;
};

}  // namespace wjs::app
