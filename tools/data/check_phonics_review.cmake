# SPDX-License-Identifier: GPL-3.0-or-later
# The review gate of a release build, in CMake so that building a release needs
# no PowerShell: refuses to pass while a phonics file under data/phonics has no
# complete review header (WJS_REQUIRE_REVIEWED_PHONICS, root CMakeLists.txt).
#
#   cmake -DROOT=<checkout> -P tools/data/check_phonics_review.cmake
#
# The header is the one tools/data/check_phonics.ps1 reads. A file starts with
# `# phase: <x>` and may carry
#   # reviewed-by: <name>   reviewed-on: <date>   source: <where it came from>
# and one without all three (reviewed-on as a real date, YYYY-MM-DD) is test
# data. A header still in angle brackets is a template, so is no header. This
# script applies the same header rules and the same file-name rule as the
# PowerShell checker, and only those: the lines of the data (graphemes, words,
# order, the dictionary) are checked by tools/data/check_phonics.ps1, which runs
# as the `data.phonics` ctest case on every host that has pwsh.
#
# No data/phonics directory, or none with a .txt file in it, is not a problem:
# there is nothing to review.
#
# Exit: 0 = every file is reviewed (or there is none), 1 = it is not.

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "check_phonics_review.cmake: pass -DROOT=<checkout>")
endif()

file(GLOB _files LIST_DIRECTORIES false "${ROOT}/data/phonics/*.txt")
list(SORT _files)
if(NOT _files)
    message(STATUS "OK: no phonics data under data/phonics, so nothing to review.")
    return()
endif()

string(ASCII 239 187 191 _bom)
set(_problems "")
set(_unreviewed 0)

function(_problem file message_text)
    set(_problems "${_problems}\n  ${file}  ${message_text}" PARENT_SCOPE)
endfunction()

# Whether "YYYY-MM-DD" is a date that exists.
function(_is_iso_date text result)
    set(${result} FALSE PARENT_SCOPE)
    if(NOT "${text}" MATCHES "^([0-9][0-9][0-9][0-9])-([0-9][0-9])-([0-9][0-9])$")
        return()
    endif()
    # The three groups first (the replacements below clear them), and leading
    # zeros off, which math() would read as octal.
    set(_year "${CMAKE_MATCH_1}")
    set(_month "${CMAKE_MATCH_2}")
    set(_day "${CMAKE_MATCH_3}")
    string(REGEX REPLACE "^0+([0-9])" "\\1" _year "${_year}")
    string(REGEX REPLACE "^0+([0-9])" "\\1" _month "${_month}")
    string(REGEX REPLACE "^0+([0-9])" "\\1" _day "${_day}")
    if(_year LESS 1 OR _month LESS 1 OR _month GREATER 12 OR _day LESS 1)
        return()
    endif()
    set(_days 31)
    if(_month EQUAL 4 OR _month EQUAL 6 OR _month EQUAL 9 OR _month EQUAL 11)
        set(_days 30)
    elseif(_month EQUAL 2)
        math(EXPR _by4 "${_year} % 4")
        math(EXPR _by100 "${_year} % 100")
        math(EXPR _by400 "${_year} % 400")
        if(_by400 EQUAL 0 OR (_by4 EQUAL 0 AND NOT _by100 EQUAL 0))
            set(_days 29)
        else()
            set(_days 28)
        endif()
    endif()
    if(_day GREATER _days)
        return()
    endif()
    set(${result} TRUE PARENT_SCOPE)
endfunction()

