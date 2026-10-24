// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "wjs/phase_data.hpp"
#include "wjs/store.hpp"

// TEST DATA for trying the parent area's screens. Every word, grapheme, sound
// and clip here was written for the tests and is not content for a child: the
// sounds are placeholders (`/t1/`), the words are ones the product brief itself
// names. It is not, and does not stand in for, a phase of the application; the
// real phase files have to be written and reviewed by a person qualified to do
// it (docs/design/phonics.md). The play in the store is made up.

namespace wjs::app::demo {

// Two phases of test data: A of single letters and B, which adds "ai" and a
// grapheme with two sounds.
[[nodiscard]] wjs::PhaseSet phases();

// A store with a few weeks of made-up play, as of `today`, in memory. The play
// is on phonics boards of phase B and on standard boards.
[[nodiscard]] wjs::Store busy_store(wjs::Day today);

// A store with nothing in it.
[[nodiscard]] wjs::Store empty_store();

}  // namespace wjs::app::demo
