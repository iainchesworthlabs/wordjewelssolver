// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <memory>
#include <span>

#include "path_tracer.hpp"
#include "solo_game.hpp"
#include "wjs/settings.hpp"

namespace wjs::app {

// What the Solo Play screens bind to: the game's state, board, tray and path as
// Qt Quick wants them, and the touch input turned into moves. It decides
// nothing about words (the SoloGame behind it does) and plays nothing: every
// moment that wants a sound or a tap of the hand is a `cue`, which no sound
// code listens to yet.
//
// It owns three things the game does not:
//   - the presentation state `word-found` (docs/design/game.md), the few moments
//     after a find when the word is shown, which the screens end with wordShown();
//   - the path being traced or tapped (PathTracer), in tile indices;
//   - the sound-it-out blocks of that path, as the finger moves.
//
// Tile indices are row * columns + column, from the top left.
//
// It starts where the game is. Over a new game that is the box. Over one brought
// back from the run before it is `playing`, with the board, the tray and the lit
// tile as they were left and nothing to shake, or `resting`: the screens open on
// it, and the scene is given the board with `show` and not `play`.
class SoloModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // "box", "shaking", "dropping", "playing", "word-found", "cleared" or
    // "resting": the ids of docs/design/game.md without the "solo." prefix.
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)

    // The size chosen (also in the box), and those offered.
    Q_PROPERTY(int boardSize READ boardSize NOTIFY sizeChanged)
    Q_PROPERTY(QVariantList sizes READ sizes CONSTANT)

    // The board on the table: empty in the box until the first shake. A dead
    // jewel is "". `faces` is the six faces of the die on each tile, row-major,
    // and `up` is which of them is up, for the scene to tumble them; both are
    // empty when the board was not rolled from dice (the phonics board, a board
    // brought back from the store, the fake game). `boardVersion` is a new number
    // for each board dealt.
    Q_PROPERTY(int rows READ rows NOTIFY boardChanged)
    Q_PROPERTY(int columns READ columns NOTIFY boardChanged)
    Q_PROPERTY(QStringList tiles READ tiles NOTIFY boardChanged)
    Q_PROPERTY(QVariantList faces READ faces NOTIFY boardChanged)
    Q_PROPERTY(QVariantList up READ up NOTIFY boardChanged)
    Q_PROPERTY(uint seed READ seed NOTIFY boardChanged)
    Q_PROPERTY(int boardVersion READ boardVersion NOTIFY boardChanged)

    // The path being made, as tile indices, and its grapheme blocks, one map
    // each: first (index in the path), tiles (how many), grapheme (letters).
    // The lists are built once per change and read many times (the screens read
    // `path` once a tile and once a path line), so the getters return a cached
    // list and `pathChanged` means "the path changed": a rebuild per read is the
    // pointer's jank. `pointerDown` has its own signal so that a finger going
    // down or up does not rebuild the path's display.
    Q_PROPERTY(QVariantList path READ path NOTIFY pathChanged)
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY pathChanged)
    Q_PROPERTY(bool pointerDown READ pointerDown NOTIFY pointerDownChanged)
    // The path of the word being shown, in `word-found`.
    Q_PROPERTY(QVariantList litPath READ litPath NOTIFY litPathChanged)

    // The tray: one map for each word found on this board: word (lower-case),
    // tier ("spark", "power", "mega"), stars and points.
    Q_PROPERTY(QVariantList found READ found NOTIFY foundChanged)
    // The running totals of the board, derived from `found`: what the panel's
    // score line and the clear summary show. No rule is added here.
    Q_PROPERTY(int boardPoints READ boardPoints NOTIFY foundChanged)
    Q_PROPERTY(int boardStars READ boardStars NOTIFY foundChanged)

    // The help: whether the parent allows it (none of the levels is off), the
    // tile lit now (-1: none), the tiles the second rung lights (up to two, in
    // the word's order) and the rung itself: 0 none, 1 the start tile, 2 the
    // first two letters.
    Q_PROPERTY(bool helpAllowed READ helpAllowed NOTIFY helpChanged)
    // The parent's help level, "off", "start" or "two": what a press may show.
    // The window hands the setting in; "start" shows the start tile alone (the
    // next press hides it), "two" adds the second rung.
    Q_PROPERTY(QString helpLevel READ helpLevel WRITE setHelpLevel NOTIFY helpChanged)
    Q_PROPERTY(int helpTile READ helpTile NOTIFY helpChanged)
    Q_PROPERTY(QVariantList helpTiles READ helpTiles NOTIFY helpChanged)
    Q_PROPERTY(int hintLevel READ hintLevel NOTIFY helpChanged)
    // The help was answered by a find made from its start: the rings are put
    // away, and how many more of the board's words begin with the tiles that
    // were lit (-1: nothing has been answered).
    Q_PROPERTY(bool hintResolved READ hintResolved NOTIFY helpChanged)
    Q_PROPERTY(int hintRemaining READ hintRemaining NOTIFY helpChanged)

    // Earlier boards, newest first: rows, columns, tiles and found (as the tray's).
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)

    // The game could not keep its progress (the store refused it, or the file
    // could not be written). The parent area says how the store is; this tells the
    // screens the moment it lets the game down, for a note the grown-up reads.
    Q_PROPERTY(bool saveFailed READ saveFailed NOTIFY saveFailedChanged)

    // The clock the parent sets for the board: "off", "up" (how long the board
    // has been open) or "down" (the minutes left, which ends the round at zero).
    // The window hands the setting in; the screen says when it is ticking
    // (`clockActive`), and the model counts whole seconds while it is.
    Q_PROPERTY(QString clockMode READ clockMode WRITE setClockMode NOTIFY clockChanged)
    Q_PROPERTY(int clockMinutes READ clockMinutes WRITE setClockMinutes NOTIFY clockChanged)
    Q_PROPERTY(bool clockActive READ clockActive WRITE setClockActive NOTIFY clockChanged)
    // What the clock shows: the seconds played (up) or left (down).
    Q_PROPERTY(int clockSeconds READ clockSeconds NOTIFY clockSecondsChanged)
    Q_PROPERTY(QString clockText READ clockText NOTIFY clockSecondsChanged)

