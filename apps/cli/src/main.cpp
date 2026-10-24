// SPDX-License-Identifier: GPL-3.0-or-later
#include <charconv>
#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "wjs/board.hpp"
#include "wjs/dictionary.hpp"
#include "wjs/platform/console.hpp"
#include "wjs/platform/paths.hpp"
#include "wjs/profiling.hpp"
#include "wjs/scoring.hpp"
#include "wjs/solver.hpp"
#include "wjs/version.hpp"

namespace {

// The exit codes are the program's contract (tests/system/check_cli.cmake): 0 for
// a board solved, and one code a reason for any other end, so that a script can
// tell them apart. Usage is 1; a failure the program did not foresee is not
// usage, and has a code of its own.
constexpr int exit_usage = 1;
constexpr int exit_dictionary = 2;
constexpr int exit_board = 3;
constexpr int exit_internal = 4;  // an exception the program did not foresee
constexpr int exit_output = 5;    // the result could not be written to standard output

constexpr std::string_view usage_text =
    "Usage: wjs-cli [options] <row>...\n"
    "\n"
    "Finds every word on a Boggle board. Each <row> is one row of the board. A row\n"
    "with no spaces is read one letter per tile (TAPE); otherwise tiles are\n"
    "separated by spaces or commas (\"Qu I T\"), so quote such a row.\n"
    "\n"
    "Options:\n"
    "  -d, --dictionary <file>  Word list, one word per line. Defaults to\n"
    "                           dictionaries/en.txt beside the executable.\n"
    "  -m, --min-length <n>     Shortest word to report (default 3).\n"
    "  -s, --scoring <scheme>   boggle (default) or rarity.\n"
    "  -r, --repeat <n>         Solve the board n times and print one result\n"
    "                           (default 1). For timing and for profiling: a\n"
    "                           profiler needs about a second to attach.\n"
    "  -h, --help               Show this help.\n"
    "      --version            Show the version.\n";

struct Arguments {
    std::optional<std::filesystem::path> dictionary;
    wjs::SolveOptions options;
    std::size_t repeat = 1;
    std::string board_text;
    bool help = false;
    bool version = false;
};

[[nodiscard]] std::expected<Arguments, std::string> parse_arguments(
    const std::vector<std::string>& args) {
    Arguments parsed;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        // The value after an option that takes one.
        const auto value = [&]() -> std::expected<std::string_view, std::string> {
            if (i + 1 >= args.size()) {
                return std::unexpected(std::format("{} needs a value", arg));
            }
            return std::string_view(args[++i]);
        };

        if (arg == "-h" || arg == "--help") {
            parsed.help = true;
        } else if (arg == "--version") {
            parsed.version = true;
        } else if (arg == "-d" || arg == "--dictionary") {
            const auto path = value();
            if (!path) {
                return std::unexpected(path.error());
            }
            parsed.dictionary = std::filesystem::path{std::string(*path)};
        } else if (arg == "-m" || arg == "--min-length") {
            const auto text = value();
            if (!text) {
                return std::unexpected(text.error());
            }
            const char* const first = text->data();
            const char* const last = first + text->size();
            const auto [end, error] = std::from_chars(first, last, parsed.options.min_length);
            if (error != std::errc{} || end != last) {
                return std::unexpected(std::format("'{}' is not a length", *text));
            }
        } else if (arg == "-r" || arg == "--repeat") {
            const auto text = value();
            if (!text) {
                return std::unexpected(text.error());
            }
            const char* const first = text->data();
            const char* const last = first + text->size();
            const auto [end, error] = std::from_chars(first, last, parsed.repeat);
            if (error != std::errc{} || end != last || parsed.repeat == 0) {
                return std::unexpected(std::format("'{}' is not a count of one or more", *text));
            }
        } else if (arg == "-s" || arg == "--scoring") {
            const auto scheme = value();
            if (!scheme) {
                return std::unexpected(scheme.error());
            }
            if (*scheme == "boggle") {
                parsed.options.scoring = wjs::ScoringScheme::standard_boggle;
            } else if (*scheme == "rarity") {
                parsed.options.scoring = wjs::ScoringScheme::letter_rarity;
            } else {
                return std::unexpected(
                    std::format("'{}' is not a scoring scheme (boggle or rarity)", *scheme));
            }
        } else if (arg.size() > 1 && arg.front() == '-') {
            return std::unexpected(std::format("unknown option '{}'", arg));
        } else {
            parsed.board_text += arg;
            parsed.board_text += '\n';
        }
    }
    return parsed;
}

