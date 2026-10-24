# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# run_coverage.cmake
#
# Runs one coverage tier and turns its counters into a report, then holds it to
# the project's minimum standard. Invoked by the coverage-unit / coverage-ui
# targets (cmake/Coverage.cmake); not a ctest entry, because it runs ctest
# itself and needs a build with WJS_ENABLE_COVERAGE on.
#
# Inputs (all -D):
#   SOURCE_DIR        the checkout root
#   BUILD_DIR         the instrumented build tree
#   OUTPUT_DIR        where the report is written (build/coverage)
#   STYLE             gcov (GCC/Clang) or llvm (clang-cl)
#   TIER, LABEL       the report's name and the ctest label to run
#   SCOPE             the source directory to measure, relative to SOURCE_DIR
#   EXECUTABLE        the instrumented test program (llvm needs it)
#   MIN_LINE, MIN_BRANCH  the floor, as percentages (0 disables a floor)
#   CTEST_EXECUTABLE  ctest's path
#   CMAKE_EXECUTABLE  cmake's path (for `cmake -E env`)
#   CTEST_CONFIG      the configuration for a multi-config generator (empty
#                     for a single-config one)
#   GCOVR, GCOV, LLVM_PROFDATA, LLVM_COV
#                     the report tools; found in PATH when not given
#
# The run is serial: Catch2's discovered cases share one test program, and two
# copies writing the same .gcda file would lose counts. The profile directory
# is emptied first, so a report says what this tier did and not what another
# tier did earlier.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.28)

foreach(_required IN ITEMS SOURCE_DIR BUILD_DIR OUTPUT_DIR STYLE TIER LABEL SCOPE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "run_coverage.cmake: ${_required} is required")
    endif()
endforeach()

if(NOT DEFINED MIN_LINE)
    set(MIN_LINE 90)
endif()
if(NOT DEFINED MIN_BRANCH)
    set(MIN_BRANCH 80)
endif()
if(NOT DEFINED CTEST_EXECUTABLE)
    find_program(CTEST_EXECUTABLE NAMES ctest)
endif()
if(NOT DEFINED CMAKE_EXECUTABLE)
    find_program(CMAKE_EXECUTABLE NAMES cmake)
endif()

set(_report_dir "${OUTPUT_DIR}/${TIER}")
file(REMOVE_RECURSE "${_report_dir}")
file(MAKE_DIRECTORY "${_report_dir}")

message(STATUS "coverage: ${TIER} tier (${LABEL}), ${SCOPE}, ${STYLE} counters")

# ---------------------------------------------------------------------------
# 1. Empty this tier's counters and run its tests.
# ---------------------------------------------------------------------------
set(_ctest_arguments
    --test-dir "${BUILD_DIR}" -L "${LABEL}" --output-on-failure --no-tests=error)
if(DEFINED CTEST_CONFIG AND NOT "${CTEST_CONFIG}" STREQUAL "")
    list(APPEND _ctest_arguments -C "${CTEST_CONFIG}")
endif()

if(STYLE STREQUAL "llvm")
    set(_profile_dir "${_report_dir}/profiles")
    file(REMOVE_RECURSE "${_profile_dir}")
    file(MAKE_DIRECTORY "${_profile_dir}")
    set(_profile_pattern "${_profile_dir}/%p-%m.profraw")
    set(_ctest_command
        "${CMAKE_EXECUTABLE}" -E env "LLVM_PROFILE_FILE=${_profile_pattern}"
        "${CTEST_EXECUTABLE}" ${_ctest_arguments})
elseif(STYLE STREQUAL "gcov")
    file(GLOB_RECURSE _stale_counters "${BUILD_DIR}/*.gcda")
    if(_stale_counters)
        file(REMOVE ${_stale_counters})
    endif()
    set(_ctest_command "${CTEST_EXECUTABLE}" ${_ctest_arguments})
else()
    message(FATAL_ERROR "run_coverage.cmake: unknown STYLE '${STYLE}' (gcov or llvm)")
endif()

