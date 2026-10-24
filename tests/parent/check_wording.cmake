# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The words a parent reads (docs/design/dashboard.md, "Wording"), held to the
# design.
#
#   cmake -DREPO_ROOT=<repository root> -P tests/parent/check_wording.cmake
#
# Every sentence of the parent area's screens is in one file,
# apps/app/qml/parent/ParentWording.qml, so that the literacy reviewer reads one
# place (the wording is a proposal for that review, gate G7). This holds that
# file to three things:
#
#   1. the sentences the design quotes are there, as the design quotes them;
#   2. none of the words an assessment, a grade, a gap or a pull is made of is
#      in any screen of the parent area ("weak", "mastered", "missed" ...);
#   3. no other file of the parent area has a sentence of its own: no qsTr()
#      outside the wording file.
#
# The test parent.screens checks the same of what is drawn, with data; this
# checks the source, with none, and runs on every host.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()

set(_directory "${REPO_ROOT}/apps/app/qml/parent")
set(_wording "${_directory}/ParentWording.qml")
if(NOT EXISTS "${_wording}")
    message(FATAL_ERROR "${_wording} does not exist.")
endif()

file(READ "${_wording}" _wording_text)

set(_failures "")

# --- 1. the design's sentences ---------------------------------------------------
# Each is a template of ParentWording.qml, in the form the design quotes
# (docs/design/dashboard.md): the placeholders are the numbers and the sound.
set(_required
    # "Words with ai have been on 6 boards. 4 were found."
    "Words with <i>%1</i> have been on %2. %3"
    "%1 were found."
    "1 was found."
    "%1 boards"
    "1 board"
    # "Found without a start tile: 3."
    "Found without a start tile: %1."
    # "This week: 3 boards cleared, 14 stars, 2 new words."
    "This week: %1."
    "%1 boards cleared"
    "%1 stars"
    "%1 new words"
    # A sound that has not been met is "not met yet", never "missed".
    "not met yet"
    # Few words use a ticked sound.
    "few words use this sound yet"
    # "on 6 boards; found on 1"
    "on %1<SEMI> found on %2"
    # The terms of the definitions.
    "Often found")
foreach(_sentence IN LISTS _required)
    string(REPLACE "<SEMI>" ";" _sentence "${_sentence}")
    string(FIND "${_wording_text}" "qsTr(\"${_sentence}\")" _in_tr)
    if(_in_tr EQUAL -1)
        list(APPEND _failures "ParentWording.qml lacks the design's sentence: ${_sentence}")
    endif()
endforeach()

# --- 2. words that must not be there -------------------------------------------------
# Lower case; a word as it would be inside a sentence. "fail" and "miss" also
# catch "failed" and "missed". A comment is prose about the design and skipped.
set(_forbidden
    weak mastered mastery missed "blind spot" emerging grade score streak behind fail
    deficit wrong poor struggl below "time played" minutes hours daily "well done"
    "keep going" "don't" "try again" lives "game over")

file(GLOB _screens "${_directory}/*.qml")
set(_scanned 0)
foreach(_screen IN LISTS _screens)
    math(EXPR _scanned "${_scanned} + 1")
    get_filename_component(_name "${_screen}" NAME)
    file(READ "${_screen}" _content)
    string(REPLACE ";" "<SEMI>" _content "${_content}")
    string(REPLACE "[" "<LB>" _content "${_content}")
    string(REPLACE "]" "<RB>" _content "${_content}")
    string(REPLACE "\n" ";" _lines "${_content}")

    set(_number 0)
    foreach(_line IN LISTS _lines)
        math(EXPR _number "${_number} + 1")
        if(_line MATCHES "^[ \t]*(//|/[*]|[*])")
            continue()
        endif()
        string(TOLOWER "${_line}" _lower)
        foreach(_word IN LISTS _forbidden)
            string(FIND "${_lower}" "${_word}" _found)
            if(NOT _found EQUAL -1)
                list(APPEND _failures "${_name}:${_number}: contains \"${_word}\": ${_line}")
            endif()
        endforeach()

        # --- 3. one place for sentences ---------------------------------------------
        if(NOT _name STREQUAL "ParentWording.qml" AND _line MATCHES "qsTr[(]")
            list(APPEND _failures "${_name}:${_number}: a sentence outside ParentWording.qml: ${_line}")
        endif()
    endforeach()
endforeach()

if(_scanned EQUAL 0)
    list(APPEND _failures "no QML files under ${_directory}")
endif()

if(_failures)
    string(REPLACE ";" "\n  " _report "${_failures}")
    message(FATAL_ERROR "The parent area's wording:\n  ${_report}\n")
endif()

message(STATUS "Parent wording: ${_scanned} files, the design's sentences present, no forbidden word")
