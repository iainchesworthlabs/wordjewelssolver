// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// Haptics: a pattern of taps of the hand, for the moments of the design's
// feedback table (docs/design/application.md, "Audio and touch feedback").
// There is exactly one definition of vibrate() per platform, chosen in CMake by
// the platform's platform.cmake; the code has no preprocessor branching.
//
// Nothing the design asks for depends on it: every haptic is paired with a
// sound and a picture (application.md, "Accessibility": what is announced by
// sound is also seen). So a platform with no haptic engine answers false and
// the game carries on, as it does today on all three desktops.
//
// The targets that will have one:
//   - Android: Vibrator / VibratorManager, driven from Java through JNI in
//     libs/platforms/android (the backend waits for that target to exist);
//   - iOS/iPadOS: UIImpactFeedbackGenerator and UINotificationFeedbackGenerator
//     in libs/platforms/ios (the backend waits for that target to exist);
//   - the desktops: detail/haptics_none.cpp, shared by all three, answers
//     false. A desktop can buzz through a game controller or a phone, but
//     there is no such device assumed here.
//
// The pattern is deliberately small: a haptic is punctuation, never a message,
// and the design limits strong effects for a young child. A call never blocks
// and may be made at any time, including after the device has gone to sleep.

namespace wjs::platform {

enum class HapticPattern : std::uint8_t {
    light,       // a tile joining a path, the box rattling
    medium,      // a word found, a new best
    double_tap,  // the board cleared
};

// Asks the device for `pattern`. Returns true when the platform has a haptic
// engine it asked, false where there is none (the desktops today) or where it
// refused. Failure is harmless: the caller has already played the sound and
// shown the picture that go with the moment.
[[nodiscard]] bool vibrate(HapticPattern pattern);

}  // namespace wjs::platform
