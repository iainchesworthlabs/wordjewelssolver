// SPDX-License-Identifier: GPL-3.0-or-later
// Writes the synthetic scanner corpus to a directory, so that a person can look at
// it: one PNG a frame, and beside each a text file with the true board.
//
//   wjs_scan_render --out <directory> [--split test|tuning] [--condition <name>|all]
//                   [--negatives] [--sheet <file.png>]
//
// The conditions are upright, angled, loose and glare. --sheet writes one contact
// sheet of the four conditions, a 4x4 board of each at a third of the size, and
// nothing else. The frames are drawn from numbers and a seed (libs/scan/tests/synth), so
// the same command writes the same files anywhere. It is a test tool: the
// application never writes a frame (docs/design/scanner.md, "Privacy").

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "png.hpp"
#include "scene.hpp"

namespace {

using wjs::scan::GrayImage;
using namespace wjs::scan::synth;

GrayImage thumbnail(const GrayImage& image, int factor) {
    GrayImage out(image.width / factor, image.height / factor);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int sum = 0;
            for (int dy = 0; dy < factor; ++dy) {
                for (int dx = 0; dx < factor; ++dx) {
                    sum += image.at(x * factor + dx, y * factor + dy);
                }
            }
            out.at(x, y) = static_cast<std::uint8_t>(sum / (factor * factor));
        }
    }
    return out;
}

std::string board_text(const Rendered& frame) {
    std::string text;
    for (std::size_t i = 0; i < frame.letters.size(); ++i) {
        if (i > 0) {
            text += (i % frame.size == 0) ? "/" : " ";
        }
        text += frame.letters[i];
    }
    return text;
}

int usage() {
    std::cerr
        << "usage: wjs_scan_render --out <directory> [--split test|tuning]\n"
           "                       [--condition upright|angled|loose|glare|all] [--negatives]\n"
           "       wjs_scan_render --sheet <file.png>\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path out;
    std::filesystem::path sheet;
    std::string condition = "all";
    Split split = Split::test;
    bool negatives = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--out" && has_value) {
            out = argv[++i];
        } else if (arg == "--sheet" && has_value) {
            sheet = argv[++i];
        } else if (arg == "--condition" && has_value) {
            condition = argv[++i];
        } else if (arg == "--split" && has_value) {
            split = std::string_view(argv[++i]) == "tuning" ? Split::tuning : Split::test;
        } else if (arg == "--negatives") {
            negatives = true;
        } else {
            return usage();
        }
    }

    if (!sheet.empty()) {
        // The four conditions, a 4x4 of each, in a 2 x 2 sheet of thirds.
        constexpr int kFactor = 3;
        GrayImage page(2 * (kFrameWidth / kFactor), 2 * (kFrameHeight / kFactor));
        for (std::size_t c = 0; c < kConditionCount; ++c) {
            Spec spec;
            spec.condition = static_cast<Condition>(c);
            spec.split = Split::test;
            spec.size = 4;
            spec.index = 3;
            const GrayImage small = thumbnail(render(spec).image, kFactor);
            const int ox = static_cast<int>(c % 2) * small.width;
            const int oy = static_cast<int>(c / 2) * small.height;
            for (int y = 0; y < small.height; ++y) {
                for (int x = 0; x < small.width; ++x) {
                    page.at(ox + x, oy + y) = small.at(x, y);
                }
            }
        }
        return wjs::scan::synth::write_png(sheet, page) ? 0 : 1;
    }

    if (out.empty()) {
        return usage();
    }
    std::filesystem::create_directories(out);
    std::ofstream manifest(out / "corpus.txt");
    manifest << "# name seed\n";
    for (const Spec& spec : corpus(split)) {
        if (condition != "all" && condition != name(spec.condition)) {
            continue;
        }
        const Rendered frame = render(spec);
        const std::string stem = std::string(name(spec.condition)) + "_" +
                                 std::to_string(spec.size) + "x" + std::to_string(spec.size) + "_" +
                                 std::to_string(spec.index);
        if (!write_png(out / (stem + ".png"), frame.image)) {
            std::cerr << "could not write " << (out / (stem + ".png")) << "\n";
            return 1;
        }
        std::ofstream(out / (stem + ".txt")) << board_text(frame) << "\n";
        manifest << stem << " " << frame.seed << "\n";
    }
    if (negatives) {
        for (std::size_t k = 0; k < kNegativeKindCount; ++k) {
            for (unsigned i = 0; i < 4; ++i) {
                const std::string stem = "negative_" + std::to_string(k) + "_" + std::to_string(i);
                if (!write_png(out / (stem + ".png"),
                               render_negative(static_cast<NegativeKind>(k), i))) {
                    return 1;
                }
            }
        }
    }
    return 0;
}
