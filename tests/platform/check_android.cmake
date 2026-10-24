# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# The Android target, held together: the manifest asks for the camera and nothing
# else, the presets that build it exist with the settings that make them Android
# builds, the CI job and the release job install the kits the pins file names, and
# the release job reads only the four Android signing secrets.
#
#   cmake -DREPO_ROOT=<repository root> -P tests/platform/check_android.cmake
#
# Everything here is read from files, so it runs on every host and in every CI leg
# (the Android build itself runs only where an NDK and a Qt Android kit are:
# .github/workflows/android-build.yml). What it cannot say is whether the package
# installs or runs: that needs a device (docs/ci.md, "What Android has and has not
# shown").
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/android_manifest.cmake")

set(_failures "")
function(_fail text)
    set(_failures "${_failures}\n  ${text}" PARENT_SCOPE)
endfunction()

function(_read file result)
    if(NOT EXISTS "${REPO_ROOT}/${file}")
        _fail("${file} does not exist")
        set(${result} "" PARENT_SCOPE)
        return()
    endif()
    file(READ "${REPO_ROOT}/${file}" _content)
    string(REPLACE "\r\n" "\n" _content "${_content}")
    set(${result} "${_content}" PARENT_SCOPE)
endfunction()

# ---- the files ----------------------------------------------------------------

set(_dir "libs/platforms/android")
foreach(_file IN ITEMS
        "${_dir}/platform.cmake"
        "${_dir}/paths_android.cpp"
        "${_dir}/fatal_error_android.cpp"
        "${_dir}/packaging/verify_apk.sh"
        "${_dir}/packaging/package-source/AndroidManifest.xml"
        "${_dir}/packaging/package-source/res/mipmap-anydpi-v26/ic_launcher.xml"
        "${_dir}/packaging/package-source/res/mipmap-xxxhdpi/ic_launcher_foreground.png"
        "${_dir}/packaging/package-source/res/values/styles.xml"
        "tools/release/new_android_keystore.sh"
        ".github/workflows/android-build.yml")
    if(NOT EXISTS "${REPO_ROOT}/${_file}")
        _fail("${_file} is missing")
    endif()
endforeach()

# ---- the manifest ---------------------------------------------------------------

wjs_check_android_manifest("${REPO_ROOT}/${_dir}/packaging/package-source/AndroidManifest.xml" _manifest_failures)
foreach(_problem IN LISTS _manifest_failures)
    _fail("AndroidManifest.xml: ${_problem}")
endforeach()

# ---- the fragment: the platform levels ---------------------------------------------

_read("${_dir}/platform.cmake" _fragment)
if(_fragment MATCHES "set[(]WJS_ANDROID_MIN_SDK ([0-9]+)[)]")
    if(CMAKE_MATCH_1 LESS 28)
        _fail("platform.cmake sets the minimum SDK to ${CMAKE_MATCH_1}; Qt 6.12 supports 28 and later")
    endif()
else()
    _fail("platform.cmake does not set WJS_ANDROID_MIN_SDK")
endif()
if(_fragment MATCHES "set[(]WJS_ANDROID_TARGET_SDK ([0-9]+)[)]")
    if(CMAKE_MATCH_1 LESS 35)
        _fail("platform.cmake sets the target SDK to ${CMAKE_MATCH_1}; Google Play wants 35 or later")
    endif()
else()
    _fail("platform.cmake does not set WJS_ANDROID_TARGET_SDK")
endif()
# Tracy listens on the network; nothing in the fragment turns it on.
if(_fragment MATCHES "WJS_ENABLE_TRACY")
    _fail("platform.cmake mentions WJS_ENABLE_TRACY; no Android build has Tracy (its client listens on the network)")
endif()

# ---- the presets ------------------------------------------------------------------

_read("CMakePresets.json" _presets)
string(JSON _count LENGTH "${_presets}" configurePresets)
math(EXPR _last "${_count} - 1")
foreach(_i RANGE 0 ${_last})
    string(JSON _name GET "${_presets}" configurePresets ${_i} name)
    set("_INDEX_${_name}" ${_i})
    if(_name MATCHES "^android" AND _name MATCHES "-debug$")
        _fail("the preset '${_name}' is named *-debug, which tests/profiling/check_presets.cmake reads as 'Tracy on'; no Android build has Tracy")
    endif()
endforeach()

function(_cache_value preset variable result)
    set(${result} "<unset>" PARENT_SCOPE)
    if(NOT DEFINED _INDEX_${preset})
        return()
    endif()
    string(JSON _type ERROR_VARIABLE _error TYPE "${_presets}" configurePresets ${_INDEX_${preset}}
        cacheVariables ${variable})
    if(_error STREQUAL "NOTFOUND")
        string(JSON _value GET "${_presets}" configurePresets ${_INDEX_${preset}} cacheVariables ${variable})
        set(${result} "${_value}" PARENT_SCOPE)
    endif()
