# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The design system's guard rail over the application's QML sources: a screen
# says what a thing IS (a Label's role, a Theme token) and never how it looks.
#
#   cmake -DREPO_ROOT=<repository> [-DREPORT=<file>] -P apps/app/ui-tests/check_qml_literals.cmake
#
# It scans apps/app/qml/**.qml (the 3D scene in qml/scene is exempt: it is drawn
# into textures and reads its colours from Theme by hand) and counts, per file:
#
#   PIXEL_SIZE        font.pixelSize / font.pointSize set to a number
#   LETTER_SPACING    font.letterSpacing set to a number (Label sets it in em)
#   HEX_COLOUR        a "#rrggbb" colour outside kit/Theme.qml
#   BARE_TEXT         a Text { } outside kit/Label.qml (a Label sets the face,
#                     the size, the line height and honours the text size)
#   DISABLED_OPACITY  opacity tied to `enabled` (a disabled look is a token set)
#   GOLD_SELECTION    Theme.gold on a line about checked / selected / current
#                     (gold is the reward; a chosen option is Theme.selected*)
#   ICON_ONLY         an AppButton with an icon and no label that is not Back or
#                     Close (icon-only is for those two)
#   LEGACY            ParentTheme, PButton or PChoice, which no longer exist
#
# A file may have at most as many of a rule as the allow-list in
# qml_lint_allowlist.cmake says (none if it is not listed; LEGACY has no list).
# It is a ratchet: the screen streams lower the numbers as they fix their files,
# and nothing may add one. A count below its allowance passes, with a note, so a
# stream that fixes a file does not have to touch this one to stay green.
#
# With -DREPORT=<file> it writes the current counts as allow-list lines to that
# file (to paste into the allow-list) and passes.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()
if(NOT ALLOWLIST)
    set(ALLOWLIST "${REPO_ROOT}/apps/app/ui-tests/qml_lint_allowlist.cmake")
endif()

set(_qml_root "${REPO_ROOT}/apps/app/qml")
include("${ALLOWLIST}")

set(WJS_LINT_RULES PIXEL_SIZE LETTER_SPACING HEX_COLOUR BARE_TEXT DISABLED_OPACITY GOLD_SELECTION ICON_ONLY)

# The text of a { } block that begins at `start` (the index of its "{").
function(wjs_block text start out)
    string(LENGTH "${text}" _length)
    set(_depth 0)
    set(_index ${start})
    while(_index LESS _length)
        string(SUBSTRING "${text}" ${_index} 1 _char)
        if(_char STREQUAL "{")
            math(EXPR _depth "${_depth} + 1")
        elseif(_char STREQUAL "}")
            math(EXPR _depth "${_depth} - 1")
            if(_depth EQUAL 0)
                math(EXPR _size "${_index} - ${start} + 1")
                string(SUBSTRING "${text}" ${start} ${_size} _found)
                set(${out} "${_found}" PARENT_SCOPE)
                return()
            endif()
        endif()
        math(EXPR _index "${_index} + 1")
    endwhile()
    set(${out} "" PARENT_SCOPE)
endfunction()

# How many AppButtons in `text` carry an icon that is not back or close and no
# label of their own.
function(wjs_count_icon_only text out)
    set(_count 0)
    set(_from 0)
    while(TRUE)
        string(SUBSTRING "${text}" ${_from} -1 _rest)
        string(FIND "${_rest}" "AppButton" _at)
        if(_at EQUAL -1)
            break()
        endif()
        math(EXPR _at "${_from} + ${_at}")
        math(EXPR _after "${_at} + 9")
        string(SUBSTRING "${text}" ${_after} 12 _next)
        # A type use is `AppButton {`; the word also appears in comments we have
        # already stripped, and in `AppButton.qml`-style names.
        if(_next MATCHES "^[ \t\r\n]*\\{")
            string(SUBSTRING "${text}" ${_after} -1 _tail)
            string(FIND "${_tail}" "{" _brace_rel)
            math(EXPR _brace "${_after} + ${_brace_rel}")
            wjs_block("${text}" ${_brace} _block)
            if(_block MATCHES "iconName[ \t]*:" AND NOT _block MATCHES "[^A-Za-z]label[ \t]*:")
                if(NOT _block MATCHES "iconName[ \t]*:[ \t]*\"(back|close)\"")
                    math(EXPR _count "${_count} + 1")
                endif()
            endif()
        endif()
        set(_from ${_after})
    endwhile()
    set(${out} ${_count} PARENT_SCOPE)
endfunction()

function(wjs_count_matches text pattern out)
    string(REGEX MATCHALL "${pattern}" _all "${text}")
    list(LENGTH _all _n)
    set(${out} ${_n} PARENT_SCOPE)
endfunction()

