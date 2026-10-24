# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# A leg that says it holds the work budgets ran them.
#
#   cmake -DPERF_TESTS=<wjs_perf_tests> -DEXPECT_RUN=<1|0> -DWORK_DIR=<dir>
#         -P tests/profiling/check_perf_ran.cmake
#
# Every case of the performance tier's work budgets stands aside (SKIP) in a
# build that is not optimised, because the counts are the same in every build and
# a Debug run is much longer (libs/core/bench/check.hpp). A skipped case is not a
# failed one, so a leg whose every build is Debug passes with the whole tier
# skipped, and ctest says "100% passed". This is the check that notices: it runs
# the work cases, and counts those that did not skip.
#
#   EXPECT_RUN=1  the build is optimised (CMake says so, from the configuration), or
#                 the environment variable WJS_REQUIRE_PERF_BUDGETS is set to
#                 something other than "" or "0" (a workflow that declares "this leg
#                 holds the budgets", whatever its configuration). Fails when no case
#                 ran, and says why.
#   EXPECT_RUN=0  the build is not optimised and nobody asked. Exits 4, which the test
#                 registers as SKIP_RETURN_CODE: it is reported as skipped, with the
#                 reason, not as a pass.
#
# A case that is skipped in an optimised build is also a failure of this check:
# the only skip the tier has is "not optimised", so one that skips in an optimised
# build has found another reason to stand aside, and a budget that does not run is
# the thing being guarded.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

foreach(_required IN ITEMS PERF_TESTS EXPECT_RUN WORK_DIR)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "check_perf_ran.cmake: ${_required} is required")
    endif()
endforeach()

set(_forced FALSE)
if(DEFINED ENV{WJS_REQUIRE_PERF_BUDGETS} AND NOT "$ENV{WJS_REQUIRE_PERF_BUDGETS}" STREQUAL ""
        AND NOT "$ENV{WJS_REQUIRE_PERF_BUDGETS}" STREQUAL "0")
    set(_forced TRUE)
endif()
if(EXPECT_RUN OR _forced)
    set(_expect TRUE)
else()
    set(_expect FALSE)
endif()

if(NOT EXISTS "${PERF_TESTS}")
    message(FATAL_ERROR "check_perf_ran.cmake: '${PERF_TESTS}' was not built")
endif()

if(NOT _expect)
    message(STATUS
        "SKIPPED: this build is not optimised, so the performance tier's work budgets stand "
        "aside by design; its counts are held by the optimised legs. Set "
        "WJS_REQUIRE_PERF_BUDGETS=1 in a leg that is meant to hold them.")
    cmake_language(EXIT 4)
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(_report "${WORK_DIR}/perf-work.xml")
file(REMOVE "${_report}")
# The work cases only: the wall-time ones are opt-in and stand aside on purpose.
execute_process(
    COMMAND "${PERF_TESTS}" "[perf]~[timing]" --reporter "xml::out=${_report}"
    RESULT_VARIABLE _code
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    TIMEOUT 300)
if(NOT EXISTS "${_report}")
    message(FATAL_ERROR
        "the performance tests wrote no report (exit ${_code}):\n${_out}\n${_err}")
endif()
file(READ "${_report}" _xml)

string(REGEX MATCH
    "<OverallResultsCases successes=\"([0-9]+)\" failures=\"([0-9]+)\" expectedFailures=\"([0-9]+)\" skips=\"([0-9]+)\"/>"
    _cases "${_xml}")
if(NOT _cases)
    message(FATAL_ERROR "the performance tests' report has no case totals:\n${_xml}")
endif()
set(_passed "${CMAKE_MATCH_1}")
set(_failed "${CMAKE_MATCH_2}")
set(_expected_failed "${CMAKE_MATCH_3}")
set(_skipped "${CMAKE_MATCH_4}")
math(EXPR _ran "${_passed} + ${_failed} + ${_expected_failed}")

message(STATUS
    "performance work cases: ${_ran} ran (${_passed} passed, ${_failed} failed), ${_skipped} skipped")

if(_ran EQUAL 0)
    message(FATAL_ERROR
        "This leg is meant to hold the performance tier's work budgets and none of its "
        "${_skipped} cases ran: every one skipped. A budget that does not run holds nothing. "
        "Either the build is not optimised (use a Release or RelWithDebInfo build), or the "
        "leg should not claim to hold them (unset WJS_REQUIRE_PERF_BUDGETS).")
endif()
if(NOT _skipped EQUAL 0)
    message(FATAL_ERROR
        "${_skipped} of the performance tier's work cases skipped in a build that is meant "
        "to run them all. The only reason the tier stands aside is that a build is not "
        "optimised; find which case skips and why (wjs_perf_tests \"[perf]~[timing]\" -s).")
endif()