foreach(_path IN LISTS _files)
    get_filename_component(_name "${_path}" NAME)
    set(_name_phase "")
    if(_name MATCHES "^phase-([A-Za-z0-9]+)\\.(graphemes|words)\\.txt$")
        string(TOUPPER "${CMAKE_MATCH_1}" _name_phase)
    else()
        _problem("${_name}" "not named phase-<x>.graphemes.txt or phase-<x>.words.txt")
    endif()

    set(_header_phase "")
    set(_has_phase FALSE)
    set(_by "")
    set(_on "")
    set(_source "")
    set(_seen "")

    file(STRINGS "${_path}" _lines)
    set(_first TRUE)
    foreach(_line IN LISTS _lines)
        if(_first)
            set(_first FALSE)
            string(FIND "${_line}" "${_bom}" _at)
            if(_at EQUAL 0)
                string(SUBSTRING "${_line}" 3 -1 _line)
            endif()
        endif()
        string(STRIP "${_line}" _text)
        if(NOT _text MATCHES "^#")
            continue()
        endif()
        string(SUBSTRING "${_text}" 1 -1 _rest)
        set(_key_pattern "(^|[ \t])(phase|reviewed-by|reviewed-on|source):")
        while("${_rest}" MATCHES "${_key_pattern}(.*)$")
            set(_key "${CMAKE_MATCH_2}")
            set(_after "${CMAKE_MATCH_3}")
            # The value runs to the next key, or to the end of the line.
            string(REGEX MATCH "${_key_pattern}" _next "${_after}")
            set(_value "${_after}")
            set(_rest "")
            if(NOT "${_next}" STREQUAL "")
                string(FIND "${_after}" "${_next}" _next_at)
                string(SUBSTRING "${_after}" 0 ${_next_at} _value)
                string(SUBSTRING "${_after}" ${_next_at} -1 _rest)
            endif()
            string(STRIP "${_value}" _value)
            if(_key IN_LIST _seen)
                _problem("${_name}" "'${_key}:' is given twice")
                continue()
            endif()
            list(APPEND _seen "${_key}")
            if("${_value}" MATCHES "^<.*>$")
                set(_value "")
            endif()
            if(_key STREQUAL "phase")
                set(_has_phase TRUE)
                if("${_value}" MATCHES "^[A-Za-z0-9]+$")
                    string(TOUPPER "${_value}" _header_phase)
                else()
                    _problem("${_name}" "the phase must be letters and digits, as in '# phase: A'")
                endif()
            elseif(_key STREQUAL "reviewed-by")
                set(_by "${_value}")
            elseif(_key STREQUAL "reviewed-on")
                set(_on "${_value}")
            else()
                set(_source "${_value}")
            endif()
        endwhile()
    endforeach()

    if(NOT _has_phase)
        _problem("${_name}" "no '# phase: <name>' header")
    elseif(NOT "${_header_phase}" STREQUAL "" AND NOT "${_name_phase}" STREQUAL ""
           AND NOT "${_header_phase}" STREQUAL "${_name_phase}")
        _problem("${_name}" "its header says phase ${_header_phase}, its name says phase ${_name_phase}")
    endif()

    set(_has_by FALSE)
    set(_has_on FALSE)
    if(NOT "${_by}" STREQUAL "")
        set(_has_by TRUE)
    endif()
    if(NOT "${_on}" STREQUAL "")
        set(_has_on TRUE)
    endif()
    if(NOT _has_by STREQUAL _has_on)
        _problem("${_name}" "give both reviewed-by and reviewed-on, or neither (neither is test data)")
    endif()
    set(_date_ok FALSE)
    if(_has_on)
        _is_iso_date("${_on}" _date_ok)
        if(NOT _date_ok)
            _problem("${_name}" "reviewed-on must be a date written YYYY-MM-DD")
        endif()
    endif()
    set(_has_source FALSE)
    if(NOT "${_source}" STREQUAL "")
        set(_has_source TRUE)
    endif()
    if(_has_by AND _has_on AND NOT _has_source)
        _problem("${_name}" "a reviewed file says where its list came from: add 'source: <where>'")
    endif()

    if(NOT (_has_by AND _has_on AND _has_source AND _date_ok))
        math(EXPR _unreviewed "${_unreviewed} + 1")
        _problem("${_name}" "has no complete review header (reviewed-by, reviewed-on and source), so it is test data, which a release build refuses")
    endif()
endforeach()

if(_problems)
    message(FATAL_ERROR "Phonics data problem(s):${_problems}\n")
endif()

list(LENGTH _files _count)
message(STATUS "OK: ${_count} phonics file(s), all reviewed.")
