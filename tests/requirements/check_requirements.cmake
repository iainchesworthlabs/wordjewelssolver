# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The requirements matrix (docs/requirements.md and docs/requirements/*.md) is
# held to the tree: a row may not point at a test, a file or a status that does
# not exist, so a renamed or deleted test breaks the page that cites it instead
# of leaving a claim nobody can check.
#
#   cmake -DREPO_ROOT=<repository root> -P tests/requirements/check_requirements.cmake
#
# What it checks:
#   * every page under docs/requirements/ says `Verified at <date>, commit <sha>`;
#   * a row is `| ID | Requirement | Status | Implemented in | Demonstrated by | Notes |`
#     with an ID of the form R-<AREA>-<n>[a-z] (a row may carry several, comma
#     separated), the IDs are unique across the pages, and Status is one of
#     Implemented, Partial, Not built, Deferred, Decided, Doc only;
#   * every `path::needle` in "Demonstrated by" names a file that exists and
#     contains the needle verbatim (a Catch2 case name, a QML `test_` function,
#     a ctest name in the CMake file that registers it);
#   * every `path` with a slash in "Implemented in" exists;
#   * the number of Implemented or Partial rows with no test reference equals the
#     figure stated in docs/requirements.md ("Rows with no test: N"), so the
#     number cannot grow unseen, and falls only by being corrected.
#
# It reads text only (no build, no PowerShell), so it runs in every leg.
# test_check_requirements.cmake holds it to a table of cases on throw-away trees.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()

set(_allowed_status "Implemented" "Partial" "Not built" "Deferred" "Decided" "Doc only")
# Stand-ins for the characters that would split or join CMake list elements.
set(_semi "@@SEMI@@")
set(_lb "@@LB@@")
set(_rb "@@RB@@")
set(_bs "@@BS@@")

file(GLOB _pages "${REPO_ROOT}/docs/requirements/*.md")
list(SORT _pages)
if(NOT _pages)
    message(FATAL_ERROR "No pages under ${REPO_ROOT}/docs/requirements/.")
endif()

set(_problems "")
set(_seen_ids "")
set(_rows 0)
set(_refs 0)
set(_untested 0)

# The text of a file from the repository, read once.
function(wjs_read_repo_file relative out_found out_text)
    string(MD5 _key "${relative}")
    get_property(_have GLOBAL PROPERTY WJS_REQ_FILE_${_key} SET)
    if(NOT _have)
        if(EXISTS "${REPO_ROOT}/${relative}" AND NOT IS_DIRECTORY "${REPO_ROOT}/${relative}")
            file(READ "${REPO_ROOT}/${relative}" _text)
            set_property(GLOBAL PROPERTY WJS_REQ_FILE_${_key} "1${_text}")
        else()
            set_property(GLOBAL PROPERTY WJS_REQ_FILE_${_key} "0")
        endif()
    endif()
    get_property(_stored GLOBAL PROPERTY WJS_REQ_FILE_${_key})
    string(SUBSTRING "${_stored}" 0 1 _flag)
    string(SUBSTRING "${_stored}" 1 -1 _body)
    if(_flag STREQUAL "1")
        set(${out_found} TRUE PARENT_SCOPE)
    else()
        set(${out_found} FALSE PARENT_SCOPE)
    endif()
    set(${out_text} "${_body}" PARENT_SCOPE)
endfunction()