execute_process(COMMAND ${_ctest_command} RESULT_VARIABLE _test_result)
if(NOT _test_result EQUAL 0)
    message(FATAL_ERROR
        "coverage: the ${LABEL} tests failed (exit ${_test_result}); the report "
        "would describe a suite that is not green, so it is not written.")
endif()

# ---------------------------------------------------------------------------
# 2. The scope's sources. The report covers the C++ in this directory; a header
# under it is included when the compiler mapped it.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE _scope_sources
    "${SOURCE_DIR}/${SCOPE}/*.cpp"
    "${SOURCE_DIR}/${SCOPE}/*.cc"
    "${SOURCE_DIR}/${SCOPE}/*.hpp"
    "${SOURCE_DIR}/${SCOPE}/*.h")
# The tests and benchmarks beside the code are not part of what is measured.
set(_scope_production "")
foreach(_source IN LISTS _scope_sources)
    file(RELATIVE_PATH _relative "${SOURCE_DIR}" "${_source}")
    if(NOT _relative MATCHES "/(tests|ui-tests|bench)/")
        list(APPEND _scope_production "${_source}")
    endif()
endforeach()
set(_scope_sources ${_scope_production})
list(SORT _scope_sources)
if(NOT _scope_sources)
    message(FATAL_ERROR "coverage: no sources under ${SOURCE_DIR}/${SCOPE}")
endif()

# ---------------------------------------------------------------------------
# 3. Report. Both styles leave a summary.json with the same keys, so the gate
# below and the CI summary read one format:
#   line_percent, line_covered, line_total,
#   branch_percent, branch_covered, branch_total
# ---------------------------------------------------------------------------
set(_summary_json "${_report_dir}/summary.json")
set(_text_report "${_report_dir}/report.txt")

