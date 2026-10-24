# SPDX-License-Identifier: GPL-3.0-or-later
# The nightly analysis integration, held together: the presets the workflow
# names exist with the settings that make them analysis builds, the workflow is
# scheduled and stays out of merge_group (it produces no required check), and
# CodeQL is nightly rather than per-push (docs/ci.md, "Nightly analysis").
#
# Run as:
#   cmake -DREPO_ROOT=<checkout> -P tests/analysis/check_analysis.cmake

if(NOT DEFINED REPO_ROOT)
    message(FATAL_ERROR "check_analysis.cmake: pass -DREPO_ROOT=<checkout>")
endif()

set(_failures "")
function(_fail message_text)
    set(_failures "${_failures}\n  ${message_text}" PARENT_SCOPE)
endfunction()

# ---- the presets ------------------------------------------------------------

set(_presets_file "${REPO_ROOT}/CMakePresets.json")
if(NOT EXISTS "${_presets_file}")
    message(FATAL_ERROR "CMakePresets.json is missing: ${_presets_file}")
endif()
file(READ "${_presets_file}" _presets)

foreach(_needle IN ITEMS
        "\"name\": \"windows-msvc-analysis\""
        "\"WJS_ENABLE_MSVC_ANALYSIS\": \"ON\""
        "\"WJS_BUILD_APP\": \"OFF\""
        "\"WJS_BUILD_TESTS\": \"OFF\""
        "\"WJS_ENABLE_SANITIZERS\": \"OFF\""
        "\"CMAKE_CXX_COMPILER\": \"clang++\"")
    string(FIND "${_presets}" "${_needle}" _at)
    if(_at EQUAL -1)
        _fail("CMakePresets.json no longer contains ${_needle}; the analysis workflow would not configure the build it names")
    endif()
endforeach()

# The Clang leg is named by its host in the preset; this check deliberately does
# not spell that host out, so it stays a data check and not an OS test in CMake
# (tools/checks/check_platform_isolation.ps1, "cmake-os"). A preset whose name
# ends in -clang-debug with clang++ is the shape the workflow needs.
if(NOT _presets MATCHES "\"name\"[ \t]*:[ \t]*\"[^\"]*-clang-debug\"")
    _fail("CMakePresets.json has no *-clang-debug preset for the analysis workflow")
endif()

# ---- the nightly workflow ---------------------------------------------------

set(_analysis_file "${REPO_ROOT}/.github/workflows/analysis.yml")
if(NOT EXISTS "${_analysis_file}")
    _fail("the analysis workflow is missing: ${_analysis_file}")
else()
    file(READ "${_analysis_file}" _analysis)
    foreach(_needle IN ITEMS
            "schedule:"
            "cron:"
            "windows-msvc-analysis"
            "-clang-debug"
            "run-clang-tidy.py"
            "warnings-as-errors"
            "CMAKE_CXX_SCAN_FOR_MODULES=OFF")
        string(FIND "${_analysis}" "${_needle}" _at)
        if(_at EQUAL -1)
            _fail("analysis.yml no longer contains '${_needle}'")
        endif()
    endforeach()
    if(_analysis MATCHES "merge_group")
        _fail("analysis.yml carries merge_group; it produces no required check, so a queue entry would burn a run of it for nothing")
    endif()
    if(_analysis MATCHES "pull_request:" OR _analysis MATCHES "push:")
        _fail("analysis.yml runs on a push or a pull request; the analysis is nightly (docs/ci.md)")
    endif()
endif()

# ---- CodeQL -----------------------------------------------------------------

set(_codeql_file "${REPO_ROOT}/.github/workflows/codeql.yml")
if(NOT EXISTS "${_codeql_file}")
    _fail("the CodeQL workflow is missing: ${_codeql_file}")
else()
    file(READ "${_codeql_file}" _codeql)
    string(FIND "${_codeql}" "cron:" _cron_at)
    if(_cron_at EQUAL -1)
        _fail("codeql.yml is not scheduled")
    endif()
    string(FIND "${_codeql}" "workflow_dispatch:" _dispatch_at)
    if(_dispatch_at EQUAL -1)
        _fail("codeql.yml cannot be dispatched by hand")
    endif()
    if(_codeql MATCHES "pull_request:" OR _codeql MATCHES "push:")
        _fail("codeql.yml runs on a push or a pull request; it is nightly (docs/ci.md)")
    endif()
endif()

# ---- result -----------------------------------------------------------------

if(_failures)
    message(FATAL_ERROR "The nightly analysis integration has drifted:${_failures}")
endif()
message(STATUS "analysis: the nightly workflow, the analysis presets and the CodeQL schedule agree")
