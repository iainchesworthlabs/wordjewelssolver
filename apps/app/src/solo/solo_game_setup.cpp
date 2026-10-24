// SPDX-License-Identifier: GPL-3.0-or-later
#include "solo_game_setup.hpp"

#include <random>
#include <utility>

#include "real_solo_game.hpp"
#include "wjs/game_session.hpp"
#include "wjs/session_store.hpp"

namespace wjs::app {
namespace {

// A path in a sentence, as UTF-8 whatever the platform's own encoding.
[[nodiscard]] std::string text_of(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

}  // namespace

SoloGameSetup::SoloGameSetup(wjs::Dictionary dictionary, wjs::DiceSets dice, wjs::PhaseSet phases,
                             std::string phase, wjs::Store& store,
                             std::optional<std::uint32_t> seed, std::function<wjs::Day()> today)
    : dictionary_(std::move(dictionary)),
      dice_(std::move(dice)),
      phases_(std::move(phases)),
      phase_(phases_.find(phase)),
      store_(store),
      seed_(seed),
      today_(std::move(today)) {}

SoloGameSetup::~SoloGameSetup() = default;

std::expected<std::unique_ptr<SoloGameSetup>, std::string> SoloGameSetup::open(
    const SoloGameOptions& options, wjs::Store& store) {
    // A find is stamped with the day it was made, and a store given a wrong day
    // keeps it: no calendar is an error here and not a day made up.
    if (!options.today) {
        return std::unexpected("No calendar was given to take the day of a find from.");
    }
    auto dictionary = wjs::Dictionary::load_file(options.dictionary);
    if (!dictionary) {
        return std::unexpected("The word list " + text_of(options.dictionary) +
                               " could not be read (" + std::string(to_string(dictionary.error())) +
                               "). Start the program with --dictionary <file>.");
    }

    // The standard board's dice, for every size it is played at: a set that
    // will not load fails every session the same way, and here it can be said.
    auto dice = wjs::DiceSets::load_directory(options.dice_directory);
    if (!dice) {
        return std::unexpected("The dice in " + text_of(options.dice_directory) +
                               " could not be read (" + std::string(to_string(dice.error())) +
                               "). They are the standard board's, 3x3.txt to 6x6.txt beside the "
                               "program.");
    }

    if (!options.phase_directory.empty()) {
        // The phases of the directory, and the one to play: the phonics board's
        // data, loaded only when the program was told where it is.
        auto loaded = wjs::PhaseSet::load_directory(options.phase_directory, *dictionary);
        if (!loaded) {
            std::string message =
                "The phase files in " + text_of(options.phase_directory) + " could not be used:";
            for (const wjs::DataProblem& problem : loaded.error()) {
                message += "\n  " + wjs::to_string(problem);
            }
            return std::unexpected(std::move(message));
        }

        // The phase asked for, else the one the settings name. A settings phase this
        // directory does not hold (the files changed since the parent chose it) is
        // not a reason to stay out of the game: the first phase is played.
        std::string name = options.phase.empty() ? store.settings().phase : options.phase;
        const wjs::PhaseIndex* index = loaded->find(name);
        if (index == nullptr && options.phase.empty() && !loaded->phases().empty()) {
            index = &loaded->phases().front();
            name = index->phase();
        }
        if (index == nullptr) {
            std::string message = "There is no phase " + name + " in " +
                                  text_of(options.phase_directory) + "; it has:";
            for (const wjs::PhaseIndex& known : loaded->phases()) {
                message += " " + known.phase();
            }
            return std::unexpected(std::move(message));
        }

        // The session is made from what the settings say each time (make()), but a
        // phase that cannot make a board fails the same way every time, and here it
        // can be said.
        wjs::SessionOptions phonics;
        phonics.mode = wjs::BoardMode::phonics;
        const auto trial =
            wjs::GameSession::create({.dictionary = *dictionary, .phase = index}, phonics);
        if (!trial) {
            return std::unexpected("Phase " + name + " cannot be played: " +
                                   std::string(to_string(trial.error())) + ".");
        }

        return std::unique_ptr<SoloGameSetup>(
            new SoloGameSetup(*std::move(dictionary), *std::move(dice), *std::move(loaded), name,
                              store, options.seed, options.today));
    }

    // No phase directory: an empty phase set, which is what the standard board
    // and the parent area see. An empty build has nothing to check, so this only
    // fails if the word list itself cannot be used.
    auto empty = wjs::PhaseSet::build({}, {}, *dictionary);
    if (!empty) {
        return std::unexpected("The word list " + text_of(options.dictionary) + " cannot be used.");
    }
    return std::unique_ptr<SoloGameSetup>(new SoloGameSetup(*std::move(dictionary),
                                                            *std::move(dice), *std::move(empty), {},
                                                            store, options.seed, options.today));
}

std::unique_ptr<SoloGame> SoloGameSetup::make() {
    const wjs::Settings& settings = store_.settings();
    wjs::SessionOptions options;
    // The standard board is the default. The phonics board is played only when
    // the parent chose it and a phase was loaded: without one the switch cannot
    // be honoured, and the standard board is played.
    options.mode = settings.board == wjs::BoardMode::phonics && phase_ != nullptr
                       ? wjs::BoardMode::phonics
                       : wjs::BoardMode::standard;
    const std::optional<wjs::BoardSize> chosen =
        wjs::board_size_for(static_cast<std::size_t>(settings.board_size));
    options.size =
        chosen && wjs::is_played_at(options.mode, *chosen) ? *chosen : wjs::BoardSize::three;
    options.seed = seed_ ? *seed_ : std::random_device{}();
    options.help = settings.help != wjs::HelpLevel::off;
    options.focus_sounds = settings.focus_sounds;

    // The game the last run left in progress comes back as it was. One that cannot
    // (the board is the other mode's now, the phase is another now, the phase no
    // longer holds the board's words, or the record is not one a session makes) is
    // not the child's to hear of: a new game begins in the box and the old record
    // is cleared by it.
    const wjs::SessionData data{.dictionary = dictionary_, .dice = &dice_, .phase = phase_};
    if (const auto& saved = store_.session()) {
        if (auto snapshot = wjs::session_snapshot_of(*saved)) {
            if (!options.help) {
                // The tile is lit by a press the parent no longer allows.
                snapshot->lit.reset();
            }
            if (auto restored = wjs::GameSession::restore(*snapshot, data, options)) {
                return std::make_unique<RealSoloGame>(*std::move(restored), options.help, phase_,
                                                      store_, today_);
            }
        }
    }

    // open() loaded everything the mode needs, and the options above stay within
    // what the mode is played at, so a session is made. If one ever is not (a mode
    // gains a requirement open() does not check), that is no exception out of an
    // object QML makes: there is no game, and the factory plays none.
    auto created = RealSoloGame::create(data, store_, options, today_);
    if (!created) {
        return nullptr;
    }
    return std::make_unique<RealSoloGame>(*std::move(created));
}

}  // namespace wjs::app