endfunction()

foreach(_preset IN ITEMS android-arm64-base android-arm64-release android-arm64-debuggable)
    if(NOT DEFINED _INDEX_${_preset})
        _fail("CMakePresets.json has no configure preset '${_preset}'")
    endif()
endforeach()

if(DEFINED _INDEX_android-arm64-base)
    string(JSON _hidden ERROR_VARIABLE _error GET "${_presets}" configurePresets ${_INDEX_android-arm64-base} hidden)
    if(NOT _hidden STREQUAL "ON" AND NOT _hidden STREQUAL "true")
        _fail("android-arm64-base is not hidden")
    endif()
    foreach(_pair IN ITEMS
            "VCPKG_TARGET_TRIPLET=arm64-android"
            "WJS_BUILD_APP=ON"
            "WJS_BUILD_CLI=OFF"
            "WJS_BUILD_TESTS=OFF"
            "WJS_ENABLE_TRACY=OFF"
            "WJS_ENABLE_SANITIZERS=OFF")
        string(FIND "${_pair}" "=" _equals)
        string(SUBSTRING "${_pair}" 0 ${_equals} _key)
        math(EXPR _start "${_equals} + 1")
        string(SUBSTRING "${_pair}" ${_start} -1 _expected)
        _cache_value(android-arm64-base "${_key}" _value)
        if(NOT _value STREQUAL _expected)
            _fail("android-arm64-base sets ${_key} to '${_value}', not '${_expected}'")
        endif()
    endforeach()
    _cache_value(android-arm64-base VCPKG_CHAINLOAD_TOOLCHAIN_FILE _chain)
    if(NOT _chain MATCHES "lib/cmake/Qt6/qt[.]toolchain[.]cmake$" OR NOT _chain MATCHES "QT_ANDROID_DIR")
        _fail("android-arm64-base chain-loads '${_chain}'; it must be QT_ANDROID_DIR's lib/cmake/Qt6/qt.toolchain.cmake")
    endif()
    foreach(_variable IN ITEMS QT_HOST_PATH ANDROID_SDK_ROOT ANDROID_NDK_ROOT)
        _cache_value(android-arm64-base ${_variable} _value)
        # The preset's own macro syntax, which CMake's parser refuses to see written out.
        string(CONCAT _from_environment "$" "env{" "${_variable}" "}")
        if(NOT _value STREQUAL _from_environment)
            _fail("android-arm64-base does not read ${_variable} from the environment (it is '${_value}')")
        endif()
    endforeach()
endif()

_cache_value(android-arm64-debuggable CMAKE_BUILD_TYPE _build_type)
if(NOT _build_type STREQUAL "Debug")
    _fail("android-arm64-debuggable is not a Debug build (CMAKE_BUILD_TYPE is '${_build_type}')")
endif()
_cache_value(android-arm64-debuggable QT_ANDROID_DEPLOYMENT_TYPE _deployment)
if(NOT _deployment STREQUAL "Debug")
    _fail("android-arm64-debuggable does not ask androiddeployqt for a debug package (QT_ANDROID_DEPLOYMENT_TYPE is '${_deployment}')")
endif()

# The release preset inherits the shared release settings (no Tracy, reviewed
# phonics, Release) and the Android base, and a signed package is not the default.
if(DEFINED _INDEX_android-arm64-release)
    string(JSON _inherits GET "${_presets}" configurePresets ${_INDEX_android-arm64-release} inherits)
    foreach(_parent IN ITEMS android-arm64-base release)
        if(NOT _inherits MATCHES "${_parent}")
            _fail("android-arm64-release does not inherit '${_parent}'")
        endif()
    endforeach()
    _cache_value(android-arm64-release QT_ANDROID_SIGN_APK _sign)
    if(NOT _sign STREQUAL "<unset>")
        _fail("android-arm64-release sets QT_ANDROID_SIGN_APK; signing is asked for on the configure line by the release job, with the keystore beside it")
    endif()
endif()

# Build presets that make the package.
string(JSON _build_count LENGTH "${_presets}" buildPresets)
math(EXPR _build_last "${_build_count} - 1")
set(_build_presets "")
foreach(_i RANGE 0 ${_build_last})
    string(JSON _name GET "${_presets}" buildPresets ${_i} name)
    string(JSON _targets ERROR_VARIABLE _error GET "${_presets}" buildPresets ${_i} targets)
    if(_error STREQUAL "NOTFOUND")
        set("_BUILD_TARGETS_${_name}" "${_targets}")
    endif()
    list(APPEND _build_presets "${_name}")
