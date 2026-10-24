// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>

#include "wjs/calendar.hpp"
#include "wjs/parent_gate.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/store.hpp"

namespace wjs::app {

// What the parent area's screens talk to (docs/design/dashboard.md): the gate,
// the dashboard, the settings and the two resets. All of the rules are the
// library's (wjs/parent_gate.hpp, wjs/store.hpp, wjs/dashboard.hpp); this is
// the shape Qt Quick wants, and the clock, which the library does not have.
//
// It shows the person nothing but what the store holds. It starts nothing and
// sends nothing, and its one file is the store the library writes.
//
// Who holds the store: the program's one store is shared with the game (so the
// two never write the same file separately), and handed in with useStore(). A
// program with no game yet (the stub the screens are tried in) lets this open
// a store of its own (openStore()). Phase data is handed in the same way
// (usePhases()); with none, the dashboard has no sounds and the Phonics board
// cannot be chosen.
class ParentController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // --- the gate -----------------------------------------------------------
    // The gate has been passed. The parent area shows nothing else until it has.
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY unlockedChanged)
    // Both spots are held; progress runs from 0 to 1 while they are.
    Q_PROPERTY(bool gateHolding READ gateHolding NOTIFY gateProgressChanged)
    Q_PROPERTY(double gateProgress READ gateProgress NOTIFY gateProgressChanged)
    // The two spots, [{x, y, r}, {x, y, r}], in the units layoutGate was given.
    Q_PROPERTY(QVariantList gateSpots READ gateSpots NOTIFY gateSpotsChanged)

    // --- what the screens show ------------------------------------------------
    // Computed when the dashboard is opened (refresh()), never on a schedule.
    Q_PROPERTY(QVariantMap dashboard READ dashboard NOTIFY dashboardChanged)
    // The child-facing totals of the same store, for the collection screen:
    // everything ever kept, not only this week. Derived, never stored anew.
    Q_PROPERTY(int lifetimeStars READ lifetimeStars NOTIFY dashboardChanged)
    Q_PROPERTY(int lifetimePoints READ lifetimePoints NOTIFY dashboardChanged)
    Q_PROPERTY(int lifetimeWords READ lifetimeWords NOTIFY dashboardChanged)
    Q_PROPERTY(int boardsKept READ boardsKept NOTIFY dashboardChanged)
    Q_PROPERTY(QString longestWord READ longestWord NOTIFY dashboardChanged)
    // The boards still held, newest first: rows, columns, tiles, stars, words
    // and how many words were found. Plain values for BoardThumb and the
    // history list; a standard board has no word list, only the count.
    Q_PROPERTY(QVariantList recentBoards READ recentBoards NOTIFY dashboardChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    // The sounds the phase has, which can be ticked; empty with no phase data.
    Q_PROPERTY(QStringList focusChoices READ focusChoices NOTIFY settingsChanged)
    Q_PROPERTY(QStringList phases READ phases NOTIFY settingsChanged)
    // Phase data is loaded, so the Phonics board can be chosen.
    Q_PROPERTY(bool phonicsAvailable READ phonicsAvailable NOTIFY settingsChanged)
    // A school font can be chosen only if its licence lets it be bundled, and
    // none is. Empty means the setting is not offered.
    Q_PROPERTY(QStringList schoolFonts READ schoolFonts CONSTANT)
    // "" when all is well; "unreadable" (the saved progress could not be read: a
    // new one was begun and the old file kept), "newer" (it was written by a newer
    // version: left alone, and nothing is saved this time) or "unsaved" (the disk
    // refused, or there is nowhere to save). For the parent only.
    Q_PROPERTY(QString storeNote READ storeNote NOTIFY storeNoteChanged)

    // --- what the program applies from the settings -----------------------------
    // The Motion setting, "system" or "reduced". Only the window knows what the
    // system itself asks for, so it is the one that decides what System comes to
    // (qml/Main.qml).
    Q_PROPERTY(QString motion READ motion NOTIFY settingsChanged)
    // The parent chose Reduced: motion is reduced whatever the system says. False
    // for System, which is not a choice for or against.
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY settingsChanged)
    Q_PROPERTY(double textScale READ textScale NOTIFY settingsChanged)
    // The Colour scheme setting, "system", "light" or "dark". The window applies
    // it to every screen's chrome; System follows the operating system.
    Q_PROPERTY(QString colourScheme READ colourScheme NOTIFY settingsChanged)
    // The Language setting: one of the eight codes, or "" for the device's own.
    // The window hands it to LanguageManager, which installs the catalogue and
    // sets the direction (qml/Main.qml).
    Q_PROPERTY(QString language READ language NOTIFY settingsChanged)
    // The help the child can ask for in play, "off", "start" or "two". The
    // window hands it to the game (qml/Main.qml).
    Q_PROPERTY(QString helpLevel READ helpLevel NOTIFY settingsChanged)
    // The clock of a solo board, "off", "up" or "down", and how many minutes a
    // count-down has. The window hands both to Solo Play (qml/Main.qml).
    Q_PROPERTY(QString soloClock READ soloClock NOTIFY settingsChanged)
    Q_PROPERTY(int soloClockMinutes READ soloClockMinutes NOTIFY settingsChanged)
    // The lengths the parent can choose for a count-down board.
    Q_PROPERTY(QVariantList timerLengthChoices READ timerLengthChoices CONSTANT)

    // --- about ------------------------------------------------------------------
    Q_PROPERTY(QString version READ version CONSTANT)
    // The version and build provenance, as lines (release, commit, branch,
    // target), for the About page.
    Q_PROPERTY(QString versionDetails READ versionDetails CONSTANT)
    // The third-party notices this build carries, for the Licences page:
    // :/notices/NOTICES.txt, the same text installed beside the programs
    // (cmake/Notices.cmake).
    Q_PROPERTY(QString licenceNotices READ licenceNotices CONSTANT)