# The allowance of `rule` for `file`: the number after "file=" in the list.
function(wjs_allowance rule file out)
    set(_value 0)
    foreach(_entry IN LISTS WJS_ALLOW_${rule})
        string(FIND "${_entry}" "=" _eq)
        string(SUBSTRING "${_entry}" 0 ${_eq} _name)
        if(_name STREQUAL file)
            math(EXPR _from "${_eq} + 1")
            string(SUBSTRING "${_entry}" ${_from} -1 _value)
        endif()
    endforeach()
    set(${out} ${_value} PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE _files RELATIVE "${_qml_root}" "${_qml_root}/*.qml")
list(SORT _files)

set(_failures "")
set(_notes "")
foreach(_rule IN LISTS WJS_LINT_RULES)
    set(_total_${_rule} 0)
    set(_lines_${_rule} "")
endforeach()

foreach(_file IN LISTS _files)
    if(_file MATCHES "^scene/")
        continue()
    endif()
    file(READ "${_qml_root}/${_file}" _text)
    # Comments are not code: // to the end of the line (not inside a URL) and
    # /* ... */ blocks.
    string(REGEX REPLACE "(^|[ \t\n])//[^\n]*" "\\1" _text "${_text}")
    string(REGEX REPLACE "/\\*([^*]|\\*[^/])*\\*/" "" _text "${_text}")

    # The names that no longer exist.
    string(REGEX MATCHALL "ParentTheme|PButton|PChoice" _legacy "${_text}")
    list(LENGTH _legacy _legacy_count)
    if(_legacy_count GREATER 0)
        string(APPEND _failures "\n  ${_file}: ${_legacy_count} use(s) of ParentTheme/PButton/PChoice, which were deleted (use Theme, AppButton, SegmentedChoice)")
    endif()

    wjs_count_matches("${_text}" "pixelSize[ \t]*:[ \t]*[0-9]|pointSize[ \t]*:[ \t]*[0-9]" _n_PIXEL_SIZE)
    wjs_count_matches("${_text}" "letterSpacing[ \t]*:[ \t]*-?[0-9.]" _n_LETTER_SPACING)
    set(_n_HEX_COLOUR 0)
    if(NOT _file STREQUAL "kit/Theme.qml")
        wjs_count_matches("${_text}" "[\"']#[0-9A-Fa-f]+[\"']" _n_HEX_COLOUR)
    endif()
    if(_file STREQUAL "kit/Theme.qml")
        set(_n_PIXEL_SIZE 0)
        set(_n_LETTER_SPACING 0)
    endif()
    set(_n_BARE_TEXT 0)
    if(NOT _file STREQUAL "kit/Label.qml")
        wjs_count_matches("${_text}" "(^|[^A-Za-z0-9_.])Text[ \t]*\\{" _n_BARE_TEXT)
    endif()
    wjs_count_matches("${_text}" "opacity[ \t]*:[^\n]*enabled[^\n]*:[ \t]*0?\\.[0-9]+" _n_DISABLED_OPACITY)
    wjs_count_matches("${_text}" "(^|[\n])[^\n]*(checked|selected|current)[^\n]*Theme\\.gold[^\n]*|(^|[\n])[^\n]*Theme\\.gold[^\n]*(checked|selected|current)[^\n]*" _n_GOLD_SELECTION)
    wjs_count_icon_only("${_text}" _n_ICON_ONLY)

    foreach(_rule IN LISTS WJS_LINT_RULES)
        set(_found ${_n_${_rule}})
        math(EXPR _total_${_rule} "${_total_${_rule}} + ${_found}")
        if(_found GREATER 0)
            string(APPEND _lines_${_rule} "    \"${_file}=${_found}\"\n")
        endif()
        wjs_allowance(${_rule} "${_file}" _allowed)
        if(_found GREATER _allowed)
            string(APPEND _failures "\n  ${_file}: ${_found} ${_rule} (allowed ${_allowed})")
        elseif(_found LESS _allowed)
            string(APPEND _notes "\n  ${_file}: ${_found} ${_rule}, below its allowance of ${_allowed}: lower it in apps/app/ui-tests/qml_lint_allowlist.cmake")
        endif()
    endforeach()
endforeach()

if(REPORT)
    set(_allow_text "")
    foreach(_rule IN LISTS WJS_LINT_RULES)
        string(APPEND _allow_text "set(WJS_ALLOW_${_rule}
${_lines_${_rule}})

")
    endforeach()
    file(WRITE "${REPORT}" "${_allow_text}")
    message(STATUS "allow-list lines written to ${REPORT}")
    cmake_language(EXIT 0)
endif()

set(_summary "")
foreach(_rule IN LISTS WJS_LINT_RULES)
    string(APPEND _summary " ${_rule}=${_total_${_rule}}")
endforeach()
message(STATUS "qml lint (allowed offences still in the tree):${_summary}")
if(_notes)
    message(STATUS "qml lint notes:${_notes}")
endif()
if(_failures)
    message(FATAL_ERROR
        "The QML sources break the design system's rules (docs/design/application.md, "
        "apps/app/ui-tests/check_qml_literals.cmake). A screen picks a Label role and a Theme "
        "token; it does not write a size, a colour or a font:${_failures}")
endif()
