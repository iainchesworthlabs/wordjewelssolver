# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The wording check held to cases on throw-away trees: a wording file that lacks
# one of the design's sentences, a word an assessment is made of, a sentence in
# a screen that is not the wording file, and the real file as it is.
#
#   cmake -DREPO_ROOT=<repository root> -DWORK_DIR=<scratch directory> \
#         -P tests/parent/test_check_wording.cmake
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT OR NOT WORK_DIR)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root> and -DWORK_DIR=<scratch directory>.")
endif()

set(_checker "${REPO_ROOT}/tests/parent/check_wording.cmake")
set(_real "${REPO_ROOT}/apps/app/qml/parent/ParentWording.qml")
file(READ "${_real}" _real_text)

set(_failures "")
set(_cases 0)

# A tree with a wording file of `wording` and one screen of `screen` (or none),
# scanned. `expect` is a text the report must contain, or CLEAN.
function(wjs_case label expect wording screen)
    set(_tree "${WORK_DIR}/case")
    file(REMOVE_RECURSE "${_tree}")
    file(MAKE_DIRECTORY "${_tree}/apps/app/qml/parent")
    file(WRITE "${_tree}/apps/app/qml/parent/ParentWording.qml" "${wording}")
    if(NOT screen STREQUAL "")
        file(WRITE "${_tree}/apps/app/qml/parent/Screen.qml" "${screen}\n")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DREPO_ROOT=${_tree}" -P "${_checker}"
        RESULT_VARIABLE _status
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error)
    set(_message "${_output}${_error}")

    math(EXPR _next "${_cases} + 1")
    set(_cases ${_next} PARENT_SCOPE)

    set(_failure "")
    if(expect STREQUAL "CLEAN")
        if(NOT _status EQUAL 0)
            set(_failure "${label}: expected the check to pass, it said:\n${_message}")
        endif()
    elseif(_status EQUAL 0)
        set(_failure "${label}: expected a finding about '${expect}', the check passed")
    else()
        string(FIND "${_message}" "${expect}" _found)
        if(_found EQUAL -1)
            set(_failure "${label}: expected '${expect}' in the report, got:\n${_message}")
        endif()
    endif()
    if(_failure)
        set(_failures "${_failures}\n  ${_failure}" PARENT_SCOPE)
    endif()
endfunction()

# The real file passes.
wjs_case("the wording as it is" CLEAN "${_real_text}" "")

# One of the design's sentences gone.
string(REPLACE "not met yet" "not seen yet" _no_not_met "${_real_text}")
wjs_case("a sentence of the design changed" "not met yet" "${_no_not_met}" "")

string(REPLACE "Found without a start tile: %1." "Without a start tile: %1." _no_unassisted "${_real_text}")
wjs_case("another sentence changed" "Found without a start tile" "${_no_unassisted}" "")

# Words an assessment or a gap is made of.
wjs_case("a word of assessment in the wording" "mastered"
    "${_real_text}\n    readonly property string x: qsTr(\"Mastered ai\")\n" "")
wjs_case("a gap" "missed"
    "${_real_text}\n    readonly property string y: qsTr(\"You missed ch\")\n" "")
wjs_case("a grade in a screen" "grade"
    "${_real_text}" "Text { text: \"A grade of 4\" }")
wjs_case("time played in a screen" "time played"
    "${_real_text}" "Text { text: \"Time played: 4\" }")

# A sentence outside the wording file.
wjs_case("a sentence in a screen" "outside ParentWording.qml"
    "${_real_text}" "Text { text: qsTr(\"Hello\") }")

# The same words in a comment are prose about the design.
wjs_case("a forbidden word in a comment" CLEAN
    "${_real_text}" "// never says mastered or weak\nText { text: ParentWording.back }")

file(REMOVE_RECURSE "${WORK_DIR}/case")

if(_failures)
    message(FATAL_ERROR "The wording check is wrong in ${_cases} cases:${_failures}")
endif()
message(STATUS "Wording check: ${_cases} cases hold")
