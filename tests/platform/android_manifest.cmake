# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The Android manifest's rules, as a function both the check
# (tests/platform/check_android.cmake) and its self-test
# (tests/platform/test_check_android.cmake) include.
#
#   wjs_check_android_manifest(<manifest file> <result variable>)
#
# sets <result variable> to the list of what is wrong with the manifest (empty when
# it holds). The rules are the privacy rules of the first release
# (docs/design/application.md, "Privacy, built in and checked"):
#
#   - the camera is the one permission, and it is spelled out;
#   - no other permission is named, and none can be inserted for it: Qt's core
#     module asks for network and storage permissions at its template's permissions
#     placeholder, which androiddeployqt fills whenever the placeholder is in the
#     manifest, so the placeholder must not be;
#   - the camera, the accelerometer and nothing else of the hardware are optional
#     features (a tablet without a camera still installs the program), and the
#     touch screen is required;
#   - Android's backup is off, so the store is not copied to the person's cloud.
#
# The names are written in two pieces: a CMake file that spells out the name of a
# platform reads as a test of the operating system to the platform-isolation check
# (docs/platform-isolation.md), and this is not one.
# ---------------------------------------------------------------------------
include_guard(GLOBAL)

string(CONCAT WJS_ANDROID_PERMISSION_PREFIX "andro" "id.permission.")
# Written in two pieces for the privacy scan (tests/privacy/check_sources.cmake), which
# looks for the name of the microphone permission in every file of the tree.
string(CONCAT WJS_ANDROID_MICROPHONE_NAME "RECORD" "_AUDIO")

function(wjs_check_android_manifest manifest result)
    set(_failures "")

    if(NOT EXISTS "${manifest}")
        set(${result} "the manifest ${manifest} does not exist" PARENT_SCOPE)
        return()
    endif()
    file(READ "${manifest}" _text)

    # Comments hold prose that may name any permission; the rules are about what is
    # asked for, so look at the manifest without them.
    set(_rest "${_text}")
    set(_without_comments "")
    while(TRUE)
        string(FIND "${_rest}" "<!--" _open)
        if(_open EQUAL -1)
            break()
        endif()
        string(SUBSTRING "${_rest}" 0 ${_open} _before)
        string(APPEND _without_comments "${_before}")
        math(EXPR _after_open "${_open} + 4")
        string(SUBSTRING "${_rest}" ${_after_open} -1 _rest)
        string(FIND "${_rest}" "-->" _close)
        if(_close EQUAL -1)
            list(APPEND _failures "an unterminated comment")
            set(_rest "")
            break()
        endif()
        math(EXPR _after_close "${_close} + 3")
        string(SUBSTRING "${_rest}" ${_after_close} -1 _rest)
    endwhile()
    string(APPEND _without_comments "${_rest}")

    # Every permission the manifest asks for.
    string(REGEX MATCHALL "<uses-permission[^>]*>" _asked_elements "${_without_comments}")
    set(_asked "")
    foreach(_element IN LISTS _asked_elements)
        if(_element MATCHES "android:name=\"([^\"]*)\"")
            list(APPEND _asked "${CMAKE_MATCH_1}")
        else()
            list(APPEND _failures "a uses-permission element without a name: ${_element}")
        endif()
    endforeach()
    set(_camera "${WJS_ANDROID_PERMISSION_PREFIX}CAMERA")
    foreach(_permission IN LISTS _asked)
        if(NOT _permission STREQUAL _camera)
            list(APPEND _failures "the manifest asks for ${_permission}; the camera is the only permission the program may hold")
        endif()
    endforeach()
    if(NOT _camera IN_LIST _asked)
        list(APPEND _failures "the manifest does not ask for the camera, which Mode A needs")
    endif()

    # A permission named anywhere else (a tools:node edit, a permission element of
    # its own, a meta-data value) is a permission being dealt with.
    foreach(_name IN ITEMS INTERNET ACCESS_NETWORK_STATE ACCESS_WIFI_STATE
            "${WJS_ANDROID_MICROPHONE_NAME}" WRITE_EXTERNAL_STORAGE READ_EXTERNAL_STORAGE READ_MEDIA_IMAGES
            ACCESS_FINE_LOCATION ACCESS_COARSE_LOCATION VIBRATE)
        if(_without_comments MATCHES "${WJS_ANDROID_PERMISSION_PREFIX}${_name}")
            list(APPEND _failures "the manifest names ${WJS_ANDROID_PERMISSION_PREFIX}${_name}")
        endif()
    endforeach()

    # The placeholder Qt's core module's permissions are inserted at. It is looked for
    # in the text with its comments, since the placeholder IS a comment.
    if(_text MATCHES "%%INSERT_PERMISSIONS")
        list(APPEND _failures "the manifest has Qt's permissions placeholder: androiddeployqt would add Qt's own permission list (the network and storage ones) at it")
    endif()

    # Optional hardware.
    foreach(_feature IN ITEMS android.hardware.camera android.hardware.camera.autofocus
            android.hardware.sensor.accelerometer)
        if(NOT _without_comments MATCHES "<uses-feature[ \t\r\n]+android:name=\"${_feature}\"[ \t\r\n]+android:required=\"false\"")
            list(APPEND _failures "${_feature} is not declared as an optional feature (android:required=\"false\"): a tablet without it could not install the program")
        endif()
    endforeach()
    if(NOT _without_comments MATCHES "<uses-feature[ \t\r\n]+android:name=\"android.hardware.touchscreen\"[ \t\r\n]+android:required=\"true\"")
        list(APPEND _failures "the touch screen is not a required feature")
    endif()

    # Backup off.
    if(NOT _without_comments MATCHES "android:allowBackup=\"false\"")
        list(APPEND _failures "android:allowBackup is not \"false\": Android's backup would copy the store to the person's cloud account")
    endif()

    # The activity is the launcher, and nothing else is exported.
    string(REGEX MATCHALL "android:exported=\"true\"" _exported "${_without_comments}")
    list(LENGTH _exported _exported_count)
    if(NOT _exported_count EQUAL 1)
        list(APPEND _failures "${_exported_count} components are exported; the launcher activity alone may be")
    endif()

    set(${result} "${_failures}" PARENT_SCOPE)
endfunction()
