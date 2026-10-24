// SPDX-License-Identifier: GPL-3.0-or-later
// A fixture for the privacy check's own test (libs/scan/tests/check_privacy_selftest.cmake): a
// platform camera file that sends. It does one thing the check forbids, network use, and
// nothing else it forbids.

void send_frame() {
    QNetworkAccessManager manager;
}
