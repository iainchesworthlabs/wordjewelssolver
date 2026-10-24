# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The requirements check held to cases on throw-away trees: a page that cites a
# test that exists, and, one fault at a time, one that does not, a missing file,
# a status that is not one, an ID twice, an implemented-in path that is gone, a
# page without its "Verified at" line, and a stated count of untested rows that
# is not the pages'.
#
#   cmake -DREPO_ROOT=<repository root> -DWORK_DIR=<scratch directory> \
#         -P tests/requirements/test_check_requirements.cmake
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT OR NOT WORK_DIR)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root> and -DWORK_DIR=<scratch directory>.")
endif()

set(_checker "${REPO_ROOT}/tests/requirements/check_requirements.cmake")
set(_failures "")
set(_cases 0)

set(_verified "Verified at 2026-10-22, commit b1aeb44.")
set(_header "| ID | Requirement | Status | Implemented in | Demonstrated by | Notes |\n|---|---|---|---|---|---|\n")

# One tree: a source file, a test file, an index stating `untested`, and one
# requirements page made of `rows` (each a full table line) with `verified` as its
# verification line. `expect` is a text the report must contain, or CLEAN.
function(wjs_case label expect untested verified rows)
    set(_tree "${WORK_DIR}/case")
    file(REMOVE_RECURSE "${_tree}")
    file(MAKE_DIRECTORY "${_tree}/docs/requirements" "${_tree}/src" "${_tree}/tests")
    file(WRITE "${_tree}/src/thing.cpp" "int thing();\n")
    file(WRITE "${_tree}/tests/thing_test.cpp"
        "TEST_CASE(\"the thing; with a semicolon and [brackets]\") {}\nTEST_CASE(\"another case\") {}\n")
    file(WRITE "${_tree}/docs/requirements.md" "Rows with no test: **${untested}**\n")
    file(WRITE "${_tree}/docs/requirements/area.md" "# Area\n\n${verified}\n\n${_header}${rows}\n")

    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DREPO_ROOT=${_tree}" -P "${_checker}"
        RESULT_VARIABLE _status
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error)
    set(_message "${_output}${_error}")

    math(EXPR _next "${_cases} + 1")
    set(_cases ${_next} PARENT_SCOPE)
    set(_problem "")
    if(expect STREQUAL "CLEAN")
        if(NOT _status EQUAL 0)
            set(_problem "should pass and failed: ${_message}")
        endif()
    else()
        if(_status EQUAL 0)
            set(_problem "should fail and passed")
        elseif(NOT _message MATCHES "${expect}")
            set(_problem "failed but did not say '${expect}': ${_message}")
        endif()
    endif()
    if(_problem)
        set(_failures "${_failures}\n  ${label}: ${_problem}" PARENT_SCOPE)
    endif()
endfunction()

set(_good_row "| R-THG-01 | It works | Implemented | `src/thing.cpp` | `tests/thing_test.cpp::the thing; with a semicolon and [brackets]` | n |")

wjs_case("a row whose test exists" CLEAN 0 "${_verified}" "${_good_row}")
wjs_case("two references in one cell" CLEAN 0 "${_verified}"
    "| R-THG-01, R-THG-02 | It works | Partial | `src/thing.cpp` | `tests/thing_test.cpp::another case`<br>`tests/thing_test.cpp::the thing; with a semicolon and [brackets]` | n |")
wjs_case("a row with no test, counted" CLEAN 1 "${_verified}"
    "| R-THG-01 | It works | Implemented | `src/thing.cpp` | — | n |")
wjs_case("a deferred row needs no test" CLEAN 0 "${_verified}"
    "| R-THG-01 | Later | Deferred | — | — | n |")
wjs_case("a test that is gone" "not in tests/thing_test.cpp" 0 "${_verified}"
    "| R-THG-01 | It works | Implemented | `src/thing.cpp` | `tests/thing_test.cpp::the thing was renamed` | n |")
wjs_case("a test file that is gone" "does not exist" 0 "${_verified}"
    "| R-THG-01 | It works | Implemented | `src/thing.cpp` | `tests/other_test.cpp::another case` | n |")
wjs_case("a status that is not one" "status 'Done'" 0 "${_verified}"
    "| R-THG-01 | It works | Done | `src/thing.cpp` | `tests/thing_test.cpp::another case` | n |")
wjs_case("an ID twice" "appears twice" 0 "${_verified}"
    "${_good_row}\n| R-THG-01 | Again | Implemented | `src/thing.cpp` | `tests/thing_test.cpp::another case` | n |")
wjs_case("an ID that is not one" "is not an ID" 0 "${_verified}"
    "| R-1 | It works | Implemented | `src/thing.cpp` | `tests/thing_test.cpp::another case` | n |")
wjs_case("an implemented-in path that is gone" "which does not exist" 0 "${_verified}"
    "| R-THG-01 | It works | Implemented | `src/gone.cpp` | `tests/thing_test.cpp::another case` | n |")
wjs_case("a page without its verification" "Verified at" 0 "Nothing here." "${_good_row}")
wjs_case("an untested count that grew" "have no test" 0 "${_verified}"
    "| R-THG-01 | It works | Implemented | `src/thing.cpp` | — | n |")
wjs_case("an untested count that was not lowered" "have no test" 3 "${_verified}"
    "| R-THG-01 | It works | Implemented | `src/thing.cpp` | `tests/thing_test.cpp::another case` | n |")
wjs_case("a row that is not six cells" "not six cells" 0 "${_verified}"
    "| R-THG-01 | It works | Implemented |")

if(_failures)
    message(FATAL_ERROR "The requirements check did not behave in ${_cases} cases:${_failures}")
endif()
message(STATUS "requirements check: ${_cases} cases behaved")