public:
    explicit ParentController(QObject* parent = nullptr);
    ~ParentController() override;

    // --- wiring (C++) -------------------------------------------------------------

    // The program's store, shared and not owned: it must outlive this. `status`
    // is how it was opened, which the note says.
    void useStore(wjs::Store* store, wjs::LoadStatus status = wjs::LoadStatus::loaded);

    // Phase data, not owned; null for none.
    void usePhases(const wjs::PhaseSet* phases);

    // The calendar, which a test fixes: today as a Day and the locale's first day
    // of the week (ISO numbered). Without it the device's own are read.
    void useCalendar(std::function<wjs::Day()> today, int first_day_of_week);

    // The clock the gate is told the time by, in milliseconds from any fixed
    // moment, which a test fixes. Without it the steady clock is read. The
    // screens ask for the time here and never read a clock of their own, so a
    // stopped or a fixed clock is the same for all of them.
    void useClock(std::function<qint64()> now);

    // The store in this file, owned by this controller.
    void openStore(const std::filesystem::path& file);

    // --- the gate (QML) -----------------------------------------------------------

    // The time, for the calls below.
    Q_INVOKABLE qint64 now() const;

    // Where the spots go on a screen of this size (the screen's own units);
    // `minRadius` is the adult touch target's radius there.
    Q_INVOKABLE void layoutGate(double width, double height, double minRadius);
    Q_INVOKABLE void gateTouchDown(int id, double x, double y, qint64 ms);
    Q_INVOKABLE void gateTouchMove(int id, double x, double y, qint64 ms);
    Q_INVOKABLE void gateTouchUp(int id, qint64 ms);
    Q_INVOKABLE void gateTick(qint64 ms);

    // The accessibility action of the gate's control: opens it with no hold.
    Q_INVOKABLE void unlockForAssistiveTechnology();

    // The parent area is left: shut again.
    Q_INVOKABLE void lock();

    // --- the screens (QML) ---------------------------------------------------------

    // Computes the dashboard and reads the settings, as of now.
    Q_INVOKABLE void refresh();

    // Tries the store's write again after a failure, and updates the note.
    // False when it still cannot be saved.
    Q_INVOKABLE bool retrySave();

    // Changes one setting by the name the settings map uses. False, and nothing
    // changes, for a name or a value the table of settings does not have.
    Q_INVOKABLE bool setSetting(const QString& key, const QVariant& value);

    // Ticks or unticks one of focusChoices. False for a sound the phase lacks.
    Q_INVOKABLE bool toggleFocusSound(const QString& sound);

    // The two resets of the design, each its own step.
    Q_INVOKABLE void resetBoardsAndFinds();
    Q_INVOKABLE void resetStarsAndStickers();

    [[nodiscard]] bool unlocked() const noexcept { return unlocked_; }
    [[nodiscard]] bool gateHolding() const noexcept { return holding_; }
    [[nodiscard]] double gateProgress() const noexcept { return progress_; }
    [[nodiscard]] QVariantList gateSpots() const;
    [[nodiscard]] QVariantMap dashboard() const { return dashboard_; }
    [[nodiscard]] int lifetimeStars() const;
    [[nodiscard]] int lifetimePoints() const;
    [[nodiscard]] int lifetimeWords() const;
    [[nodiscard]] int boardsKept() const;
    [[nodiscard]] QString longestWord() const;
    [[nodiscard]] QVariantList recentBoards() const;
    [[nodiscard]] QVariantMap settings() const { return settings_; }
    [[nodiscard]] QStringList focusChoices() const;
    [[nodiscard]] QStringList phases() const;
    [[nodiscard]] bool phonicsAvailable() const noexcept { return phases_ != nullptr; }
    // No school font is bundled, so none can be chosen: the list is empty and the
    // Settings screen hides its row. The property stays only until that row goes
    // (SettingsScreen.qml reads its length); then it, the "schoolFont" key of
    // setSetting() and the settings map's entry go with it.
    [[nodiscard]] QStringList schoolFonts() const { return {}; }
    [[nodiscard]] QString storeNote() const;
    [[nodiscard]] QString motion() const;
    [[nodiscard]] bool reducedMotion() const;
    [[nodiscard]] double textScale() const;
    [[nodiscard]] QString colourScheme() const;
    [[nodiscard]] QString language() const;
    [[nodiscard]] QString helpLevel() const;
    [[nodiscard]] QString soloClock() const;
    [[nodiscard]] int soloClockMinutes() const;
    [[nodiscard]] QVariantList timerLengthChoices() const;
    [[nodiscard]] QString version() const;
    [[nodiscard]] QString versionDetails() const;
    [[nodiscard]] QString licenceNotices() const;

signals:
    void unlockedChanged();
    void gateProgressChanged();
    void gateSpotsChanged();
    void dashboardChanged();
    void settingsChanged();
    void storeNoteChanged();

    // The parent changed something the game reads from the store: a setting (one
    // that was not already so), or a reset. Not sent by refresh(), which only
    // reads, so that opening a screen is not an edit. The shell tells the game,
    // which takes its next game from the store as it is now.
    void storeEdited();

private:
    void afterGateEvent(std::chrono::milliseconds now);
    [[nodiscard]] const wjs::PhaseIndex* activeIndex() const;
    void applySettings(wjs::Settings settings);
    void updateNote();

    std::optional<wjs::Store> owned_;
    wjs::Store* store_ = nullptr;
    wjs::LoadStatus status_ = wjs::LoadStatus::loaded;
    const wjs::PhaseSet* phases_ = nullptr;

    std::function<wjs::Day()> today_;
    int first_day_of_week_ = 0;  // 0: ask the locale
    std::function<qint64()> clock_;

    wjs::Gate gate_;
    bool unlocked_ = false;
    bool holding_ = false;
    double progress_ = 0.0;

    QVariantMap dashboard_;
    QVariantMap settings_;
    QString note_;
};

}  // namespace wjs::app
