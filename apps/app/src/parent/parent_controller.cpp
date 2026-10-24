// SPDX-License-Identifier: GPL-3.0-or-later
#include "parent_controller.hpp"

#include <QDate>
#include <QFile>
#include <QLocale>
#include <QMetaType>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "qt_text.hpp"
#include "wjs/dashboard.hpp"
#include "wjs/game_rules.hpp"
#include "wjs/profiling.hpp"
#include "wjs/scoring.hpp"
#include "wjs/settings.hpp"
#include "wjs/version.hpp"

namespace wjs::app {
namespace {

using std::chrono::milliseconds;

using wjs::app::to_qstring;
using wjs::app::to_utf8;

[[nodiscard]] QStringList list(const std::vector<std::string>& values) {
    QStringList result;
    result.reserve(static_cast<qsizetype>(values.size()));
    for (const std::string& value : values) {
        result.append(to_qstring(value));
    }
    return result;
}

[[nodiscard]] bool is_bool(const QVariant& value) {
    return value.typeId() == QMetaType::Bool;
}

[[nodiscard]] bool is_string(const QVariant& value) {
    return value.typeId() == QMetaType::QString;
}

// A whole number from QML, which hands over integers as doubles when it likes. A
// double that is not one (NaN, infinite, fractional) or does not fit an int is
// not a whole number: converting one that does not fit is undefined, so the
// range is checked before the cast.
[[nodiscard]] std::optional<int> whole_number(const QVariant& value) {
    switch (value.typeId()) {
        case QMetaType::Int:
            return value.toInt();
        case QMetaType::Double: {
            const double number = value.toDouble();
            constexpr double lowest = static_cast<double>(std::numeric_limits<int>::min());
            constexpr double highest = static_cast<double>(std::numeric_limits<int>::max());
            if (!std::isfinite(number) || number < lowest || number > highest) {
                return std::nullopt;
            }
            const int whole = static_cast<int>(number);
            return static_cast<double>(whole) == number ? std::optional<int>{whole} : std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

[[nodiscard]] QString tier_name(std::size_t letters) {
    switch (wjs::tier_for_letters(letters).value_or(wjs::Tier::spark)) {
        case wjs::Tier::spark:
            return QStringLiteral("spark");
        case wjs::Tier::power:
            return QStringLiteral("power");
        case wjs::Tier::mega:
            return QStringLiteral("mega");
    }
    return QStringLiteral("spark");
}

[[nodiscard]] QVariantMap dashboard_map(const wjs::Dashboard& d) {
    QVariantMap map;
    map.insert(QStringLiteral("empty"), d.empty);
    map.insert(QStringLiteral("hasPhaseData"), d.has_phase_data);
    map.insert(QStringLiteral("phase"), to_qstring(d.phase));

    QVariantMap week;
    week.insert(QStringLiteral("played"), d.week.played());
    week.insert(QStringLiteral("boardsCleared"), d.week.boards_cleared);
    week.insert(QStringLiteral("stars"), d.week.stars);
    week.insert(QStringLiteral("newWords"), d.week.new_words);
    week.insert(QStringLiteral("longestWord"), to_qstring(d.week.longest_word));
    map.insert(QStringLiteral("week"), week);

    QVariantList sounds;
    for (const wjs::SoundRow& row : d.sounds) {
        QVariantMap sound;
        sound.insert(QStringLiteral("letters"), to_qstring(row.letters));
        sound.insert(QStringLiteral("wordsInPhase"), static_cast<qulonglong>(row.words_in_phase));
        sound.insert(QStringLiteral("boardsMet"), row.boards_met);
        sound.insert(QStringLiteral("boardsFound"), row.boards_found);
        sound.insert(QStringLiteral("wordsFound"), row.words_found);
        sound.insert(QStringLiteral("wordsUnassisted"), row.words_unassisted);
        sound.insert(QStringLiteral("oftenFound"), row.often_found);
        sound.insert(QStringLiteral("focus"), row.focus);
        sound.insert(QStringLiteral("fewWords"), row.few_words);
        sounds.append(sound);
    }
    map.insert(QStringLiteral("sounds"), sounds);

    QVariantList vault;
    for (const wjs::StickerRow& row : d.vault) {
        QVariantMap sticker;
        sticker.insert(QStringLiteral("word"), to_qstring(row.word));
        vault.append(sticker);
    }
    map.insert(QStringLiteral("vault"), vault);

    QVariantList best;
    for (const wjs::BestRow& row : d.best) {
        QVariantMap one;
        one.insert(QStringLiteral("size"), row.size);
        one.insert(QStringLiteral("stars"), row.stars);
        best.append(one);
    }
    map.insert(QStringLiteral("best"), best);
    return map;
}

[[nodiscard]] QVariantMap settings_map(const wjs::Settings& s) {
    QVariantMap map;
    map.insert(QStringLiteral("help"), to_qstring(to_string(s.help)));
    map.insert(QStringLiteral("board"), to_qstring(to_string(s.board)));
    map.insert(QStringLiteral("phase"), to_qstring(s.phase));
    map.insert(QStringLiteral("focusSounds"), list(s.focus_sounds));
    map.insert(QStringLiteral("boardSize"), s.board_size);
    map.insert(QStringLiteral("soundItOut"), to_qstring(to_string(s.sound_it_out)));
    map.insert(QStringLiteral("celebrationSounds"), to_qstring(to_string(s.celebration_sounds)));
    map.insert(QStringLiteral("sound"), s.sound);
    map.insert(QStringLiteral("vibration"), s.vibration);
    map.insert(QStringLiteral("shake"), s.shake);
    map.insert(QStringLiteral("motion"), to_qstring(to_string(s.motion)));
    map.insert(QStringLiteral("colourScheme"), to_qstring(to_string(s.colour_scheme)));
    map.insert(QStringLiteral("timer"), to_qstring(to_string(s.solo_clock)));
    map.insert(QStringLiteral("timerLength"), s.solo_clock_minutes);
    map.insert(QStringLiteral("textSizePercent"), s.text_size_percent);
    map.insert(QStringLiteral("schoolFont"), to_qstring(s.school_font));
    map.insert(QStringLiteral("language"), to_qstring(s.language));
    return map;
}

// One scope for every edit a grown-up makes: the slots that change the store are
// timed under one name, so a profile shows what a tap in the settings or a reset
// costs, whichever it was. The zone is made in the constructor, once, so there is
// one place for the name.
class EditScope {
public:
    EditScope() = default;
    EditScope(const EditScope&) = delete;
    EditScope& operator=(const EditScope&) = delete;

private:
    wjs::profiling::Zone zone_ = wjs::profiling::zone<"parent.edit">();
};

}  // namespace

ParentController::ParentController(QObject* parent)
    : QObject(parent), gate_(wjs::gate_layout(1024.0, 768.0, 48.0)) {
    // Until the program hands it the real one, the screens have a store that
    // keeps nothing, so that none of them has to ask whether there is one.
    owned_.emplace(wjs::Store::in_memory());
    store_ = &*owned_;
    refresh();
}

ParentController::~ParentController() = default;

void ParentController::useStore(wjs::Store* store, wjs::LoadStatus status) {
    if (store == nullptr) {
        return;
    }
    store_ = store;
    owned_.reset();
    status_ = status;
    refresh();
}

void ParentController::usePhases(const wjs::PhaseSet* phases) {
    phases_ = phases;
    refresh();
}

void ParentController::useCalendar(std::function<wjs::Day()> today, int first_day_of_week) {
    today_ = std::move(today);
    first_day_of_week_ = first_day_of_week;
    refresh();
}

void ParentController::useClock(std::function<qint64()> now) {
    clock_ = std::move(now);
}

qint64 ParentController::now() const {
    if (clock_) {
        return clock_();
    }
    return static_cast<qint64>(std::chrono::duration_cast<milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

void ParentController::openStore(const std::filesystem::path& file) {
    wjs::OpenResult opened = wjs::Store::open(file);
    status_ = opened.status;
    owned_.emplace(std::move(opened.store));
    store_ = &*owned_;
    refresh();
}

// ---------------------------------------------------------------------------
// The gate

QVariantList ParentController::gateSpots() const {
    QVariantList spots;
    for (const wjs::Spot& spot : {gate_.layout().first, gate_.layout().second}) {
        QVariantMap one;
        one.insert(QStringLiteral("x"), spot.centre.x);
        one.insert(QStringLiteral("y"), spot.centre.y);
        one.insert(QStringLiteral("r"), spot.radius);
        spots.append(one);
    }
    return spots;
}

void ParentController::afterGateEvent(milliseconds now) {
    const bool holding = gate_.state() == wjs::GateState::holding;
    const double progress = gate_.progress(now);
    if (holding != holding_ || progress != progress_) {
        holding_ = holding;
        progress_ = progress;
        emit gateProgressChanged();
    }
    const bool open = gate_.is_open();
    if (open != unlocked_) {
        unlocked_ = open;
        if (open) {
            // The dashboard is worked out when it is opened, as of this moment.
            refresh();
        }
        emit unlockedChanged();
    }
}

void ParentController::layoutGate(double width, double height, double minRadius) {
    gate_.set_layout(wjs::gate_layout(width, height, minRadius));
    emit gateSpotsChanged();
    afterGateEvent(milliseconds{0});
}

void ParentController::gateTouchDown(int id, double x, double y, qint64 ms) {
    gate_.touch_down(id, {x, y}, milliseconds{ms});
    afterGateEvent(milliseconds{ms});
}

void ParentController::gateTouchMove(int id, double x, double y, qint64 ms) {
    gate_.touch_move(id, {x, y}, milliseconds{ms});
    afterGateEvent(milliseconds{ms});
}

void ParentController::gateTouchUp(int id, qint64 ms) {
    gate_.touch_up(id, milliseconds{ms});
    afterGateEvent(milliseconds{ms});
}

void ParentController::gateTick(qint64 ms) {
    gate_.tick(milliseconds{ms});
    afterGateEvent(milliseconds{ms});
}

void ParentController::unlockForAssistiveTechnology() {
    gate_.unlock_for_assistive_technology();
    afterGateEvent(milliseconds{0});
}

void ParentController::lock() {
    gate_.relock();
    afterGateEvent(milliseconds{0});
}

// ---------------------------------------------------------------------------
// What the screens show

const wjs::PhaseIndex* ParentController::activeIndex() const {
    return phases_ != nullptr ? phases_->find(store_->settings().phase) : nullptr;
}

QStringList ParentController::focusChoices() const {
    const wjs::PhaseIndex* index = activeIndex();
    return index != nullptr ? list(wjs::focus_sound_choices(*index)) : QStringList{};
}

QStringList ParentController::phases() const {
    if (phases_ == nullptr) {
        // Generic names; what each holds is the reviewer's (dashboard.md).
        return {QStringLiteral("A"), QStringLiteral("B")};
    }
    QStringList names;
    for (const wjs::PhaseIndex& index : phases_->phases()) {
        names.append(to_qstring(index.phase()));
    }
    return names;
}

bool ParentController::retrySave() {
    // update_settings writes the file and records how the write went, which
    // save() alone does not: a save that works must also take "unsaved" away.
    store_->update_settings(store_->settings());
    updateNote();
    return store_->persistent() && !store_->last_save_error().has_value();
}

void ParentController::refresh() {
    // The device's own calendar: Qt's Julian day of 1970-01-01 is 2440588.
    const wjs::Day today =
        today_ ? today_() : static_cast<wjs::Day>(QDate::currentDate().toJulianDay() - 2440588);
    const int first_day =
        first_day_of_week_ > 0 ? first_day_of_week_ : static_cast<int>(QLocale().firstDayOfWeek());
    dashboard_ =
        dashboard_map(wjs::build_dashboard(store_->data(), activeIndex(), today, first_day));
    settings_ = settings_map(store_->settings());
    emit dashboardChanged();
    emit settingsChanged();
    updateNote();
}

void ParentController::updateNote() {
    QString note;
    switch (status_) {
        case wjs::LoadStatus::unreadable:
            note = QStringLiteral("unreadable");
            break;
        case wjs::LoadStatus::newer_version:
            note = QStringLiteral("newer");
            break;
        case wjs::LoadStatus::cannot_read:
            note = QStringLiteral("unsaved");
            break;
        case wjs::LoadStatus::created:
        case wjs::LoadStatus::loaded:
        case wjs::LoadStatus::migrated:
            if (!store_->persistent() || store_->last_save_error().has_value()) {
                note = QStringLiteral("unsaved");
            }
            break;
    }
    if (note != note_) {
        note_ = note;
        emit storeNoteChanged();
    }
}

QString ParentController::storeNote() const {
    return note_;
}

int ParentController::lifetimeStars() const {
    return static_cast<int>(store_->data().stars);
}

int ParentController::lifetimePoints() const {
    int total = 0;
    for (const wjs::VaultWord& word : store_->data().vault) {
        total += wjs::word_points(word.word) * static_cast<int>(word.found);
    }
    return total;
}

int ParentController::lifetimeWords() const {
    return static_cast<int>(store_->data().vault.size());
}

QString ParentController::longestWord() const {
    std::string longest;
    for (const wjs::VaultWord& word : store_->data().vault) {
        if (word.word.size() > longest.size() ||
            (word.word.size() == longest.size() && word.word < longest)) {
            longest = word.word;
        }
    }
    return to_qstring(longest);
}

int ParentController::boardsKept() const {
    return static_cast<int>(store_->data().boards.size());
}

QVariantList ParentController::recentBoards() const {
    QVariantList list;
    const wjs::StoreData& data = store_->data();
    for (auto board = data.boards.rbegin(); board != data.boards.rend(); ++board) {
        QVariantMap map;
        map.insert(QStringLiteral("rows"), board->size);
        map.insert(QStringLiteral("columns"), board->size);
        map.insert(QStringLiteral("stars"), board->stars);
        map.insert(QStringLiteral("found"), board->found);
        map.insert(QStringLiteral("cleared"), board->cleared);
        map.insert(QStringLiteral("day"), static_cast<qint64>(board->day));

        // The tiles are kept "A,B/C,D"; BoardThumb wants them row-major.
        QStringList tiles;
        const QString packed = to_qstring(board->tiles);
        const QStringList rows = packed.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString& row : rows) {
            const QStringList faces = row.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString& face : faces) {
                tiles.append(face);
            }
        }
        map.insert(QStringLiteral("tiles"), tiles);

        QVariantList words;
        for (const wjs::FindRecord& find : data.finds) {
            if (find.board != board->serial) {
                continue;
            }
            QVariantMap word;
            word.insert(QStringLiteral("word"), to_qstring(find.word));
            word.insert(QStringLiteral("stars"), find.stars);
            word.insert(QStringLiteral("points"), wjs::word_points(find.word));
            word.insert(QStringLiteral("assisted"), find.start_tile_used);
            word.insert(QStringLiteral("tier"), tier_name(find.word.size()));
            words.append(word);
        }
        map.insert(QStringLiteral("words"), words);
        list.append(map);
    }
    return list;
}

QString ParentController::motion() const {
    return to_qstring(to_string(store_->settings().motion));
}

QString ParentController::colourScheme() const {
    return to_qstring(to_string(store_->settings().colour_scheme));
}

QString ParentController::language() const {
    return to_qstring(store_->settings().language);
}

QString ParentController::helpLevel() const {
    return to_qstring(to_string(store_->settings().help));
}

QString ParentController::soloClock() const {
    return to_qstring(to_string(store_->settings().solo_clock));
}

int ParentController::soloClockMinutes() const {
    return store_->settings().solo_clock_minutes;
}

QVariantList ParentController::timerLengthChoices() const {
    QVariantList list;
    for (const int minutes : wjs::solo_clock_minutes_choices) {
        list.append(minutes);
    }
    return list;
}

bool ParentController::reducedMotion() const {
    return store_->settings().motion == wjs::MotionSetting::reduced;
}

double ParentController::textScale() const {
    return static_cast<double>(store_->settings().text_size_percent) / 100.0;
}

QString ParentController::version() const {
    return to_qstring(wjs::project_version);
}

QString ParentController::versionDetails() const {
    return to_qstring(wjs::version_details());
}

// The notice is not one of the parent area's sentences: it is a legal text, in
// English, the same in every language, and it comes from the file rather than
// from ParentWording.qml. A build without the resource - the app tests compile
// this controller without the application's resources - says where the file is
// instead of showing nothing.
QString ParentController::licenceNotices() const {
    QFile file(QStringLiteral(":/notices/NOTICES.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral(
            "The licence notices are installed as NOTICES.txt beside this program.");
    }
    return QString::fromUtf8(file.readAll());
}

void ParentController::applySettings(wjs::Settings settings) {
    settings = settings.normalized();
    // Only sounds of the phase can be ticked: held to it whenever it is known.
    if (const wjs::PhaseIndex* index =
            phases_ != nullptr ? phases_->find(settings.phase) : nullptr) {
        settings = wjs::restrict_focus_sounds(std::move(settings), *index);
    }
    const bool changed = !(settings == store_->settings());
    // A setting that is already what it is asks nothing of the file.
    if (changed) {
        store_->update_settings(settings);
    }
    refresh();
    if (changed) {
        emit storeEdited();
    }
}

bool ParentController::setSetting(const QString& key, const QVariant& value) {
    const EditScope edit_scope;
    wjs::Settings s = store_->settings();

    if (key == QLatin1String("help") && is_string(value)) {
        const auto level = wjs::help_level_from_string(to_utf8(value.toString()));
        if (!level) {
            return false;
        }
        s.help = *level;
    } else if (key == QLatin1String("board") && is_string(value)) {
        const auto mode = wjs::board_mode_from_string(to_utf8(value.toString()));
        // The Phonics board needs a phase's words.
        if (!mode || (*mode == wjs::BoardMode::phonics && !phonicsAvailable())) {
            return false;
        }
        s.board = *mode;
    } else if (key == QLatin1String("phase") && is_string(value)) {
        if (!phases().contains(value.toString())) {
            return false;
        }
        s.phase = to_utf8(value.toString());
    } else if (key == QLatin1String("boardSize")) {
        const auto size = whole_number(value);
        // The standard board plays 3x3 to 6x6; the phonics board 3x3 and 4x4.
        const int largest = s.board == wjs::BoardMode::standard ? wjs::largest_standard_board_size
                                                                : wjs::largest_default_board_size;
        if (!size || *size < wjs::smallest_default_board_size || *size > largest) {
            return false;
        }
        s.board_size = *size;
    } else if (key == QLatin1String("soundItOut") && is_string(value)) {
        const auto parsed = wjs::sound_it_out_from_string(to_utf8(value.toString()));
        if (!parsed) {
            return false;
        }
        s.sound_it_out = *parsed;
    } else if (key == QLatin1String("celebrationSounds") && is_string(value)) {
        const auto parsed = wjs::celebration_sounds_from_string(to_utf8(value.toString()));
        if (!parsed) {
            return false;
        }
        s.celebration_sounds = *parsed;
    } else if (key == QLatin1String("sound") && is_bool(value)) {
        s.sound = value.toBool();
    } else if (key == QLatin1String("vibration") && is_bool(value)) {
        s.vibration = value.toBool();
    } else if (key == QLatin1String("shake") && is_bool(value)) {
        s.shake = value.toBool();
    } else if (key == QLatin1String("motion") && is_string(value)) {
        const auto parsed = wjs::motion_from_string(to_utf8(value.toString()));
        if (!parsed) {
            return false;
        }
        s.motion = *parsed;
    } else if (key == QLatin1String("colourScheme") && is_string(value)) {
        const auto parsed = wjs::colour_scheme_from_string(to_utf8(value.toString()));
        if (!parsed) {
            return false;
        }
        s.colour_scheme = *parsed;
    } else if (key == QLatin1String("timer") && is_string(value)) {
        const auto parsed = wjs::solo_clock_from_string(to_utf8(value.toString()));
        if (!parsed) {
            return false;
        }
        s.solo_clock = *parsed;
    } else if (key == QLatin1String("timerLength")) {
        const auto minutes = whole_number(value);
        if (!minutes || std::find(std::begin(wjs::solo_clock_minutes_choices),
                                  std::end(wjs::solo_clock_minutes_choices),
                                  *minutes) == std::end(wjs::solo_clock_minutes_choices)) {
            return false;
        }
        s.solo_clock_minutes = *minutes;
    } else if (key == QLatin1String("textSizePercent")) {
        const auto percent = whole_number(value);
        if (!percent ||
            std::find(std::begin(wjs::text_size_percents), std::end(wjs::text_size_percents),
                      *percent) == std::end(wjs::text_size_percents)) {
            return false;
        }
        s.text_size_percent = *percent;
    } else if (key == QLatin1String("schoolFont") && is_string(value)) {
        // Only a font that can be bundled can be named, and none is bundled.
        if (!value.toString().isEmpty() && !schoolFonts().contains(value.toString())) {
            return false;
        }
        s.school_font = to_utf8(value.toString());
    } else if (key == QLatin1String("language") && is_string(value)) {
        // One of the eight, lower case, or empty for the device's own.
        std::string code = to_utf8(value.toString());
        std::transform(code.begin(), code.end(), code.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!code.empty() && !wjs::is_valid_language(code)) {
            return false;
        }
        s.language = code;
    } else {
        return false;
    }

    applySettings(std::move(s));
    return true;
}

bool ParentController::toggleFocusSound(const QString& sound) {
    const EditScope edit_scope;
    if (!focusChoices().contains(sound)) {
        return false;
    }
    wjs::Settings s = store_->settings();
    const std::string name = to_utf8(sound);
    const auto ticked = std::find(s.focus_sounds.begin(), s.focus_sounds.end(), name);
    if (ticked != s.focus_sounds.end()) {
        s.focus_sounds.erase(ticked);
    } else {
        s.focus_sounds.push_back(name);
    }
    applySettings(std::move(s));
    return true;
}

void ParentController::resetBoardsAndFinds() {
    const EditScope edit_scope;
    store_->reset_boards_and_finds();
    refresh();
    emit storeEdited();
}

void ParentController::resetStarsAndStickers() {
    const EditScope edit_scope;
    store_->reset_stars_and_stickers();
    refresh();
    emit storeEdited();
}

}  // namespace wjs::app
