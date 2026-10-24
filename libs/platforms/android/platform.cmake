# SPDX-License-Identifier: GPL-3.0-or-later
# Android, for a device (arm64-v8a). Included by cmake/Platform.cmake; the contract
# it fills in is described at the top of that file.
#
# The application is a Qt Quick program packaged by Qt's own tooling: qt_add_executable
# makes a shared library on Android and generates the deployment settings, and
# androiddeployqt (the `apk` and `aab` targets) turns it, the Qt libraries it needs
# and the manifest in packaging/ into a package. There is no CPack configuration and
# no installer; CI builds a debug-signed APK, and the release workflow signs one with
# the author's upload key when the secrets are there (docs/ci.md, "Android").
#
# Needs the Qt for Android kit (QT_ANDROID_DIR / the `android-arm64-*` presets), a
# desktop Qt of the same version for the host tools (QT_HOST_PATH), the Android SDK
# (ANDROID_SDK_ROOT) and the NDK the kit was built with (ANDROID_NDK_ROOT), and a JDK 17
# or later for Gradle (docs/building.md, "Android"). Everything that touches a device
# (the camera, the accelerometer, the speech engine, sound) is built here and has
# not been run on one: docs/ci.md, "What Android has and has not shown".

set(WJS_PLATFORM_DISPLAY_NAME "Android")

# No installers and no command-line program: the package is made by androiddeployqt.
set(WJS_PLATFORM_DESKTOP OFF)

# An Android build is never `cmake --install`ed, and the program does not look for
# its data beside an executable the way a desktop one does: paths_android.cpp unpacks
# the dictionary and the dice into the application's private storage and says that is
# where its executable is. This is the name an install rule would use.
set(WJS_PLATFORM_INSTALL_APP_DATA_DIR "assets")

# Qt's runtime is bundled into the package by androiddeployqt; Qt's install-time
# deploy script refuses the platform.
set(WJS_PLATFORM_QT_DEPLOY_AT_INSTALL OFF)

# Qt for Android when no QT_ROOT_DIR names it (the aqtinstall layout: Qt/6.12.0/android_arm64_v8a).
set(WJS_PLATFORM_QT_SEARCH_ROOTS "$ENV{HOME}/Qt" "/opt/Qt")
set(WJS_PLATFORM_QT_ARCH_DIRS android_arm64_v8a)

# No test runs on the device or offscreen here.
set(WJS_PLATFORM_QT_HEADLESS_ENVIRONMENT "")

# A tablet has no desktop Qt to build against: the application is the whole build.
# (Said here, in the one place that knows, rather than as a Qt6::Core that cannot be
# resolved three directories away.)
string(TOUPPER "${WJS_BUILD_APP}" _wjs_android_build_app)
if(_wjs_android_build_app MATCHES "^(OFF|FALSE|NO|N|0)$")
    message(FATAL_ERROR
        "An Android build is the Qt application: WJS_BUILD_APP=${WJS_BUILD_APP} leaves "
        "nothing to package. Use the android-arm64-release preset (docs/building.md, "
        "\"Android\").")
endif()
unset(_wjs_android_build_app)

# detail/ is what no platform differs in: no native camera backend (Mode A reads the
# camera through Qt Multimedia, as the desktops and iOS do), the portable board reader
# (docs/design/scanner.md), no haptics (below), and no window chrome. The paths and the
# fatal error are Android's own; the console is the POSIX one (nothing to switch on).
#
# Haptics: the engine is the Vibrator service, which Qt has no API for; reaching it
# means JNI through QJniObject and the VIBRATE permission, which this manifest does not
# ask for (the camera is the only permission it holds, docs/product-requirements.md).
# So vibrate() answers false, as it does on the desktops, and the game carries on with
# its sound and its picture. The accelerometer shake is Qt Sensors, which the
# application's motion variant already uses where the module is in the kit.
set(WJS_PLATFORM_SOURCES
    android/fatal_error_android.cpp
    android/paths_android.cpp
    detail/board_reader_portable.cpp
    detail/camera_unavailable.cpp
    detail/haptics_none.cpp
    detail/window_chrome_none.cpp
    posix/console_posix.cpp)

# paths_android.cpp reads the unpacked data out of Qt's resource system and finds the
# application's private directory through QStandardPaths; fatal_error_android.cpp
# writes to logcat (liblog, a part of the NDK's sysroot).
set(WJS_PLATFORM_LINK_LIBRARIES Qt6::Core log)
set(WJS_PLATFORM_COMPILE_DEFINITIONS "")