public:
    explicit SoloModel(QObject* parent = nullptr);
    explicit SoloModel(std::unique_ptr<SoloGame> game, QObject* parent = nullptr);
    ~SoloModel() override;

    [[nodiscard]] QString state() const;
    [[nodiscard]] int boardSize() const;
    [[nodiscard]] QVariantList sizes() const;
    [[nodiscard]] int rows() const;
    [[nodiscard]] int columns() const;
    [[nodiscard]] QStringList tiles() const;
    [[nodiscard]] QVariantList faces() const;
    [[nodiscard]] QVariantList up() const;
    [[nodiscard]] uint seed() const;
    [[nodiscard]] int boardVersion() const { return board_version_; }
    [[nodiscard]] QVariantList path() const;
    [[nodiscard]] QVariantList blocks() const;
    [[nodiscard]] bool pointerDown() const { return tracer_.pointerDown(); }
    [[nodiscard]] QVariantList litPath() const { return lit_path_; }
    [[nodiscard]] QVariantList found() const;
    [[nodiscard]] int boardPoints() const;
    [[nodiscard]] int boardStars() const;
    [[nodiscard]] bool helpAllowed() const;
    [[nodiscard]] QString helpLevel() const;
    void setHelpLevel(const QString& level);
    [[nodiscard]] int helpTile() const;
    [[nodiscard]] QVariantList helpTiles() const { return hint_tiles_; }
    [[nodiscard]] int hintLevel() const { return hint_level_; }
    [[nodiscard]] bool hintResolved() const { return hint_resolved_; }
    [[nodiscard]] int hintRemaining() const { return hint_remaining_; }
    [[nodiscard]] QVariantList history() const { return history_; }
    [[nodiscard]] bool saveFailed() const noexcept { return save_failed_; }
    [[nodiscard]] QString clockMode() const;
    [[nodiscard]] int clockMinutes() const;
    [[nodiscard]] bool clockActive() const;
    [[nodiscard]] int clockSeconds() const;
    [[nodiscard]] QString clockText() const;
    void setClockMode(const QString& mode);
    void setClockMinutes(int minutes);
    void setClockActive(bool active);

    // --- the box
    Q_INVOKABLE void chooseSize(int size);
    // The Shake button, Space, or a device shake. Does nothing outside the box.
    Q_INVOKABLE void shake();
    // The board scene says its dice are still.
    Q_INVOKABLE void sceneSettled();
    // One second of the board's clock. A test calls it so that a minute never
    // takes a minute; the model's own timer measures the time that really went
    // by (a stalled interface loses none of it) and advances the clock by that.
    Q_INVOKABLE void tickClock();
    // The clock moves on by `milliseconds` of the time the board has been open
    // and the screen has been active. Does nothing while the clock is off or not
    // ticking, or once a count-down has ended.
    void advanceClock(qint64 milliseconds);

    // --- making a word. Positions are in pixels from the board's top left.
    Q_INVOKABLE void setGeometry(qreal tile, qreal gap);
    Q_INVOKABLE void pressAt(qreal x, qreal y);
    Q_INVOKABLE void moveTo(qreal x, qreal y);
    Q_INVOKABLE void releaseAt(qreal x, qreal y, bool inside);
    // Input with no pointer: Space on the focused tile, a screen reader's
    // activation. The same as tapping the tile.
    Q_INVOKABLE void tapTile(int index);
    // The Done jewel, Enter.
    Q_INVOKABLE void done();
    Q_INVOKABLE void removeLast();
    Q_INVOKABLE void clearPath();

    // --- the moments the screens time
    // The word has been shown: -> playing, or -> cleared after the last one.
    Q_INVOKABLE void wordShown();
    // The quiet celebration is over: -> resting.
    Q_INVOKABLE void celebrated();

    // --- help, and the ways out
    Q_INVOKABLE void askHelp();
    // Back from playing: the lid goes on, the board is saved.
    Q_INVOKABLE void lidOn();
    // Again from resting, or leaving it for home: the box is ready next time.
    Q_INVOKABLE void again();

    // --- what the parent changes
    // The parent changed a setting or reset what is kept (ParentController::
    // storeEdited). The next game is made from the store as it is now: a new one
    // at once when the box has its lid on, as the parent area is reached from it,
    // and otherwise when it next has. A game in play is never taken from a child.
    Q_INVOKABLE void storeEdited();

