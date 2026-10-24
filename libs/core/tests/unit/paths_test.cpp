// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

#include "wjs/platform/paths.hpp"

TEST_CASE("the executable path names the running test program", "[platform]") {
    const auto path = wjs::platform::executable_path();

    REQUIRE_FALSE(path.empty());
    CHECK(path.is_absolute());
    CHECK(std::filesystem::is_regular_file(path));
    CHECK(path.filename().string().find("wjs_unit_tests") != std::string::npos);
}

TEST_CASE("the user data directory is an absolute path", "[platform]") {
    const auto path = wjs::platform::user_data_directory();

    // A machine with no home directory would answer with nothing, and the
    // callers keep nothing on disk then; every machine that runs these tests
    // has one. The directory is not made by the question, and a fresh account
    // may not have it yet, so that is not asked.
    REQUIRE_FALSE(path.empty());
    CHECK(path.is_absolute());
}

TEST_CASE("the user data directory is the same every time it is asked", "[platform]") {
    CHECK(wjs::platform::user_data_directory() == wjs::platform::user_data_directory());
}
