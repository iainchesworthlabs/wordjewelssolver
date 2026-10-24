// SPDX-License-Identifier: GPL-3.0-or-later
#include "real_solo_game.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>
#include <utility>

#include "wjs/board.hpp"
#include "wjs/game_rules.hpp"
#include "wjs/profiling.hpp"
#include "wjs/scoring.hpp"
#include "wjs/seeding.hpp"
#include "wjs/session_store.hpp"

namespace wjs::app {
namespace {

[[nodiscard]] std::string lower(std::string word) {
    std::ranges::transform(word, word.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return word;
}

[[nodiscard]] WordTier tier_of(wjs::Tier tier) noexcept {
    switch (tier) {
        case wjs::Tier::spark:
            return WordTier::spark;
        case wjs::Tier::power:
            return WordTier::power;
        case wjs::Tier::mega:
            return WordTier::mega;
    }
    return WordTier::spark;
}

// A word in the tray: lower-case, whatever case it was kept in.
[[nodiscard]] SoloFound tray_word(std::string_view word, int stars) {
    SoloFound found;
    found.word = lower(std::string(word));
    found.tier = tier_of(wjs::tier_for_letters(word.size()).value_or(wjs::Tier::spark));
    found.stars = stars;
    found.points = wjs::word_points(word);
    return found;
}

[[nodiscard]] SoloFound tray_word(const wjs::Find& find) {
    return tray_word(find.word, find.reward().stars);
}

[[nodiscard]] SoloState solo_state_of(wjs::SessionState state) noexcept {
    switch (state) {
        case wjs::SessionState::box:
            return SoloState::box;
        case wjs::SessionState::shaking:
            return SoloState::shaking;
        case wjs::SessionState::dropping:
            return SoloState::dropping;
        case wjs::SessionState::playing:
            return SoloState::playing;
        case wjs::SessionState::cleared:
            return SoloState::cleared;
        case wjs::SessionState::resting:
            return SoloState::resting;
    }
    return SoloState::box;
}

// The letter the phase leaves out of every board, which is what the session
// writes on a dead jewel in a record. Empty when the phase cannot make a board.
[[nodiscard]] std::string dead_letter_of(const wjs::PhaseIndex& phase) {
    const auto options = wjs::phonics_board_options(phase, 3);
    if (!options) {
        return {};
    }
    return std::string(
        1, static_cast<char>(std::toupper(static_cast<unsigned char>(options->dead_letter))));
}

}  // namespace

RealSoloGame::RealSoloGame(wjs::GameSession session, bool help_allowed,
                           const wjs::PhaseIndex* phase, wjs::Store& store,
                           std::function<wjs::Day()> today)
    : session_(std::move(session)),
      help_allowed_(help_allowed),
      phase_(phase),
      store_(&store),
      today_(std::move(today)),
      binder_(phase != nullptr ? std::optional<wjs::GraphemeBinder>{*phase} : std::nullopt),
      dead_letter_(phase != nullptr ? dead_letter_of(*phase) : std::string{}),
      sizes_count_(session_.mode() == wjs::BoardMode::standard ? sizes_.size() : 2),
      board_(scene_board()) {
    refresh_found();
    save_session();
}

std::expected<RealSoloGame, wjs::SessionError> RealSoloGame::create(
    const wjs::SessionData& data, wjs::Store& store, const wjs::SessionOptions& options,
    std::function<wjs::Day()> today) {
    // The game plays whichever board the options name; the session itself
    // refuses a mode whose data is missing (no dice on the standard board, no
    // phase on the phonics board).
    auto session = wjs::GameSession::create(data, options);
    if (!session) {
        return std::unexpected(session.error());
    }
    return RealSoloGame(*std::move(session), options.help, data.phase, store, std::move(today));
}

SoloState RealSoloGame::state() const {
    return solo_state_of(session_.state());
}

std::size_t RealSoloGame::size() const {
    return wjs::side(session_.size());
}

void RealSoloGame::chooseSize(std::size_t size) {
    const auto chosen = wjs::board_size_for(size);
    // Only the box takes a choice, and only a choice that took is the last used.
    if (chosen && session_.choose_size(*chosen)) {
        remember_size(*chosen);
    }
}

bool RealSoloGame::shake() {
    if (!session_.shake()) {
        return false;
    }
    board_ = scene_board();
    found_.clear();
    return true;
}

void RealSoloGame::settled() {
    // The scene reports one moment for the shake, the lid and the drop, which the
    // session has as two. Each applies in its own state and ignores the others, so
    // from `shaking` both run and from `dropping` the second does.
    static_cast<void>(session_.still());
    static_cast<void>(session_.landed());
    save_session();
}

TraceOutcome RealSoloGame::submit(std::span<const Position> path) {
    TraceOutcome outcome;
    const wjs::TraceOutcome traced = session_.trace(path, today_());

    switch (traced.kind) {
        case wjs::TraceKind::found:
        case wjs::TraceKind::found_again: {
            if (!traced.find) {
                break;
            }
            outcome.kind = WordKind::viable;
            outcome.found = tray_word(*traced.find);
            outcome.alreadyFound = traced.kind == wjs::TraceKind::found_again;
            if (traced.kind == wjs::TraceKind::found) {
                refresh_found();
                outcome.cleared = traced.cleared;
                save_session();
                const wjs::RecordOutcome recorded = record_board();
                // Recorded silently, and meant for one sparkle inside the last word's
                // celebration: the store says so only for a board that has just cleared.
                outcome.newBest = recorded.new_best && traced.cleared;
                outcome.sticker =
                    traced.sticker() && std::ranges::find(recorded.new_words, outcome.found.word) !=
                                            recorded.new_words.end();
            }
            break;
        }
        case wjs::TraceKind::real_not_todays:
            // The kind and no word: nothing in the answer could be shown or read.
            outcome.kind = WordKind::realNotTodays;
            break;
        case wjs::TraceKind::not_a_word:
        case wjs::TraceKind::ignored:
            break;
    }
    return outcome;
}

std::vector<GraphemeBinder::Block> RealSoloGame::blocks(std::span<const Position> path) const {
    // The standard board has no phase, so no graphemes to bind: every tile is a
    // block of its own and there are no shared bars to draw.
    if (!binder_) {
        return {};
    }
    const wjs::Board* board = session_.board();
    if (board == nullptr ||
        !std::ranges::all_of(path, [&](Position position) { return board->contains(position); })) {
        return {};
    }
    return binder_->bind(*board, path);
}

void RealSoloGame::celebrated() {
    static_cast<void>(session_.rest());
    save_session();
}

std::optional<Position> RealSoloGame::startTile() {
    // The help press: the session's choice of a word, and the write of the game
    // in progress that follows it.
    const auto zone = wjs::profiling::zone<"solo.help">();
    const auto tile = session_.request_help();
    save_session();
    return tile;
}

std::optional<Position> RealSoloGame::litTile() const {
    return session_.lit_tile();
}

std::vector<Position> RealSoloGame::helpPath() const {
    return session_.lit_path();
}

std::size_t RealSoloGame::wordsWithStart(std::span<const Position> prefix) const {
    // Once per find made from a hint's start: a count over the board's words.
    const auto zone = wjs::profiling::zone<"solo.words_with_start">();
    return session_.words_with_start(prefix);
}

void RealSoloGame::dismissHelp() {
    session_.dismiss_help();
    save_session();
}

void RealSoloGame::lidOn() {
    // The board that was in play is kept by the session until the next shake, so
    // it can be handed over whichever way it was left.
    if (session_.back()) {
        record_board();
        save_session();
    }
}

void RealSoloGame::again() {
    // The next board's dealing waits behind this press: a new box, and a write.
    const auto zone = wjs::profiling::zone<"solo.again">();
    static_cast<void>(session_.again());
    save_session();
}

std::vector<SoloHistoryEntry> RealSoloGame::history() const {
    const wjs::StoreData& data = store_->data();
    std::vector<SoloHistoryEntry> entries;
    entries.reserve(data.boards.size());
    for (auto stored = data.boards.rbegin(); stored != data.boards.rend(); ++stored) {
        const auto parsed = wjs::Board::parse(stored->tiles);
        if (!parsed) {
            continue;
        }
        // A tile with the phase's dead letter is a dead jewel on a board of this
        // phase; on a board of another the letter is not ours to read.
        const bool ours = !dead_letter_.empty() && stored->phase == phase_->phase();
        SoloHistoryEntry entry;
        entry.board.rows = parsed->rows();
        entry.board.columns = parsed->columns();
        for (std::size_t row = 0; row < parsed->rows(); ++row) {
            for (std::size_t column = 0; column < parsed->columns(); ++column) {
                const std::string& tile = parsed->tile({row, column});
                entry.board.tiles.push_back(ours && tile == dead_letter_ ? std::string{} : tile);
            }
        }
        for (const wjs::FindRecord& find : wjs::finds_on_board(data, stored->serial)) {
            entry.found.push_back(tray_word(find.word, find.stars));
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

SoloBoard RealSoloGame::scene_board() const {
    const wjs::Board* board = session_.board();
    if (board == nullptr) {
        return {};
    }
    SoloBoard scene;
    scene.rows = board->rows();
    scene.columns = board->columns();
    const std::span<const Position> dead = session_.dead_tiles();
    scene.tiles.reserve(scene.rows * scene.columns);
    for (std::size_t row = 0; row < scene.rows; ++row) {
        for (std::size_t column = 0; column < scene.columns; ++column) {
            const Position position{row, column};
            const bool is_dead = std::ranges::find(dead, position) != dead.end();
            scene.tiles.push_back(is_dead ? std::string{} : board->tile(position));
        }
    }
    if (const auto& deal = session_.last_deal()) {
        scene.seed = deal->seed;
        // A dealt standard board is the roll of its seed (docs/architecture.md,
        // "Dice"), so the dice that made it are the library's: the six faces of
        // the die on each tile and the face that is up, from
        // wjs::roll_with_placements. The phonics board has no dice, and a board
        // the store brought back keeps no roll seed: both are left without
        // faces, and the scene draws the tile's letter on the face that is up.
        if (session_.mode() == wjs::BoardMode::standard) {
            const wjs::DiceSets* sets = session_.dice();
            const wjs::DiceSet* dice = sets != nullptr ? sets->find(scene.rows) : nullptr;
            if (dice != nullptr) {
                const auto rolled = wjs::roll_with_placements(*dice, scene.seed);
                if (rolled && rolled->placements.size() == scene.tiles.size()) {
                    scene.faces.reserve(scene.tiles.size());
                    scene.up.reserve(scene.tiles.size());
                    for (const wjs::Placement& placement : rolled->placements) {
                        scene.faces.push_back(dice->die(placement.die));
                        scene.up.push_back(static_cast<int>(placement.face));
                    }
                }
            }
        }
    }
    return scene;
}

void RealSoloGame::refresh_found() {
    found_.clear();
    for (const wjs::Find& find : session_.finds()) {
        found_.push_back(tray_word(find));
    }
}

void RealSoloGame::note_write(bool accepted) {
    save_failed_ = !accepted || store_->last_save_error().has_value();
}

wjs::RecordOutcome RealSoloGame::record_board() {
    const auto record = session_.record();
    if (!record) {
        return {};
    }
    const auto snapshot = wjs::snapshot_of(*record);
    if (!snapshot) {
        // A board the store has no form for: not a failure of the store, and
        // nothing to keep.
        return {};
    }
    wjs::RecordOutcome outcome = store_->record(*snapshot, today_());
    note_write(outcome.stored);
    return outcome;
}

void RealSoloGame::save_session() {
    std::optional<wjs::SavedSession> saved;
    // A game in the box (and the shake and the drop, which the session saves as the
    // box) is not one in progress.
    if (const wjs::SessionSnapshot snapshot = session_.snapshot();
        snapshot.state != wjs::SessionState::box) {
        saved = wjs::saved_session_of(snapshot);
    }
    if (store_->session() == saved) {
        return;
    }
    // A board the store cannot keep whole is refused and nothing changes, which
    // would leave the game before it to be brought back in this one's place.
    if (store_->save_session(saved)) {
        note_write(true);
        return;
    }
    // Refused: the game before this one must not come back in its place, so what
    // the store held is cleared (clearing is never refused: there is nothing to
    // keep). The game cannot be resumed, and that is told.
    std::ignore = store_->save_session(std::nullopt);
    note_write(false);
}

void RealSoloGame::remember_size(wjs::BoardSize size) {
    wjs::Settings settings = store_->settings();
    const int side = static_cast<int>(wjs::side(size));
    if (settings.board_size != side) {
        settings.board_size = side;
        store_->update_settings(settings);
        note_write(true);
    }
}

}  // namespace wjs::app
