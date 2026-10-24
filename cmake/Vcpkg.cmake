# SPDX-License-Identifier: GPL-3.0-or-later
# Included from the top-level CMakeLists.txt BEFORE project(): the vcpkg
# toolchain file is loaded at project() time, so the checkout it comes from is
# verified first. Sets WJS_VCPKG_ROOT and WJS_VCPKG_PIN, and finds Git.

# ---------------------------------------------------------------------------
# Shared vcpkg checkout, found through the VCPKG_ROOT environment variable.
#
# One vcpkg clone serves every project on the machine. The CMakePresets point
# CMAKE_TOOLCHAIN_FILE at $env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake.
# cmake/vcpkg-commit.txt is the vcpkg commit this project was verified with:
# the checkout must contain it (be at it, or ahead of it), so a stale clone
# that predates the baseline fails here rather than deep inside a port build.
# ---------------------------------------------------------------------------
if(NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR
        "The vcpkg toolchain file was not found.\n"
        "Set VCPKG_ROOT to a clone of https://github.com/iainchesworthlabs/vcpkg "
        "(or pass -DCMAKE_TOOLCHAIN_FILE=<root>/scripts/buildsystems/vcpkg.cmake) "
        "and reconfigure. See docs/building.md.")
endif()

get_filename_component(WJS_VCPKG_ROOT "${CMAKE_TOOLCHAIN_FILE}/../../.." ABSOLUTE)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vcpkg-commit.txt" WJS_VCPKG_PIN
    LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vcpkg-commit.txt")

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${WJS_VCPKG_ROOT}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${WJS_VCPKG_ROOT}"
                merge-base --is-ancestor "${WJS_VCPKG_PIN}" HEAD
        RESULT_VARIABLE WJS_VCPKG_PIN_RESULT
        ERROR_QUIET)
    if(WJS_VCPKG_PIN_RESULT EQUAL 1)
        message(FATAL_ERROR
            "The vcpkg checkout at ${WJS_VCPKG_ROOT} does not contain the pinned "
            "commit ${WJS_VCPKG_PIN} (cmake/vcpkg-commit.txt).\n"
            "Update it:\n"
            "  git -C \"${WJS_VCPKG_ROOT}\" pull")
    elseif(NOT WJS_VCPKG_PIN_RESULT EQUAL 0)
        message(FATAL_ERROR
            "The vcpkg checkout at ${WJS_VCPKG_ROOT} does not have the pinned "
            "commit ${WJS_VCPKG_PIN} (cmake/vcpkg-commit.txt) in its history; "
            "it may be a shallow clone or from a different remote.\n"
            "Fetch it:\n"
            "  git -C \"${WJS_VCPKG_ROOT}\" fetch origin")
    endif()
else()
    message(WARNING
        "${WJS_VCPKG_ROOT} is not a git checkout, so the pinned vcpkg commit "
        "${WJS_VCPKG_PIN} cannot be verified.")
endif()
message(STATUS "vcpkg: ${WJS_VCPKG_ROOT} (pin ${WJS_VCPKG_PIN})")
