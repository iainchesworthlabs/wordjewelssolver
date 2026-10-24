// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "solo_game.hpp"
#include "wjs/calendar.hpp"
#include "wjs/dice_sets.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/phase_data.hpp"
#include "wjs/store.hpp"

// What the real game needs to exist, loaded and owned in one place for the
// program's whole run: the English word list always, the dice of the standard
// board, and the phases of a data directory when the program was told where one
// is. The store is the program's one (main.cpp opens it and the parent area
// shares it), held here by reference. The game (RealSoloGame) holds the phase
// and the store by reference too, so this outlives every game it makes.
// Qt-free; the program supplies the paths and the calendar.
//
// The standard board (dice, 3x3 to 6x6, the full dictionary) is the default and
// needs no phase directory. The phonics board is played when the parent's
// switch is on and a phase directory was given: the repository has no phase to
// load (the files are written and reviewed by a person qualified to do it,
// docs/design/phonics.md), and a phase given that way is reported as test data
// unless every file in it carries a complete review header.

namespace wjs::app {

struct SoloGameOptions {
    // The directory of phase-<x>.graphemes.txt and phase-<x>.words.txt files.
    // Empty for the standard board alone, which is the default.
    std::filesystem::path phase_directory;
    // The phase to play ("A"); empty for the one in the store's settings.
    std::string phase;
    // The English word list: the standard board plays every word of it, and the
    // session tells a real word from a string of letters with it.
    std::filesystem::path dictionary;
    // The directory of the standard board's dice, 3x3.txt to 6x6.txt.
    std::filesystem::path dice_directory;
    // The seed of the run's boards; a random one when not given. A fixed seed
    // makes a run repeat, which a test and a screenshot want.
    std::optional<std::uint32_t> seed;
    // The device's calendar day. Required: open() refuses without it.
    std::function<wjs::Day()> today;
};

class SoloGameSetup {
public:
    // Loads everything the game needs, or says in a sentence why it cannot: the
    // dictionary or the dice cannot be read, the phase files cannot be read
    // (every problem of the files is listed), the phase asked for is not in
    // them, or it cannot make a board. `store` is the program's own, which must
    // outlive the setup.
    [[nodiscard]] static std::expected<std::unique_ptr<SoloGameSetup>, std::string> open(
        const SoloGameOptions& options, wjs::Store& store);

    SoloGameSetup(const SoloGameSetup&) = delete;
    SoloGameSetup& operator=(const SoloGameSetup&) = delete;
    ~SoloGameSetup();

    // The game to play, over the store, with the parent's settings as they are
    // now (the board, the size to offer first, the help, the sounds in focus).
    // A change made in the meantime is read by the next game made: the program
    // makes one when the parent has changed a setting (SoloModel::storeEdited),
    // and at its next start. The standard board is the default; the phonics
    // board is played when the parent chose it and a phase was loaded, and the
    // standard board otherwise.
    //
    // If the store holds a game in progress of the board being played (a board
    // on the table, or the resting screen), that game, as it was left. Otherwise
    // a new game in the box: the store holds none, or the one it holds cannot
    // come back (GameSession::restore refuses it: the other board mode, another
    // phase, a board the words no longer fit, a record no session makes), and is
    // then cleared. Nothing is said of either to the child. Null only if a
    // session cannot be made at all, which open() has checked it can; the
    // factory (make_solo_game) then plays no game rather than fail in QML.
    [[nodiscard]] std::unique_ptr<SoloGame> make();

    // The phase the game plays; null when no phase directory was given (the
    // standard board).
    [[nodiscard]] const wjs::PhaseIndex* phase() const noexcept { return phase_; }
    // The English word list every game is played against, and the Family
    // Assistant's words too: the program's Mode A maker is made from the same
    // list (main.cpp).
    [[nodiscard]] const wjs::Dictionary& dictionary() const noexcept { return dictionary_; }
    // Every phase of the directory, which the parent area chooses among. Empty
    // when no phase directory was given.
    [[nodiscard]] const wjs::PhaseSet& phases() const noexcept { return phases_; }
    // Every phase file carried a complete review; otherwise the files are test
    // data. True when no phase directory was given (there is no file to check).
    [[nodiscard]] bool reviewed() const noexcept {
        return phases_.phases().empty() || phases_.reviewed();
    }
    [[nodiscard]] const std::vector<std::string>& unreviewed_files() const noexcept {
        return phases_.unreviewed_files();
    }

private:
    SoloGameSetup(wjs::Dictionary dictionary, wjs::DiceSets dice, wjs::PhaseSet phases,
                  std::string phase, wjs::Store& store, std::optional<std::uint32_t> seed,
                  std::function<wjs::Day()> today);

    wjs::Dictionary dictionary_;
    wjs::DiceSets dice_;
    wjs::PhaseSet phases_;
    const wjs::PhaseIndex* phase_;
    wjs::Store& store_;
    std::optional<std::uint32_t> seed_;
    std::function<wjs::Day()> today_;
};

}  // namespace wjs::app
