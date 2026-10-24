# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The QML literal check held to cases on throw-away trees: each rule catches its
# own offence, the exempt places stay exempt, the allow-list is a ceiling and
# not a licence to add, and the real tree passes.
#
#   cmake -DREPO_ROOT=<repository root> -DWORK_DIR=<scratch directory> \
#         -P apps/app/ui-tests/test_check_qml_literals.cmake
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT OR NOT WORK_DIR)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root> and -DWORK_DIR=<scratch directory>.")
endif()

set(_checker "${REPO_ROOT}/apps/app/ui-tests/check_qml_literals.cmake")
set(_failures "")
set(_cases 0)

# A tree holding one QML file at `path` (under apps/app/qml) with `content`,
# checked against an allow-list of `allow`. `expect` is a text the report must
# contain, or CLEAN.
function(wjs_case label expect path content allow)
    set(_tree "${WORK_DIR}/case")
    file(REMOVE_RECURSE "${_tree}")
    get_filename_component(_dir "${_tree}/apps/app/qml/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")
    file(WRITE "${_tree}/apps/app/qml/${path}" "${content}\n")
    file(WRITE "${_tree}/allow.cmake" "${allow}\n")

    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DREPO_ROOT=${_tree}" "-DALLOWLIST=${_tree}/allow.cmake" -P "${_checker}"
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

set(_none "set(WJS_ALLOW_PIXEL_SIZE)")

wjs_case("a screen of Labels and tokens" CLEAN "Screen.qml"
    "import QtQuick\nItem {\n    Label { role: \"body\"; text: \"x\" }\n    Rectangle { color: Theme.card }\n}" "${_none}")
wjs_case("a pixel size" "PIXEL_SIZE" "Screen.qml"
    "Item { Label { font.pixelSize: 11 } }" "${_none}")
wjs_case("a pixel size as a token is fine" CLEAN "Screen.qml"
    "Item { Label { font.pixelSize: Theme.smallSize } }" "${_none}")
wjs_case("a letter spacing in pixels" "LETTER_SPACING" "Screen.qml"
    "Item { Label { font.letterSpacing: 1.4 } }" "${_none}")
wjs_case("a hex colour" "HEX_COLOUR" "Screen.qml"
    "Item { Rectangle { color: \"#C98A1E\" } }" "${_none}")
wjs_case("a hex colour in the tokens is the point of them" CLEAN "kit/Theme.qml"
    "QtObject { readonly property color gold: \"#BF8019\" }" "${_none}")
wjs_case("a hex colour in the 3D scene is exempt" CLEAN "scene/FaceAtlas.qml"
    "Item { Rectangle { color: \"#F4E9CC\"; Text { font.pixelSize: 12 } } }" "${_none}")
wjs_case("a bare Text" "BARE_TEXT" "Screen.qml"
    "Item { Text { text: \"x\" } }" "${_none}")
wjs_case("the Label itself is a Text" CLEAN "kit/Label.qml"
    "Text { id: root }" "${_none}")
wjs_case("a disabled look by opacity" "DISABLED_OPACITY" "Screen.qml"
    "Item { opacity: enabled ? 1.0 : 0.45 }" "${_none}")
wjs_case("gold as the chosen edge" "GOLD_SELECTION" "Screen.qml"
    "Item { Rectangle { border.color: checked ? Theme.gold : Theme.line } }" "${_none}")
wjs_case("an icon-only button that is not back or close" "ICON_ONLY" "Screen.qml"
    "Item { AppButton { iconName: \"history\" } }" "${_none}")
wjs_case("an icon-only back button" CLEAN "Screen.qml"
    "Item { AppButton { iconName: \"back\"; onClicked: { x(); } } }" "${_none}")
wjs_case("an icon-only close button" CLEAN "Screen.qml"
    "Item { AppButton { iconName: \"close\" } }" "${_none}")
wjs_case("an icon with a label" CLEAN "Screen.qml"
    "Item { AppButton { iconName: \"history\"; label: \"Boards\" } }" "${_none}")
wjs_case("a deleted name" "ParentTheme" "Screen.qml"
    "Item { color: ParentTheme.page }" "${_none}")
wjs_case("a comment is not code" CLEAN "Screen.qml"
    "// font.pixelSize: 12 and \"#FFFFFF\" and Text {\nItem { /* Text { } */ }" "${_none}")

# The allow-list is a ceiling per file.
wjs_case("an allowed offence" CLEAN "Screen.qml"
    "Item { Text { } }" "set(WJS_ALLOW_BARE_TEXT \"Screen.qml=1\")")
wjs_case("one more than allowed" "BARE_TEXT (allowed 1)" "Screen.qml"
    "Item { Text { } Text { } }" "set(WJS_ALLOW_BARE_TEXT \"Screen.qml=1\")")
wjs_case("an allowance for another file" "BARE_TEXT (allowed 0)" "Screen.qml"
    "Item { Text { } }" "set(WJS_ALLOW_BARE_TEXT \"Other.qml=3\")")
wjs_case("below the allowance is noted, not failed" CLEAN "Screen.qml"
    "Item { }" "set(WJS_ALLOW_BARE_TEXT \"Screen.qml=2\")")

# The real tree, with the real allow-list.
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DREPO_ROOT=${REPO_ROOT}" -P "${_checker}"
    RESULT_VARIABLE _status
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
math(EXPR _cases "${_cases} + 1")
if(NOT _status EQUAL 0)
    set(_failures "${_failures}\n  the real tree: expected the check to pass, it said:\n${_output}${_error}")
endif()

if(_failures)
    message(FATAL_ERROR "The QML literal check does not behave:${_failures}")
endif()
message(STATUS "qml literal check: ${_cases} cases behave")