if(STYLE STREQUAL "llvm")
    if(NOT DEFINED EXECUTABLE OR NOT EXISTS "${EXECUTABLE}")
        message(FATAL_ERROR
            "coverage: the instrumented test program '${EXECUTABLE}' does not "
            "exist; build it before asking for the report.")
    endif()
    if(NOT DEFINED LLVM_PROFDATA OR "${LLVM_PROFDATA}" STREQUAL "")
        find_program(LLVM_PROFDATA NAMES llvm-profdata)
    endif()
    if(NOT DEFINED LLVM_COV OR "${LLVM_COV}" STREQUAL "")
        find_program(LLVM_COV NAMES llvm-cov)
    endif()
    if(NOT LLVM_PROFDATA OR NOT LLVM_COV)
        message(FATAL_ERROR
            "coverage: llvm-profdata and llvm-cov were not found. They ship with "
            "clang-cl; pass -DLLVM_PROFDATA=... and -DLLVM_COV=... if they are "
            "in a non-standard directory.")
    endif()

    file(GLOB _profiles "${_profile_dir}/*.profraw")
    if(NOT _profiles)
        message(FATAL_ERROR
            "coverage: the ${LABEL} run wrote no profile data to ${_profile_dir}. "
            "Check that the test programs link wjs::coverage and that "
            "LLVM_PROFILE_FILE reached them.")
    endif()
    list(LENGTH _profiles _profile_count)
    message(STATUS "coverage: merging ${_profile_count} profile file(s)")

    set(_profdata "${_report_dir}/merged.profdata")
    # One test process writes one profile in this build (Catch2's discovered
    # cases are separate processes), so the unit tier makes hundreds of them and
    # the full list does not fit a Windows command line. llvm-profdata reads the
    # names from a file instead.
    set(_profile_list "${_report_dir}/profiles.txt")
    file(WRITE "${_profile_list}" "")
    foreach(_profile IN LISTS _profiles)
        file(APPEND "${_profile_list}" "${_profile}\n")
    endforeach()
    execute_process(
        COMMAND "${LLVM_PROFDATA}" merge --sparse
            "--input-files=${_profile_list}" -o "${_profdata}"
        RESULT_VARIABLE _merge_result
        ERROR_VARIABLE _merge_error)
    if(NOT _merge_result EQUAL 0)
        message(FATAL_ERROR "coverage: llvm-profdata failed:\n${_merge_error}")
    endif()

    # `llvm-cov export --summary-only` with the source list writes JSON for
    # those files only; its totals are the gate's numbers. Summary-only keeps
    # the file to a few hundred kilobytes (the full export is tens of MB).
    execute_process(
        COMMAND "${LLVM_COV}" export "${EXECUTABLE}"
            "--instr-profile=${_profdata}"
            -format=text
            --summary-only
            ${_scope_sources}
        RESULT_VARIABLE _export_result
        OUTPUT_FILE "${_report_dir}/coverage.json"
        ERROR_VARIABLE _export_error)
    if(NOT _export_result EQUAL 0)
        message(FATAL_ERROR "coverage: llvm-cov export failed:\n${_export_error}")
    endif()
    file(READ "${_report_dir}/coverage.json" _export_json)

    foreach(_part IN ITEMS lines branches)
        string(JSON _count GET "${_export_json}" data 0 totals ${_part} count)
        string(JSON _covered GET "${_export_json}" data 0 totals ${_part} covered)
        string(JSON _percent GET "${_export_json}" data 0 totals ${_part} percent)
        if(_part STREQUAL "lines")
            set(_line_total "${_count}")
            set(_line_covered "${_covered}")
            set(_line_percent "${_percent}")
        else()
            set(_branch_total "${_count}")
            set(_branch_covered "${_covered}")
            set(_branch_percent "${_percent}")
        endif()
    endforeach()

    # The same numbers as a readable report, and the browsable HTML beside it.
    execute_process(
        COMMAND "${LLVM_COV}" report "${EXECUTABLE}"
            "--instr-profile=${_profdata}"
            --show-branch-summary
            ${_scope_sources}
        RESULT_VARIABLE _report_result
        OUTPUT_VARIABLE _report_text
        ERROR_VARIABLE _report_error)
    if(NOT _report_result EQUAL 0)
        message(FATAL_ERROR "coverage: llvm-cov report failed:\n${_report_error}")
    endif()
    file(WRITE "${_text_report}" "${_report_text}")

    file(REMOVE_RECURSE "${_report_dir}/html")
    execute_process(
        COMMAND "${LLVM_COV}" show "${EXECUTABLE}"
            "--instr-profile=${_profdata}"
            --format=html
            --show-branches=count
            --output-dir "${_report_dir}/html"
            ${_scope_sources}
        RESULT_VARIABLE _html_result
        ERROR_VARIABLE _html_error)
    if(NOT _html_result EQUAL 0)
        message(WARNING "coverage: the HTML report could not be made:\n${_html_error}")
    endif()
else()
    if(NOT DEFINED GCOVR OR "${GCOVR}" STREQUAL "")
        find_program(GCOVR NAMES gcovr)
    endif()
    if(NOT GCOVR)
        message(FATAL_ERROR
            "coverage: gcovr was not found. Install the pinned tool with\n"
            "  pip install -r .github/requirements/coverage.txt")
    endif()

    set(_gcov_args "")
    if(DEFINED GCOV AND NOT "${GCOV}" STREQUAL "")
        list(APPEND _gcov_args --gcov-executable "${GCOV}")
    endif()

    execute_process(
        COMMAND "${GCOVR}"
            --root "${SOURCE_DIR}"
            # gcovr matches --filter against the absolute source path, not the
            # path relative to --root (checked with 7.2 and the pinned 8.6): a
            # bare 'libs/core/.*' excludes everything. The leading .* is what
            # makes the checkout prefix match.
            --filter ".*${SCOPE}/.*"
            --exclude ".*/(tests|ui-tests|bench)/.*"
            --object-directory "${BUILD_DIR}"
            ${_gcov_args}
            --exclude-throw-branches
            --exclude-unreachable-branches
            --gcov-ignore-errors=no_working_dir_found
            --gcov-ignore-parse-errors negative_hits.warn_once_per_file
            --txt "${_text_report}"
            --html-details "${_report_dir}/index.html"
            --json-summary "${_summary_json}"
            --json-summary-pretty
        RESULT_VARIABLE _gcovr_result
        OUTPUT_VARIABLE _gcovr_output
        ERROR_VARIABLE _gcovr_error)
    file(WRITE "${_report_dir}/gcovr.log" "${_gcovr_output}\n${_gcovr_error}")
    if(NOT _gcovr_result EQUAL 0)
        message(FATAL_ERROR
            "coverage: gcovr failed (exit ${_gcovr_result}):\n${_gcovr_error}")
    endif()

    file(READ "${_summary_json}" _gcovr_summary)
    string(JSON _line_covered GET "${_gcovr_summary}" line_covered)
    string(JSON _line_total GET "${_gcovr_summary}" line_total)
    string(JSON _line_percent GET "${_gcovr_summary}" line_percent)
    string(JSON _branch_covered GET "${_gcovr_summary}" branch_covered)
    string(JSON _branch_total GET "${_gcovr_summary}" branch_total)
    string(JSON _branch_percent GET "${_gcovr_summary}" branch_percent)
