// SPDX-License-Identifier: GPL-3.0-or-later
#include "assist_game_factory.hpp"

#include <utility>

namespace wjs::app {
namespace {

AssistGameMaker& installed() {
    static AssistGameMaker maker;
    return maker;
}

}  // namespace

ScopedAssistGameMaker::ScopedAssistGameMaker(AssistGameMaker maker) {
    installed() = std::move(maker);
}

ScopedAssistGameMaker::~ScopedAssistGameMaker() {
    installed() = nullptr;
}

std::unique_ptr<AssistGame> make_assist_game(wjs::Board board) {
    if (const AssistGameMaker& maker = installed()) {
        return maker(std::move(board));
    }
    return nullptr;
}

}  // namespace wjs::app
