// SPDX-License-Identifier: GPL-3.0-or-later
#include "wjs/app/sound_backend.hpp"

// Nothing to do: this variant is compiled when Qt Multimedia is not in the
// kit. The engine still counts the cue as a request, so the wiring is the same
// and a build without sound is still a working game.

namespace wjs::app {

SoundBackend::SoundBackend() = default;

SoundBackend::~SoundBackend() = default;

bool SoundBackend::available() const {
    return false;
}

void SoundBackend::play(std::string_view, double) {}

}  // namespace wjs::app