[[nodiscard]] std::string describe_path(const std::vector<wjs::Position>& path) {
    std::string text;
    for (const auto& position : path) {
        if (!text.empty()) {
            text += ' ';
        }
        text += std::format("r{}c{}", position.row + 1, position.column + 1);
    }
    return text;
}

// Brings what has been written to standard output to its destination and says
// whether it got there. The stream is flushed here, by the program, and not left
// to the flush at exit: in the Debug build (AddressSanitizer and the profiler's
// client) the exit-time flush was lost about one run in ten, so that --version
// and --help left 0 with nothing printed. A closed pipe or a full disk fails the
// stream without a word; a program whose output is the point says so and does not
// leave 0 for a result that never arrived.
[[nodiscard]] int finish_output() {
    std::cout.flush();
    if (!std::cout) {
        std::cerr << "wjs-cli: the result could not be written to standard output\n";
        return exit_output;
    }
    return 0;
}

[[nodiscard]] int run(const Arguments& arguments) {
    auto zone = wjs::profiling::zone<"cli.run">();

    const auto board = wjs::Board::parse(arguments.board_text);
    if (!board) {
        std::cerr << std::format("wjs-cli: {}\n", wjs::to_string(board.error()));
        return exit_board;
    }

    const auto dictionary_path = arguments.dictionary.value_or(
        wjs::platform::executable_path().parent_path() / "dictionaries" / "en.txt");
    const auto dictionary = wjs::Dictionary::load_file(dictionary_path);
    if (!dictionary) {
        std::cerr << std::format("wjs-cli: {}: {}\n", dictionary_path.string(),
                                 wjs::to_string(dictionary.error()));
        return exit_dictionary;
    }

    // One result is printed whatever the count: the repeats are for a clock
    // or a profiler (docs/profiling.md), not for the person reading the list.
    auto words = wjs::solve(*board, *dictionary, arguments.options);
    for (std::size_t again = 1; again < arguments.repeat; ++again) {
        words = wjs::solve(*board, *dictionary, arguments.options);
    }

    int total = 0;
    for (const auto& found : words) {
        total += found.score;
        std::cout << std::format("{:<16}{:>5}   {}\n", found.word, found.score,
                                 describe_path(found.path));
    }
    std::cout << std::format("{} words, {} points\n", words.size(), total);
    return finish_output();
}

int run_cli(int argc, char** argv) {
    wjs::platform::use_utf8_console();
    wjs::profiling::thread_name("main");

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    const auto arguments = parse_arguments(args);
    if (!arguments) {
        std::cerr << std::format("wjs-cli: {}\n\n{}", arguments.error(), usage_text);
        return exit_usage;
    }
    if (arguments->version) {
        std::cout << std::format("wjs-cli {} ({})\n", wjs::project_version, wjs::target_platform);
        return finish_output();
    }
    if (arguments->help) {
        std::cout << usage_text;
        return finish_output();
    }
    if (arguments->board_text.empty()) {
        std::cerr << usage_text;
        return exit_usage;
    }
    return run(*arguments);
}

}  // namespace

// The catch-all below is the handler; the check does not model one in main.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    // std::format and the standard library can throw (allocation, a bad format
    // string): a command-line program reports it and exits, rather than
    // terminating through std::terminate.
    try {
        return run_cli(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "wjs-cli: " << error.what() << '\n';
        return exit_internal;
    } catch (...) {
        std::cerr << "wjs-cli: unknown error\n";
        return exit_internal;
    }
}