signals:
    void stateChanged();
    void sizeChanged();
    void boardChanged();
    void pathChanged();
    // The finger went down or up. Apart from `pathChanged` on purpose: the path
    // itself did not change, and the screens redraw it only for the path.
    void pointerDownChanged();
    void litPathChanged();
    void foundChanged();
    void helpChanged();
    void historyChanged();
    void saveFailedChanged();

    // A path was handed in. `kind` is "word" (a viable word: `word`, `tier` and
    // `stars` say which; `again` is true when it was on the tray already), "tick"
    // (a real word that is not today's: no word is given), or "fade" (anything
    // else). `path` is the tiles, for the fade and the tick. `newBest` and
    // `cleared` come with the last word.
    void traced(const QString& kind, const QVariantList& path, const QString& word,
                const QString& tier, int stars, bool again, bool newBest, bool cleared);

    // The board's clock: its setting, or what it shows, changed.
    void clockChanged();
    void clockSecondsChanged();
    // A count-down board reached zero: the round is over.
    void clockExpired();

    // A moment that wants a sound or a tap of the hand: "rattle", "tile" (detail:
    // the tile's letters, for its sound), "word" (the word), "tick", "chime",
    // "cleared". Nothing listens yet (docs/design/application.md, "Audio and
    // touch feedback"); the cues are where it will.
    void cue(const QString& kind, const QString& detail);

private:
    void applyEvent(PathTracer::Event event);
    void submitPath();
    void resetTracer();
    void refreshHistory();
    void refreshHelp();
    void renewGameIfDue();
    // What the clock shows for the time played; a changed setting keeps the time
    // already played, and a new board (startClock) starts it again.
    void showClock();
    void clockTimerFired();
    // Asks the game whether its last write went wrong, and tells the screens if
    // that changed. Called after each call that can write.
    void refreshSaveStatus();
    void startClock();
    [[nodiscard]] QVariantList indices(std::span<const Position> path) const;
    [[nodiscard]] QString lettersAt(Position position) const;
    // The cache behind the change signals: a flag is set where the signal is
    // sent, and the getter builds the list the first time it is read after
    // that. Unchanged reads return the same list, so a binding storm over
    // `path` costs one build, not one a read.
    void markPathChanged();
    void markBoardChanged();
    void markFoundChanged();
    // Builds the tiles, faces and up lists from the game's board if the board
    // changed since they were.
    void ensureBoardCaches() const;

    std::unique_ptr<SoloGame> game_;
    PathTracer tracer_;
    bool presenting_ = false;  // solo.word-found
    bool renew_due_ = false;   // the store was edited since the game was made
    int board_version_ = 0;
    int help_tile_ = -1;
    bool save_failed_ = false;
    int hint_level_ = 0;
    QVariantList hint_tiles_;
    bool hint_resolved_ = false;
    int hint_remaining_ = -1;
    // The child put the help away (the third press, or the second at the
    // start-tile level). The session keeps its lit word for the assisted-find
    // record; the screens show no ring until the next press lights it again.
    bool hint_hidden_ = false;
    HelpLevel help_level_ = HelpLevel::two_letters;
    QVariantList lit_path_;
    QVariantList history_;
    std::size_t last_path_size_ = 0;

    // The board's clock: the parent's choice, and the seconds it shows. The
    // QTimer below ticks once a second while the screen says it is active.
    SoloClock clock_mode_ = SoloClock::off;
    int clock_minutes_ = wjs::default_solo_clock_minutes;
    bool clock_active_ = false;
    bool clock_expired_ = false;
    int clock_seconds_ = 0;
    // The time the board has been open with the clock on and the screen active, in
    // milliseconds; clock_seconds_ is what it shows. clock_since_ measures the
    // time between two looks at it.
    qint64 clock_elapsed_ms_ = 0;
    QElapsedTimer clock_since_;
    QTimer clock_timer_;

    mutable QVariantList path_cache_;
    mutable bool path_dirty_ = true;
    mutable QVariantList blocks_cache_;
    mutable bool blocks_dirty_ = true;
    mutable QStringList tiles_cache_;
    mutable QVariantList faces_cache_;
    mutable QVariantList up_cache_;
    mutable bool board_dirty_ = true;
    mutable QVariantList found_cache_;
    mutable bool found_dirty_ = true;
};

}  // namespace wjs::app