endforeach()
foreach(_preset IN ITEMS android-arm64-release android-arm64-debuggable)
    if(NOT _preset IN_LIST _build_presets)
        _fail("CMakePresets.json has no build preset '${_preset}'")
    elseif(NOT "${_BUILD_TARGETS_${_preset}}" MATCHES "apk")
        _fail("the build preset '${_preset}' does not build the apk target")
    endif()
endforeach()

# ---- the pins ------------------------------------------------------------------------

_read(".github/toolchain-versions.json" _versions)
string(JSON _qt_version GET "${_versions}" qt version)
string(JSON _qt_modules GET "${_versions}" qt modules)
string(JSON _aqtsource GET "${_versions}" qt aqtsource)
string(JSON _kit_host GET "${_versions}" qt kits android host)
string(JSON _kit_arch GET "${_versions}" qt kits android arch)
string(JSON _kit_dir GET "${_versions}" qt kits android dir)
string(JSON _kit_hostdir GET "${_versions}" qt kits android hostdir)
string(JSON _ndk GET "${_versions}" android ndk)
string(JSON _platform GET "${_versions}" android platform)
string(JSON _build_tools GET "${_versions}" android build_tools)
string(JSON _jdk GET "${_versions}" android jdk)

_read(".github/workflows/android-build.yml" _build_workflow)
_read(".github/workflows/release.yml" _release_workflow)

# The release workflow's Android job: from its name to the next job's comment.
string(FIND "${_release_workflow}" "\n  android:\n" _job_start)
string(FIND "${_release_workflow}" "\n  sign:\n" _job_end)
set(_release_job "")
if(_job_start EQUAL -1 OR _job_end EQUAL -1 OR _job_end LESS _job_start)
    _fail("release.yml has no android job before the sign job")
else()
    math(EXPR _job_length "${_job_end} - ${_job_start}")
    string(SUBSTRING "${_release_workflow}" ${_job_start} ${_job_length} _release_job)
endif()

foreach(_pair IN ITEMS "android-build.yml|_build_workflow" "release.yml (the android job)|_release_job")
    string(REPLACE "|" ";" _parts "${_pair}")
    list(GET _parts 0 _label)
    list(GET _parts 1 _variable)
    set(_text "${${_variable}}")
    if("${_text}" STREQUAL "")
        continue()
    endif()
    foreach(_needle IN ITEMS
            "ANDROID_NDK_VERSION: \"${_ndk}\""
            "ANDROID_PLATFORM: \"${_platform}\""
            "ANDROID_BUILD_TOOLS: \"${_build_tools}\""
            "version: \"${_qt_version}\""
            "host: ${_kit_host}"
            "target: android"
            "arch: ${_kit_arch}"
            "modules: \"${_qt_modules}\""
            "aqtsource: \"${_aqtsource}\""
            "Qt/${_qt_version}/${_kit_dir}"
            "Qt/${_qt_version}/${_kit_hostdir}"
            "JAVA_HOME_17_X64")
        string(FIND "${_text}" "${_needle}" _at)
        if(_at EQUAL -1)
            _fail("${_label} does not contain '${_needle}'; toolchain-versions.json (android, qt.kits.android) says it must")
        endif()
    endforeach()
    if(NOT "${_text}" MATCHES "uses: jurplel/install-qt-action@[0-9a-f]+")
        _fail("${_label} does not install Qt with a pinned jurplel/install-qt-action")
    endif()
endforeach()
if(NOT _jdk STREQUAL "17")
    _fail("toolchain-versions.json says the JDK is ${_jdk}; the workflows use the runner's JDK 17 (JAVA_HOME_17_X64): change both together")
endif()

# ---- the CI job ---------------------------------------------------------------------------

if(NOT "${_build_workflow}" STREQUAL "")
    foreach(_needle IN ITEMS
            "cmake --preset android-arm64-debuggable"
            "cmake --build --preset android-arm64-debuggable"
            "verify_apk.sh"
            "actions/upload-artifact@"
            "permissions: {}")
        string(FIND "${_build_workflow}" "${_needle}" _at)
        if(_at EQUAL -1)
            _fail("android-build.yml does not contain '${_needle}'")
        endif()
    endforeach()
    if(_build_workflow MATCHES "secrets[.]")
        _fail("android-build.yml reads a secret; the debug-signed build needs none (the signed package is release.yml's)")
    endif()
    if(_build_workflow MATCHES "pull_request_target")
        _fail("android-build.yml runs on pull_request_target; a build of a pull request's code must not have secrets")
    endif()
endif()

