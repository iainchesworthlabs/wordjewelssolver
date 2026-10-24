// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>

#include "solo_game.hpp"

namespace wjs::app {

// How a game is made, for the program to say. The model that the screens bind to
// is made by the QML engine and takes no arguments, so it asks make_solo_game();
// what that answers is set here, once, before the engine loads.
using SoloGameMaker = std::function<std::unique_ptr<SoloGame>()>;

// Installs `maker` for as long as the object lives and takes it away again,
// which is how the program lets the engine, and so the game, go before the phase
// and the store the game points to. At most one at a time.
class ScopedSoloGameMaker {
public:
    explicit ScopedSoloGameMaker(SoloGameMaker maker);
    ~ScopedSoloGameMaker();

    ScopedSoloGameMaker(const ScopedSoloGameMaker&) = delete;
    ScopedSoloGameMaker& operator=(const ScopedSoloGameMaker&) = delete;
};

// The game the application plays: the installed maker's, which is the library's
// session (RealSoloGame, set up by SoloGameSetup); the program installs its
// maker before the engine loads (main.cpp). With none installed, a game that
// cannot be shaken (the box and nothing in it); the tests' stand-in is
// FakeSoloGame, handed to a SoloModel by the test. This is the one place that
// decides. See solo_game.hpp.
[[nodiscard]] std::unique_ptr<SoloGame> make_solo_game();

}  // namespace wjs::app
