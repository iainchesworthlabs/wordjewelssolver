# SPDX-License-Identifier: GPL-3.0-or-later
# check_phonics_review.cmake held to a table of cases on throw-away data, as
# test_check_phonics.ps1 does for the checker the gate was ported from: the
# release build's gate must refuse what it should, so the gate itself is tested.
#
#   cmake -DREPO_ROOT=<checkout> -DWORK_DIR=<scratch> -P test_check_phonics_review.cmake

foreach(_required IN ITEMS REPO_ROOT WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "test_check_phonics_review.cmake: pass -D${_required}=...")
    endif()
endforeach()

set(_gate "${REPO_ROOT}/tools/data/check_phonics_review.cmake")
set(_root "${WORK_DIR}/root")
set(_reviewed "# reviewed-by: Test Person   reviewed-on: 2026-10-20   source: written in the test")

# A tree with the given files (name;text pairs); none leaves no directory.
function(_tree)
    file(REMOVE_RECURSE "${_root}")
    file(MAKE_DIRECTORY "${_root}")
    set(_args ${ARGN})
    list(LENGTH _args _n)
    if(_n GREATER 0)
        file(MAKE_DIRECTORY "${_root}/data/phonics")
    endif()
    set(_i 0)
    while(_i LESS _n)
        math(EXPR _j "${_i} + 1")
        list(GET _args ${_i} _file)
        list(GET _args ${_j} _text)
        file(WRITE "${_root}/data/phonics/${_file}" "${_text}")
        math(EXPR _i "${_i} + 2")
    endwhile()
endfunction()

# Runs the gate on the tree; `expect` is PASS or FAIL, `text` a fragment of the
# output (empty for none).
function(_case name expect text)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DROOT=${_root}" -P "${_gate}"
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

set(_words "nip  n i p\n")

# --- what there is -----------------------------------------------------------
_tree()
_case("no phonics directory is nothing to review" PASS "nothing to review")

file(MAKE_DIRECTORY "${_root}/data/phonics")
_case("an empty phonics directory is nothing to review" PASS "nothing to review")

_tree("phase-a.words.txt" "# phase: A\n${_reviewed}\n${_words}"
      "phase-a.graphemes.txt" "# phase: A\n${_reviewed}\na  /t1/  clip:t1\n")
_case("reviewed phases pass" PASS "all reviewed")

# --- test data is refused ----------------------------------------------------
_tree("phase-a.words.txt" "# phase: A\n${_words}")
_case("test data is refused" FAIL "has no complete review header")
_case("test data is named" FAIL "phase-a.words.txt")

_tree("phase-a.words.txt" "# phase: A\n${_reviewed}\n${_words}"
      "phase-b.words.txt" "# phase: B\n${_words}")
_case("one test file among reviewed ones is named" FAIL "phase-b.words.txt")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: <name>   reviewed-on: <date>   source: <where>\n${_words}")
_case("placeholders left in the header are test data" FAIL "has no complete review header")

# --- the header's own rules ----------------------------------------------------
_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: Test Person\n${_words}")
_case("a reviewer without a date" FAIL "give both reviewed-by and reviewed-on")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-on: 2026-10-20\n${_words}")
_case("a date without a reviewer" FAIL "give both reviewed-by and reviewed-on")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: Test Person   reviewed-on: last week   source: a test\n${_words}")
_case("a date that is not a date" FAIL "YYYY-MM-DD")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: Test Person   reviewed-on: 2026-02-29   source: a test\n${_words}")
_case("a day the month does not have" FAIL "YYYY-MM-DD")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: Test Person   reviewed-on: 2026-13-01   source: a test\n${_words}")
_case("a month there is not" FAIL "YYYY-MM-DD")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: Test Person   reviewed-on: 2028-02-29   source: a test\n${_words}")
_case("a leap day is a date" PASS "all reviewed")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: Test Person   reviewed-on: 1900-02-29   source: a test\n${_words}")
_case("1900 was not a leap year" FAIL "YYYY-MM-DD")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: Test Person   reviewed-on: 2000-02-29   source: a test\n${_words}")
_case("2000 was" PASS "all reviewed")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: Test Person   reviewed-on: 2026-10-20\n${_words}")
_case("a reviewer and a date and no source" FAIL "says where its list came from")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: Test Person   reviewed-on: 2026-10-20   source: <where>\n${_words}")
_case("a source still in angle brackets is no source" FAIL "says where its list came from")

_tree("phase-a.words.txt" "${_reviewed}\n${_words}")
_case("no phase line" FAIL "no '# phase: <name>' header")

_tree("phase-a.words.txt" "# phase: B\n${_reviewed}\n${_words}")
_case("a header that names another phase than the file" FAIL "its header says phase B, its name says phase A")

_tree("extra.txt" "nip\n")
_case("a .txt file of another name" FAIL "not named phase-<x>")

_tree("phase-a.words.txt" "# phase: A\n# reviewed-by: One   reviewed-on: 2026-10-20   source: a test\n# reviewed-by: Two\n${_words}")
_case("a key given twice" FAIL "'reviewed-by:' is given twice")

# --- the file as an editor leaves it -----------------------------------------
string(ASCII 239 187 191 _bom)
string(ASCII 13 _cr)
_tree("phase-a.words.txt" "${_bom}# phase: A${_cr}\n${_reviewed}${_cr}\n${_words}")
_case("a byte-order mark and CRLF are tolerated" PASS "all reviewed")

message(STATUS "check_phonics_review.cmake refuses everything it should")
