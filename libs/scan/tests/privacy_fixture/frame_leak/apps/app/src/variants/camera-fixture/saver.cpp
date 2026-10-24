// SPDX-License-Identifier: GPL-3.0-or-later
// A fixture for the privacy check's own test (libs/scan/tests/check_privacy_selftest.cmake): a
// camera backend that has Qt write the frame, through Qt's own classes rather than a file
// class. It does one thing the check forbids, and nothing else it forbids.
void keep_a_frame(QImage image) {
    QImageCapture capture;
    capture.captureToFile("frame.jpg");
}
