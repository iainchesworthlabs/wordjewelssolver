# SPDX-License-Identifier: GPL-3.0-or-later
# The board scene is one component with one interface (docs/design/board.md, "The
# shake and the drop"; docs/design/shake-prototype.md, "The interface"): given a
# finished board and a seed, play the shake and the drop and say when the dice are
# settled. QML has no interface type, so the contract is a list of members and this
# check: every file that is a BoardScene (the application's 3D scene, and the 2D
# fallback kept for D2) has to declare them all, under the same names.
#
# Run as: cmake -DSCENES="<file>;<file>" -P check_scene_contract.cmake
# A file in the list that does not exist is reported and skipped, so a scene that is
# moved or renamed does not break the build of the other.

if(NOT DEFINED SCENES)
    message(FATAL_ERROR "check_scene_contract.cmake: pass -DSCENES=<files>")
endif()

# <what it is>|<a regular expression that finds its declaration>
set(_members
    "reducedMotion, in|property[ \t]+bool[ \t]+reducedMotion"
    "pushMotion(ax, ay, az, wx, wy, wz)|function[ \t]+pushMotion[ \t]*\\([ \t]*ax[ \t]*,[ \t]*ay[ \t]*,[ \t]*az[ \t]*,[ \t]*wx[ \t]*,[ \t]*wy[ \t]*,[ \t]*wz[ \t]*\\)"
    "settled(), out|signal[ \t]+settled"
    "landed(index, pitch), out|signal[ \t]+landed[ \t]*\\([ \t]*int[ \t]+index[ \t]*,[ \t]*real[ \t]+pitch[ \t]*\\)"
    "rattled(), out|signal[ \t]+rattled"
    "busy, read-only|readonly[ \t]+property[ \t]+bool[ \t]+busy"
    "rows, read-only|readonly[ \t]+property[ \t]+int[ \t]+rows"
    "columns, read-only|readonly[ \t]+property[ \t]+int[ \t]+columns"
    "tileSize, read-only|readonly[ \t]+property[ \t]+real[ \t]+tileSize"
    "tileGap, read-only|readonly[ \t]+property[ \t]+real[ \t]+tileGap"
    "boardRect, read-only|readonly[ \t]+property[ \t]+rect[ \t]+boardRect"
    "overdraw, read-only|readonly[ \t]+property[ \t]+real[ \t]+overdraw"
    "play(board, seed)|function[ \t]+play[ \t]*\\([ \t]*board[ \t]*,[ \t]*seed[ \t]*\\)"
    "show(board)|function[ \t]+show[ \t]*\\([ \t]*board[ \t]*\\)"
    "prepare(board)|function[ \t]+prepare[ \t]*\\([ \t]*board[ \t]*\\)")

set(_failed FALSE)
set(_checked 0)
foreach(_scene IN LISTS SCENES)
    if(NOT EXISTS "${_scene}")
        message(STATUS "skipped (not there): ${_scene}")
        continue()
    endif()
    math(EXPR _checked "${_checked} + 1")
    file(READ "${_scene}" _text)
    foreach(_member IN LISTS _members)
        string(REPLACE "|" ";" _parts "${_member}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _pattern)
        if(NOT _text MATCHES "${_pattern}")
            message(SEND_ERROR "${_scene}: no declaration of ${_name}")
            set(_failed TRUE)
        endif()
    endforeach()
endforeach()

if(_checked EQUAL 0)
    message(FATAL_ERROR "no scene file was found among: ${SCENES}")
endif()
if(_failed)
    message(FATAL_ERROR "a board scene does not match the interface of docs/design/shake-prototype.md")
endif()
message(STATUS "${_checked} board scene(s) declare the whole interface")
