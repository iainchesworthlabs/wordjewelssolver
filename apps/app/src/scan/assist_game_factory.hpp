// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>

#include "assist_game.hpp"
#include "wjs/board.hpp"

namespace wjs::app {

// How a Mode A round is made, for the program to say. The model that the screens
// bind to is made by the QML engine and takes no arguments, so it asks
// make_assist_game(); what that answers is set here, once, before the engine
// loads. The board is the one a grown-up confirmed on the check screen.
using AssistGameMaker = std::function<std::unique_ptr<AssistGame>(wjs::Board)>;

// Installs `maker` for as long as the object lives and takes it away again,
// which is how the program lets the engine, and so the round, go before the
// dictionary and the store the round points to. At most one at a time.
class ScopedAssistGameMaker {
public:
    explicit ScopedAssistGameMaker(AssistGameMaker maker);
    ~ScopedAssistGameMaker();

    ScopedAssistGameMaker(const ScopedAssistGameMaker&) = delete;
    ScopedAssistGameMaker& operator=(const ScopedAssistGameMaker&) = delete;
};

// The round the application plays: the installed maker's, over the program's one
// dictionary and store (main.cpp installs it before the engine loads). With none
// installed there is no round, and begin() answers false: unlike Solo Play there
// is no fake board, because a board is a real one a person confirmed.
[[nodiscard]] std::unique_ptr<AssistGame> make_assist_game(wjs::Board board);

}  // namespace wjs::app