endif()

# ---------------------------------------------------------------------------
# 4. One summary, written for both styles, then the gate.
# ---------------------------------------------------------------------------
function(wjs_one_decimal value out)
    string(REGEX MATCH "^([0-9]+)(\\.([0-9]*))?" _match "${value}")
    if(NOT _match)
        set(${out} "0.0" PARENT_SCOPE)
        return()
    endif()
    set(_fraction "${CMAKE_MATCH_3}0")
    string(SUBSTRING "${_fraction}" 0 1 _first)
    set(${out} "${CMAKE_MATCH_1}.${_first}" PARENT_SCOPE)
endfunction()
wjs_one_decimal("${_line_percent}" _line_display)
wjs_one_decimal("${_branch_percent}" _branch_display)

set(_summary
"{
  \"tier\": \"${TIER}\",
  \"scope\": \"${SCOPE}\",
  \"line_covered\": ${_line_covered},
  \"line_total\": ${_line_total},
  \"line_percent\": ${_line_display},
  \"branch_covered\": ${_branch_covered},
  \"branch_total\": ${_branch_total},
  \"branch_percent\": ${_branch_display}
}
")
file(WRITE "${_summary_json}" "${_summary}")

message(STATUS "")
message(STATUS "Coverage of ${SCOPE} from the ${TIER} tier")
message(STATUS "  lines     ${_line_covered}/${_line_total}  ${_line_display}%")
message(STATUS "  branches  ${_branch_covered}/${_branch_total}  ${_branch_display}%")
message(STATUS "  report    ${_report_dir}")

set(_markdown
"### Coverage: ${TIER} (${SCOPE})

| | covered | total | percent | floor |
|---|---:|---:|---:|---:|
| lines | ${_line_covered} | ${_line_total} | ${_line_display}% | ${MIN_LINE}% |
| branches | ${_branch_covered} | ${_branch_total} | ${_branch_display}% | ${MIN_BRANCH}% |
")
file(WRITE "${_report_dir}/coverage.md" "${_markdown}")

set(_failed FALSE)
if(MIN_LINE GREATER 0 AND _line_percent LESS MIN_LINE)
    message(SEND_ERROR
        "coverage: ${_line_display}% of lines is under the ${MIN_LINE}% minimum "
        "for ${SCOPE} (${TIER} tier). Add tests that execute the uncovered lines; "
        "the report lists them in ${_text_report}.")
    set(_failed TRUE)
endif()
if(MIN_BRANCH GREATER 0 AND _branch_percent LESS MIN_BRANCH)
    message(SEND_ERROR
        "coverage: ${_branch_display}% of branches is under the ${MIN_BRANCH}% "
        "minimum for ${SCOPE} (${TIER} tier). Add tests that take the untaken "
        "sides; the report lists them in ${_text_report}.")
    set(_failed TRUE)
endif()
if(_failed)
    message(FATAL_ERROR
        "coverage: the ${TIER} tier is below the project's minimum standard.")
endif()

message(STATUS "coverage: ${TIER} tier meets the ${MIN_LINE}% line / ${MIN_BRANCH}% branch minimum")
