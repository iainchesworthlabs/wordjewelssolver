// SPDX-License-Identifier: GPL-3.0-or-later
#include "solo_game.hpp"

namespace wjs::app {

std::string_view to_string(SoloState state) noexcept {
    switch (state) {
        case SoloState::box:
            return "box";
        case SoloState::shaking:
            return "shaking";
        case SoloState::dropping:
            return "dropping";
        case SoloState::playing:
            return "playing";
        case SoloState::cleared:
            return "cleared";
        case SoloState::resting:
            return "resting";
    }
    return "box";
}

}  // namespace wjs::app
