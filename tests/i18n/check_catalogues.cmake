# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The interface's catalogues are what lupdate makes from the sources now
# (docs/design/application.md, "Languages"; docs/ci.md).
#
#   cmake -DLUPDATE=<lupdate> -DREPO_ROOT=<repository>
#         -DWORK_DIR=<empty directory> -P tests/i18n/check_catalogues.cmake
#
# Copies the checked-in apps/app/translations/*.ts into WORK_DIR, runs lupdate over the same
# source list the update target uses, and fails when a checked-in catalogue
# differs from the regenerated one. That is the gate for a qsTr() added without
# its catalogue, or a source line moved without regenerating the locations.
#
# It reports itself skipped (exit 4) when lupdate is not there: the Linux and
# macOS build legs do not build the application, and a developer without Qt's
# Linguist tools can still build and test everything else.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()
if(NOT WORK_DIR)
    message(FATAL_ERROR "Pass -DWORK_DIR=<a directory to work in>.")
endif()

include("${REPO_ROOT}/cmake/Translations.cmake")

if(NOT LUPDATE OR NOT EXISTS "${LUPDATE}")
    message(STATUS "i18n: lupdate is not available; the catalogue drift check is skipped")
    cmake_language(EXIT 4)
endif()

set(_missing "")
foreach(_ts IN LISTS WJS_TS_FILES)
    if(NOT EXISTS "${_ts}")
        list(APPEND _missing "${_ts}")
    endif()
endforeach()
if(_missing)
    string(REPLACE ";" "\n  " _report "${_missing}")
    message(FATAL_ERROR
        "These catalogues are missing (they are generated, and checked in):\n  ${_report}\n"
        "Run: cmake --build <build> --target wjs_update_translations")
endif()

# A copy of the checked-in catalogues, so the check never rewrites the tree.
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(_generated "")
foreach(_ts IN LISTS WJS_TS_FILES)
    get_filename_component(_name "${_ts}" NAME)
    set(_copy "${WORK_DIR}/${_name}")
    configure_file("${_ts}" "${_copy}" COPYONLY)
    list(APPEND _generated "${_copy}")
endforeach()

execute_process(
    COMMAND "${LUPDATE}" -locations relative -no-obsolete
            ${WJS_TRANSLATION_SOURCES} -ts ${_generated}
    WORKING_DIRECTORY "${REPO_ROOT}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _errors)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "lupdate failed (${_result}):\n${_output}\n${_errors}")
endif()

# lupdate writes a location's filename relative to the .ts file's own directory.
# The checked-in catalogues live in apps/app/translations/, the regenerated ones in WORK_DIR, so
# the same source is named "../qml/..." in one and "../../../../../apps/app/qml/..." in the
# other. The path from the application's directory is what is compared; everything
# else in the line (the line number and its delta) still has to match.
function(wjs_normalise_catalogue variable)
    string(REGEX REPLACE "filename=\"[^\"]*/((src|qml)/[^\"]*)\"" "filename=\"\\1\""
        _normalised "${${variable}}")
    set("${variable}" "${_normalised}" PARENT_SCOPE)
endfunction()

set(_drift "")
foreach(_ts IN LISTS WJS_TS_FILES)
    get_filename_component(_name "${_ts}" NAME)
    set(_copy "${WORK_DIR}/${_name}")
    file(READ "${_ts}" _checked)
    file(READ "${_copy}" _regenerated)
    # A checkout that gave the .ts a CRLF is not a catalogue that drifted; the
    # comparison is over the text lupdate writes (LF).
    string(REPLACE "\r\n" "\n" _checked "${_checked}")
    string(REPLACE "\r\n" "\n" _regenerated "${_regenerated}")
    wjs_normalise_catalogue(_checked)
    wjs_normalise_catalogue(_regenerated)
    if(NOT _checked STREQUAL _regenerated)
        list(APPEND _drift "${_ts}")
    endif()
endforeach()

if(_drift)
    string(REPLACE ";" "\n  " _report "${_drift}")
    message(FATAL_ERROR
        "The interface's catalogues are out of date:\n  ${_report}\n"
        "They are generated from the sources (apps/app/translations/sources are listed in "
        "cmake/Translations.cmake). Regenerate and translate anything new:\n"
        "  cmake --build <build> --target wjs_update_translations")
endif()

string(REPLACE ";" ", " _languages "${WJS_TRANSLATION_LANGUAGES}")
message(STATUS "i18n: ${_languages} catalogues are up to date")