# ---- the release job -----------------------------------------------------------------------

if(NOT "${_release_job}" STREQUAL "")
    # The four secrets, and no other.
    set(_expected_secrets
        ANDROID_KEYSTORE_BASE64 ANDROID_KEYSTORE_PASSWORD ANDROID_KEY_ALIAS ANDROID_KEY_PASSWORD)
    string(REGEX MATCHALL "secrets[.][A-Za-z0-9_]+" _used "${_release_job}")
    list(REMOVE_DUPLICATES _used)
    set(_used_names "")
    foreach(_use IN LISTS _used)
        string(REPLACE "secrets." "" _secret "${_use}")
        list(APPEND _used_names "${_secret}")
        if(NOT _secret IN_LIST _expected_secrets)
            _fail("the android job of release.yml reads secrets.${_secret}; it may read only ${_expected_secrets}")
        endif()
    endforeach()
    foreach(_secret IN LISTS _expected_secrets)
        if(NOT _secret IN_LIST _used_names)
            _fail("the android job of release.yml does not read secrets.${_secret}")
        endif()
    endforeach()

    foreach(_needle IN ITEMS
            "steps.signing.outputs.have == 'true'"
            "cmake --preset android-arm64-release -DQT_ANDROID_SIGN_APK=ON -DQT_ANDROID_SIGN_AAB=ON"
            "--target aab"
            "verify_apk.sh"
            "--release --signed"
            "name: package-android"
            "rm -f \"$RUNNER_TEMP/wjs-upload.p12\"")
        string(FIND "${_release_job}" "${_needle}" _at)
        if(_at EQUAL -1)
            _fail("the android job of release.yml does not contain '${_needle}'")
        endif()
    endforeach()

    # The package family the signing job collects, and its dependence on this job.
    if(NOT _release_workflow MATCHES "pattern: package-[*]")
        _fail("release.yml's signing job does not download package-*")
    endif()
    if(NOT _release_workflow MATCHES "needs: [[]prepare, build, ios, android[]]")
        _fail("release.yml's sign job does not wait for the android job")
    endif()
    if(NOT _release_workflow MATCHES "contains[(]fromJSON[(]'[[]\"success\",\"skipped\"[]]'[)], needs[.]android[.]result[)]")
        _fail("release.yml's sign job does not accept a skipped android job")
    endif()
    # A skipped or secret-less run must not fail the release: the job's own check is
    # what says so, and it exits non-zero only for a partial set.
    if(NOT _release_job MATCHES "\"[$]present\" -eq 0")
        _fail("the android job does not treat 'no Android secrets at all' as 'no Android package'")
    endif()
    # The keystore is in a file in the runner's temp directory and nowhere else.
    if(_release_job MATCHES "(echo|printf)[^\n]*[$][{]?(QT_)?ANDROID_KEY(STORE_PASSWORD|_PASSWORD|STORE_STORE_PASS|STORE_KEY_PASS)")
        _fail("the android job of release.yml prints a signing secret's value")
    endif()
endif()

# The other workflows that use the release secrets stay as they were: this job's
# secrets are the Android ones only.
foreach(_workflow IN ITEMS ci analysis codeql)
    if(EXISTS "${REPO_ROOT}/.github/workflows/${_workflow}.yml")
        file(READ "${REPO_ROOT}/.github/workflows/${_workflow}.yml" _other)
        if(_other MATCHES "secrets[.]ANDROID_")
            _fail("${_workflow}.yml reads an Android signing secret; only release.yml may")
        endif()
    endif()
endforeach()

# ---- the keystore script ----------------------------------------------------------------------

_read("tools/release/new_android_keystore.sh" _script)
foreach(_secret IN ITEMS ANDROID_KEYSTORE_BASE64 ANDROID_KEYSTORE_PASSWORD ANDROID_KEY_ALIAS ANDROID_KEY_PASSWORD)
    if(NOT _script MATCHES "gh secret set ${_secret} ")
        _fail("new_android_keystore.sh does not print the 'gh secret set ${_secret}' command")
    endif()
endforeach()
if(_script MATCHES "gh secret set [^\n]*--body" OR _script MATCHES "\n[ ]*gh secret set [^\n]*<<")
    _fail("new_android_keystore.sh puts a secret on a command line; the commands read the files")
endif()
if(NOT _script MATCHES "inside a git working tree")
    _fail("new_android_keystore.sh no longer refuses an output folder inside a working tree")
endif()

# ---- result ---------------------------------------------------------------------------------------

if(_failures)
    message(FATAL_ERROR "The Android target does not hold together:${_failures}")
endif()
message(STATUS "android: manifest (the camera alone), presets, pins, CI job, release job and keystore script agree")
