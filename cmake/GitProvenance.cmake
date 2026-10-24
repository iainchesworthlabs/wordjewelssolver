# SPDX-License-Identifier: GPL-3.0-or-later
# Included from the top-level CMakeLists.txt after project(): reads the
# provenance the version block (libs/core/include/wjs/version.hpp.in) states, once
# at configure time. Sets WJS_GIT_COMMIT, WJS_GIT_DESCRIBE, WJS_GIT_BRANCH and
# WJS_GIT_DIRTY.

# ---------------------------------------------------------------------------
# Git provenance for the version block (libs/core/include/wjs/version.hpp.in):
# what the build was built from, read once at configure time. A tree git
# cannot be asked (a source tarball) keeps the "unknown" defaults.
# ---------------------------------------------------------------------------
find_package(Git QUIET)
set(WJS_GIT_COMMIT "unknown")
set(WJS_GIT_DESCRIBE "v${PROJECT_VERSION}")
set(WJS_GIT_BRANCH "unknown")
set(WJS_GIT_DIRTY false)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE WJS_GIT_COMMIT_RAW
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE WJS_GIT_COMMIT_RESULT)
    if(WJS_GIT_COMMIT_RESULT EQUAL 0)
        set(WJS_GIT_COMMIT "${WJS_GIT_COMMIT_RAW}")

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --always --match "v[0-9]*"
            OUTPUT_VARIABLE WJS_GIT_DESCRIBE_RAW
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE WJS_GIT_DESCRIBE_RESULT)
        if(WJS_GIT_DESCRIBE_RESULT EQUAL 0)
            set(WJS_GIT_DESCRIBE "${WJS_GIT_DESCRIBE_RAW}")
        else()
            set(WJS_GIT_DESCRIBE "${WJS_GIT_COMMIT}")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --abbrev-ref HEAD
            OUTPUT_VARIABLE WJS_GIT_BRANCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)

        # Ignore untracked files, so a populated build tree does not read "dirty".
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                    status --porcelain --untracked-files=no
            OUTPUT_VARIABLE WJS_GIT_STATUS
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT WJS_GIT_STATUS STREQUAL "")
            set(WJS_GIT_DIRTY true)
        endif()
    endif()
endif()
