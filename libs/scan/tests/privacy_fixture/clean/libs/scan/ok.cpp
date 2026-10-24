// SPDX-License-Identifier: GPL-3.0-or-later
// A fixture for the privacy check's own test (libs/scan/tests/check_privacy_selftest.cmake): a
// scanner source the check has to pass, and to count. Comments may name what the code must
// not do: std::ofstream, QAudioSource, QNetworkAccessManager, QImageCapture.
int brightness(int red, int green, int blue) {
    return (77 * red + 150 * green + 29 * blue + 128) / 256;
}
