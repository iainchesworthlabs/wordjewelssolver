# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The Android manifest check held to a table of cases, so that a rule that
# silently stopped matching is a failed test and not a manifest that passes:
# each case is a manifest made from the real one with one thing changed, and the
# check must name that thing (or pass, for a change it is not about).
#
#   cmake -DREPO_ROOT=<repository root> -DWORK_DIR=<scratch directory> \
#         -P tests/platform/test_check_android.cmake
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT OR NOT WORK_DIR)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root> and -DWORK_DIR=<scratch directory>.")
endif()

include("${REPO_ROOT}/tests/platform/android_manifest.cmake")

set(_real "${REPO_ROOT}/libs/platforms/android/packaging/package-source/AndroidManifest.xml")
file(READ "${_real}" _manifest)
string(REPLACE "\r\n" "\n" _manifest "${_manifest}")

set(_failures "")
set(_cases 0)
set(_p "${WJS_ANDROID_PERMISSION_PREFIX}")

# wjs_case(<label> <expected> <old> <new>): the real manifest with <old> replaced by
# <new> (the first occurrence; <old> must be there), checked. <expected> is a phrase
# the findings must contain, or CLEAN when there must be none.
function(wjs_case label expected old new)
    string(FIND "${_manifest}" "${old}" _at)
    if(_at EQUAL -1)
        set(_failures "${_failures}\n  ${label}: the case text '${old}' is not in the real manifest" PARENT_SCOPE)
        return()
    endif()
    string(REPLACE "${old}" "${new}" _changed "${_manifest}")
    file(MAKE_DIRECTORY "${WORK_DIR}")
    file(WRITE "${WORK_DIR}/AndroidManifest.xml" "${_changed}")
    wjs_check_android_manifest("${WORK_DIR}/AndroidManifest.xml" _found)

    math(EXPR _next "${_cases} + 1")
    set(_cases ${_next} PARENT_SCOPE)
    set(_text "")
    list(JOIN _found "\n      " _text)
    if(expected STREQUAL "CLEAN")
        if(_found)
            set(_failures "${_failures}\n  ${label}: expected a pass, found:\n      ${_text}" PARENT_SCOPE)
        endif()
    elseif(NOT _found)
        set(_failures "${_failures}\n  ${label}: expected '${expected}' to be found, the check passed" PARENT_SCOPE)
    elseif(NOT _text MATCHES "${expected}")
        set(_failures "${_failures}\n  ${label}: expected '${expected}' among the findings, got:\n      ${_text}" PARENT_SCOPE)
    endif()
endfunction()

set(_camera "<uses-permission android:name=\"${_p}CAMERA\" />")

wjs_case("the real manifest" CLEAN "<manifest" "<manifest")
wjs_case("a network permission" "INTERNET" "${_camera}"
    "${_camera}\n    <uses-permission android:name=\"${_p}INTERNET\" />")
wjs_case("a network state permission" "ACCESS_NETWORK_STATE" "${_camera}"
    "${_camera}\n    <uses-permission android:name=\"${_p}ACCESS_NETWORK_STATE\" />")
wjs_case("a microphone permission" "${WJS_ANDROID_MICROPHONE_NAME}" "${_camera}"
    "${_camera}\n    <uses-permission android:name=\"${_p}${WJS_ANDROID_MICROPHONE_NAME}\" />")
wjs_case("a storage permission" "WRITE_EXTERNAL_STORAGE" "${_camera}"
    "${_camera}\n    <uses-permission android:name=\"${_p}WRITE_EXTERNAL_STORAGE\" />")
wjs_case("a vibration permission (not asked for until haptics are built)" "VIBRATE" "${_camera}"
    "${_camera}\n    <uses-permission android:name=\"${_p}VIBRATE\" />")
wjs_case("a permission removed by an edit rule" "INTERNET" "${_camera}"
    "${_camera}\n    <uses-permission android:name=\"${_p}INTERNET\" xmlns:tools=\"http://schemas.android.com/tools\" tools:node=\"remove\" />")
wjs_case("no camera permission" "does not ask for the camera" "${_camera}" "")
wjs_case("Qt's permissions placeholder" "placeholder" "<!-- %%INSERT_FEATURES -->"
    "<!-- %%INSERT_PERMISSIONS -->\n    <!-- %%INSERT_FEATURES -->")
wjs_case("a camera that is required" "android.hardware.camera is not declared as an optional"
    "<uses-feature android:name=\"android.hardware.camera\" android:required=\"false\" />"
    "<uses-feature android:name=\"android.hardware.camera\" android:required=\"true\" />")
wjs_case("an accelerometer that is required" "accelerometer is not declared as an optional"
    "android:name=\"android.hardware.sensor.accelerometer\" android:required=\"false\""
    "android:name=\"android.hardware.sensor.accelerometer\" android:required=\"true\"")
wjs_case("backup on" "allowBackup" "android:allowBackup=\"false\"" "android:allowBackup=\"true\"")
wjs_case("a second exported component" "components are exported" "android:exported=\"false\""
    "android:exported=\"true\"")
# What prose may say: a permission named in a comment is not asked for.
wjs_case("a permission named in a comment" CLEAN "<uses-feature android:name=\"android.hardware.touchscreen\""
    "<!-- ${_p}INTERNET is not asked for -->\n    <uses-feature android:name=\"android.hardware.touchscreen\"")

if(_failures)
    message(FATAL_ERROR "The Android manifest check is not what it should be:${_failures}")
endif()
message(STATUS "android manifest check: ${_cases} cases, each found what it is for")
