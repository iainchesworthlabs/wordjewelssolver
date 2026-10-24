// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A PNG writer for the corpus tool, so that a person can look at what the
// generator draws. Uncompressed (stored deflate blocks), so it needs no library.
// A test tool: the application never writes a frame, and the build check
// scan.no_frame_files holds libs/scan to it.

#include <filesystem>

#include "wjs/scan/image.hpp"

namespace wjs::scan::synth {

// Writes the image as an 8-bit grey PNG. False if the file could not be written.
[[nodiscard]] bool write_png(const std::filesystem::path& path, const GrayImage& image);

// Writes the image as a binary grey PGM (P5, maxval 255). False if the file could not
// be written. For the recorded-frames harness's own test, which needs a file to read.
[[nodiscard]] bool write_pgm(const std::filesystem::path& path, const GrayImage& image);

// Reads a binary grey PGM (P5, maxval 255), the format `[.recorded]` frames are in. The
// header is the Netpbm one: white space between its fields, and `#` comments to the end
// of a line anywhere in it (a camera tool or an editor puts one there). An empty image if
// the file is not one, is cut short, has a maxval other than 255, or is larger than a
// frame the reader accepts.
[[nodiscard]] GrayImage read_pgm(const std::filesystem::path& path);

}  // namespace wjs::scan::synth
