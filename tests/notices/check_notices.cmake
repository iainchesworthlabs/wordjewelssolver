# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The third-party notices a package carries, held to the sections it has to
# state (docs/licensing.md, "Notices").
#
#   cmake -DNOTICES=<NOTICES.txt> -DVERSION=<project version>
#         -DEXPECT_CLI=ON|OFF -DEXPECT_QT=ON|OFF [-DQT_VERSION=<version>]
#         -P tests/notices/check_notices.cmake
#
# cmake/Notices.cmake writes the file at configure time from
# notices/fragments/, so this checks the generated text rather than the
# fragments: a token left unfilled, a fragment dropped from the list, or the
# application's licence section losing its GPL-3.0-only sentence is a failure
# here. A build without the application has no Qt section, and this holds it to
# that too: a command-line package must not claim to carry Qt.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT NOTICES)
    message(FATAL_ERROR "Pass -DNOTICES=<NOTICES.txt>.")
endif()
if(NOT EXISTS "${NOTICES}")
    message(FATAL_ERROR "${NOTICES} does not exist: cmake/Notices.cmake did not write it.")
endif()

file(READ "${NOTICES}" _text)

set(_failures "")

# No fragment marker may be left: a package whose notice says "{{QT_VERSION}}"
# is worse than one with no notice.
string(REGEX MATCH "{{[A-Za-z0-9_:.-]+}}" _leftover "${_text}")
if(_leftover)
    list(APPEND _failures "${NOTICES} still contains the placeholder ${_leftover}")
endif()

# The header names the version, and the project's own licence is stated.
set(_required
    "Word Jewels Solver ${VERSION}"
    "notices for the"
    "GNU General Public License"
    "Catch2"
    "Boost Software License"
    "ENABLE"
    "public domain"
    "translation"
    "LICENSE.txt")

if(EXPECT_CLI)
    list(APPEND _required
        "wjs-cli"
        "GPL-3.0-or-later")
endif()

if(EXPECT_QT)
    if(NOT QT_VERSION)
        message(FATAL_ERROR "EXPECT_QT is ON, so -DQT_VERSION=<version> is required.")
    endif()
    list(APPEND _required
        # The application binary is GPL-3.0-only because it links Quick 3D, and
        # the notice has to say so plainly.
        "GPL-3.0-only"
        "Qt ${QT_VERSION}"
        "GNU Lesser General Public License"
        "Quick 3D"
        # The feedback engines' modules, under the same LGPL/GPL options.
        "Multimedia"
        "Sensors"
        "TextToSpeech"
        "feedback sounds"
        "light probe"
        "font")
else()
    # No application in this build, so no section may describe one.
    foreach(_absent IN ITEMS "Quick 3D" "GNU Lesser General Public License" "GPL-3.0-only"
            "Multimedia" "TextToSpeech" "feedback sounds")
        string(FIND "${_text}" "${_absent}" _found)
        if(NOT _found EQUAL -1)
            list(APPEND _failures
                "${NOTICES} describes '${_absent}', but this build has no application")
        endif()
    endforeach()
endif()

foreach(_phrase IN LISTS _required)
    string(FIND "${_text}" "${_phrase}" _found)
    if(_found EQUAL -1)
        list(APPEND _failures "${NOTICES} does not contain: ${_phrase}")
    endif()
endforeach()

if(_failures)
    string(REPLACE ";" "\n  " _report "${_failures}")
    message(FATAL_ERROR "The third-party notices:\n  ${_report}\n")
endif()

message(STATUS
    "Notices: ${NOTICES} states every required section (application: ${EXPECT_QT}, CLI: ${EXPECT_CLI})")
