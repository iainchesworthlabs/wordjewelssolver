// SPDX-License-Identifier: GPL-3.0-or-later
// A fixture for the privacy check's own test (libs/scan/tests/check_privacy_selftest.cmake): a
// camera source that also listens. It does one thing the check forbids, audio capture, and
// nothing else it forbids, so that the test can say which rule caught it.
#include <QAudioSource>

void listen() {
    QAudioSource source;
}
