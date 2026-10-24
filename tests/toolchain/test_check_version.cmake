# SPDX-License-Identifier: GPL-3.0-or-later
# check_version.cmake held to a table of cases on throw-away trees, so that a
# comparison that stopped comparing is a failed test and not a pass.
#
#   cmake -DREPO_ROOT=<checkout> -DWORK_DIR=<scratch> -P test_check_version.cmake

foreach(_required IN ITEMS REPO_ROOT WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "test_check_version.cmake: pass -D${_required}=...")
    endif()
endforeach()

set(_check "${REPO_ROOT}/tests/toolchain/check_version.cmake")
set(_tree "${WORK_DIR}/tree")

# A tree with the three places set to the given values; a changelog heading of
# "" leaves the file with only [Unreleased].
function(_make_tree project manifest changelog)
    file(REMOVE_RECURSE "${_tree}")
    file(MAKE_DIRECTORY "${_tree}")
    file(WRITE "${_tree}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.28)\nproject(Thing\n    VERSION ${project}\n    LANGUAGES CXX)\n")
    file(WRITE "${_tree}/vcpkg.json" "{ \"name\": \"thing\", \"version\": \"${manifest}\" }\n")
    set(_text "# Changelog\n\n## [Unreleased]\n\nWork.\n\n")
    if(NOT "${changelog}" STREQUAL "")
        string(APPEND _text "## [${changelog}] - 2026-01-01\n\nShipped.\n\n## [0.0.1] - 2025-01-01\n")
    endif()
    file(WRITE "${_tree}/CHANGELOG.md" "${_text}")
endfunction()

# Runs the check on the tree; `expect` is PASS or FAIL, `text` a fragment the
# output must contain (empty for none).
function(_case name expect text)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DREPO_ROOT=${_tree}" ${ARGN} -P "${_check}"
        RESULT_VARIABLE _code
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
    set(_all "${_out}${_err}")
    if(expect STREQUAL "PASS" AND NOT _code EQUAL 0)
        message(FATAL_ERROR "case '${name}' should pass:\n${_all}")
    endif()
    if(expect STREQUAL "FAIL" AND _code EQUAL 0)
        message(FATAL_ERROR "case '${name}' should fail but passed:\n${_all}")
    endif()
    if(NOT "${text}" STREQUAL "")
        string(FIND "${_all}" "${text}" _at)
        if(_at EQUAL -1)
            message(FATAL_ERROR "case '${name}': '${text}' is missing from:\n${_all}")
        endif()
    endif()
endfunction()

_make_tree(0.2.0 0.2.0 0.2.0)
_case("all three agree" PASS "agree")

_make_tree(0.1.0 0.1.0 "")
_case("no release heading yet" PASS "was not compared")
_case("no release heading, one required" FAIL "no '## [0.1.0]' release heading" -DREQUIRE_CHANGELOG_ENTRY=ON)

_make_tree(0.2.0 0.1.0 0.2.0)
_case("manifest behind" FAIL "vcpkg.json version is 0.1.0")

_make_tree(0.2.0 0.2.0 0.1.0)
_case("changelog behind" FAIL "newest CHANGELOG.md release is 0.1.0")

_make_tree(0.2.0 0.2.0 0.2.0)
_case("project behind the configured value" FAIL "reconfigure" -DVERSION=0.3.0)
_case("the tag is the version" PASS "agree" -DTAG=v0.2.0)
_case("the tag is another version" FAIL "is not v0.2.0" -DTAG=v0.2.1)
_case("print" PASS "0.2.0" -DPRINT=ON)

message(STATUS "check_version.cmake refuses every mismatch it should")