foreach(_page IN LISTS _pages)
    file(RELATIVE_PATH _page_name "${REPO_ROOT}" "${_page}")
    file(READ "${_page}" _content)
    string(REPLACE "\r\n" "\n" _content "${_content}")

    if(NOT _content MATCHES "Verified at [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9], commit [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]")
        string(APPEND _problems "${_page_name}: no 'Verified at <date>, commit <sha>' line\n")
    endif()

    string(REPLACE "\\" "${_bs}" _content "${_content}")
    string(REPLACE ";" "${_semi}" _content "${_content}")
    string(REPLACE "[" "${_lb}" _content "${_content}")
    string(REPLACE "]" "${_rb}" _content "${_content}")
    string(REPLACE "\n" ";" _lines "${_content}")

    foreach(_line IN LISTS _lines)
        if(NOT _line MATCHES "^\\| *R-")
            continue()
        endif()
        if(NOT _line MATCHES "^\\| *([^|]+[^| ]) *\\| *([^|]*) *\\| *([^|]*[^| ]) *\\| *([^|]*) *\\| *([^|]*) *\\| *([^|]*) *\\|$")
            string(APPEND _problems "${_page_name}: a row that is not six cells: ${_line}\n")
            continue()
        endif()
        set(_ids "${CMAKE_MATCH_1}")
        set(_status "${CMAKE_MATCH_3}")
        set(_implemented "${CMAKE_MATCH_4}")
        set(_demonstrated "${CMAKE_MATCH_5}")
        math(EXPR _rows "${_rows} + 1")

        # The IDs.
        string(REPLACE "," ";" _id_list "${_ids}")
        foreach(_id IN LISTS _id_list)
            string(STRIP "${_id}" _id)
            if(NOT _id MATCHES "^R-[A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9]?-[0-9]+[a-z]?$")
                string(APPEND _problems "${_page_name}: '${_id}' is not an ID (R-AREA-n)\n")
                continue()
            endif()
            if(_id IN_LIST _seen_ids)
                string(APPEND _problems "${_page_name}: ${_id} appears twice\n")
            endif()
            list(APPEND _seen_ids "${_id}")
        endforeach()

        # The status.
        if(NOT _status IN_LIST _allowed_status)
            string(APPEND _problems "${_page_name}: ${_ids}: status '${_status}' is not one of: ${_allowed_status}\n")
        endif()

        # Implemented in: a path with a slash must exist.
        set(_rest "${_implemented}")
        while(_rest MATCHES "`([^`]+)`")
            set(_token "${CMAKE_MATCH_1}")
            string(REPLACE "`${_token}`" "" _rest "${_rest}")
            if(_token MATCHES "/" AND NOT _token MATCHES " ")
                if(NOT EXISTS "${REPO_ROOT}/${_token}")
                    string(APPEND _problems "${_page_name}: ${_ids}: implemented in '${_token}', which does not exist\n")
                endif()
            endif()
        endwhile()

        # Demonstrated by: path::needle must be in the file.
        set(_row_refs 0)
        set(_rest "${_demonstrated}")
        while(_rest MATCHES "`([^`:]+)::([^`]+)`")
            set(_path "${CMAKE_MATCH_1}")
            set(_needle "${CMAKE_MATCH_2}")
            string(REPLACE "`${_path}::${_needle}`" "" _rest "${_rest}")
            string(REPLACE "${_semi}" ";" _needle "${_needle}")
            string(REPLACE "${_lb}" "[" _needle "${_needle}")
            string(REPLACE "${_rb}" "]" _needle "${_needle}")
            string(REPLACE "${_bs}" "\\" _needle "${_needle}")
            math(EXPR _row_refs "${_row_refs} + 1")
            math(EXPR _refs "${_refs} + 1")

            wjs_read_repo_file("${_path}" _found _text)
            if(NOT _found)
                string(APPEND _problems "${_page_name}: ${_ids}: test file '${_path}' does not exist\n")
            else()
                string(FIND "${_text}" "${_needle}" _at)
                if(_at EQUAL -1)
                    string(APPEND _problems "${_page_name}: ${_ids}: '${_needle}' is not in ${_path}\n")
                endif()
            endif()
        endwhile()

        if(_row_refs EQUAL 0 AND (_status STREQUAL "Implemented" OR _status STREQUAL "Partial"))
            math(EXPR _untested "${_untested} + 1")
        endif()
    endforeach()
endforeach()

# The figure the index states.
set(_stated "")
if(EXISTS "${REPO_ROOT}/docs/requirements.md")
    file(READ "${REPO_ROOT}/docs/requirements.md" _index)
    if(_index MATCHES "Rows with no test: \\*\\*([0-9]+)\\*\\*")
        set(_stated "${CMAKE_MATCH_1}")
    endif()
endif()
if(_stated STREQUAL "")
    string(APPEND _problems "docs/requirements.md: no 'Rows with no test: **N**' figure\n")
elseif(NOT _stated EQUAL _untested)
    string(APPEND _problems
        "docs/requirements.md says ${_stated} Implemented or Partial rows have no test, the pages have ${_untested}: "
        "correct the figure (it may only fall because a test was cited, and rise never)\n")
endif()

if(_rows EQUAL 0)
    string(APPEND _problems "no requirement rows found\n")
endif()

if(_problems)
    # One status line a problem: a fatal error's text is re-wrapped, which would
    # split a path from its test name.
    string(REPLACE ";" "," _problems "${_problems}")
    string(REPLACE "\n" ";" _problem_lines "${_problems}")
    foreach(_problem IN LISTS _problem_lines)
        if(NOT _problem STREQUAL "")
            message(STATUS "requirements: ${_problem}")
        endif()
    endforeach()
    message(FATAL_ERROR "The requirements matrix does not match the tree (the lines above say where).")
endif()
message(STATUS "requirements: ${_rows} rows, ${_refs} test references checked, ${_untested} rows with no test")