# Where the package source (the manifest and the resources) and the package check are
# (captured here: inside a function CMAKE_CURRENT_LIST_DIR is the caller's).
set(WJS_ANDROID_PACKAGING_DIR "${CMAKE_CURRENT_LIST_DIR}/packaging")

# The application id: the same reverse-domain name as the iOS bundle identifier.
set(WJS_ANDROID_APPLICATION_ID "org.iainchesworth.wordjewelssolver")

# The platform levels. 28 is the lowest Qt 6.12 supports (its toolchain file's
# default). The target and compile level are the newest the Play Console takes: a level
# the SDK in use does not have fails androiddeployqt by name.
set(WJS_ANDROID_MIN_SDK 28)
set(WJS_ANDROID_TARGET_SDK 36)

# Play wants an integer that only grows. Derived from the version so that a tag is
# enough to make one: 0.1.0 is 10000, 0.2.3 is 20300, 1.0.0 is 1000000. A build that
# must go out under a number of its own (a second upload of one version) passes
# -DWJS_ANDROID_VERSION_CODE=...
set(WJS_ANDROID_VERSION_CODE "" CACHE STRING
    "versionCode of the package (empty: derived from the project version)")

# The data the program reads beside its executable on a desktop is compiled into the
# application's resources here and unpacked on first start (paths_android.cpp, which
# names this prefix). A resource prefix, not an Android asset, because the dictionary
# loader opens a file by path and an APK's assets are not files.
set(WJS_ANDROID_DATA_RESOURCE_PREFIX "/wjs-android-data")

# What makes the application target a program of this device: the package
# properties androiddeployqt writes into the deployment settings, the manifest and
# icons Qt packs from QT_ANDROID_PACKAGE_SOURCE_DIR, and the data the program unpacks.
function(wjs_platform_configure_app target)
    if(WJS_ANDROID_VERSION_CODE)
        set(_code "${WJS_ANDROID_VERSION_CODE}")
    else()
        math(EXPR _code
            "${PROJECT_VERSION_MAJOR} * 1000000 + ${PROJECT_VERSION_MINOR} * 10000 + ${PROJECT_VERSION_PATCH} * 100")
    endif()

    # A package source directory is copied over Qt's own template by androiddeployqt:
    # it holds AndroidManifest.xml (what the program asks of the system) and res/
    # (the launcher icon, the launch colour).
    set_target_properties(${target} PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${WJS_ANDROID_PACKAGING_DIR}/package-source"
        QT_ANDROID_PACKAGE_NAME "${WJS_ANDROID_APPLICATION_ID}"
        QT_ANDROID_APP_NAME "Word Jewels Solver"
        QT_ANDROID_APP_ICON "@mipmap/ic_launcher"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE "${_code}"
        QT_ANDROID_MIN_SDK_VERSION "${WJS_ANDROID_MIN_SDK}"
        QT_ANDROID_TARGET_SDK_VERSION "${WJS_ANDROID_TARGET_SDK}"
        QT_ANDROID_COMPILE_SDK_VERSION "${WJS_ANDROID_TARGET_SDK}")

    # The dictionary and the dice, as one resource tree. Listed here, once, with the
    # files apps/app/CMakeLists.txt copies beside a desktop executable.
    set(_data "${PROJECT_SOURCE_DIR}/data")
    qt_add_resources(${target} wjs_android_data
        PREFIX "${WJS_ANDROID_DATA_RESOURCE_PREFIX}"
        BASE "${_data}"
        FILES
            "${_data}/dictionaries/en.txt"
            "${_data}/dice/3x3.txt"
            "${_data}/dice/4x4.txt"
            "${_data}/dice/5x5.txt"
            "${_data}/dice/6x6.txt")
endfunction()

# androiddeployqt bundles Qt's runtime, plugins and QML modules into the package from
# the QML import scan of the target's QML root; the extra directories (Mode A's live
# view is a file of the camera variant) are scanned too.
function(wjs_platform_deploy_qt_runtime target qml_dir)
    set(_roots ${qml_dir} ${ARGN})
    set_target_properties(${target} PROPERTIES QT_QML_ROOT_PATH "${_roots}")
endfunction()

# No CPack generator: the package is androiddeployqt's.
function(wjs_platform_configure_packaging)
endfunction()
