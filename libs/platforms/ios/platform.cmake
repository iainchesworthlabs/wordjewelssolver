# SPDX-License-Identifier: GPL-3.0-or-later
# iOS and iPadOS, for a device (arm64). Included by cmake/Platform.cmake; the
# contract it fills in is described at the top of that file.
#
# This is a private build for the author's own devices, installed ad hoc
# (docs/ios-install.md, docs/ci.md "iOS"), not a store build. The configure
# is the `ios-release` preset, which needs a Mac with Xcode: Qt's iOS kit (a
# static Qt) and a desktop Qt of the same version for the host tools
# (QT_HOST_PATH). Nothing here has run on a Mac yet; the first hosted run is
# the check (docs/ci.md, "What iOS has and has not shown").

set(WJS_PLATFORM_DISPLAY_NAME "iOS")

# A phone and a tablet have no installers and no command-line program, and the
# application is built by Xcode, not packaged by CPack: the ad-hoc IPA comes
# from `xcodebuild archive` and `-exportArchive` (release.yml).
set(WJS_PLATFORM_DESKTOP OFF)

# An iOS bundle is flat: the executable, Info.plist and the data are side by
# side in WordJewelsSolver.app, and the application looks for its dictionary and
# dice beside its own executable. (An iOS build is never `cmake --install`ed;
# this is where an install rule would put them.)
set(WJS_PLATFORM_INSTALL_APP_DATA_DIR "WordJewelsSolver.app")

# Qt is static on iOS, linked into the executable, and Xcode embeds what is
# needed: there is no deploy script to run (and Qt's would refuse the platform).
set(WJS_PLATFORM_QT_DEPLOY_AT_INSTALL OFF)

# Qt's iOS kit, when no QT_ROOT_DIR names it (the aqtinstall layout; only on a Mac).
set(WJS_PLATFORM_QT_SEARCH_ROOTS "$ENV{HOME}/Qt" "/opt/Qt")
set(WJS_PLATFORM_QT_ARCH_DIRS ios)

# No test runs on the device or offscreen here.
set(WJS_PLATFORM_QT_HEADLESS_ENVIRONMENT "")

# detail/ is what no platform differs in: no native camera backend yet, the portable
# board reader (docs/design/scanner.md), and the no-op haptics the desktops answer
# with. The iPad's own camera and Vision reader (camera_ios.mm, board_reader_ios.mm)
# and UIImpactFeedbackGenerator haptics replace them here when they are written
# (docs/roadmap.md); until then Mode A reads the camera through Qt Multimedia, as
# the desktops do, and nothing buzzes.
set(WJS_PLATFORM_SOURCES
    detail/board_reader_portable.cpp
    detail/camera_unavailable.cpp
    detail/fatal_error_stderr.cpp
    detail/haptics_none.cpp
    detail/window_chrome_none.cpp
    ios/paths_ios.cpp
    posix/console_posix.cpp)

set(WJS_PLATFORM_LINK_LIBRARIES "")
set(WJS_PLATFORM_COMPILE_DEFINITIONS "")

# Where the bundle's Info.plist template and asset catalogue are (captured here:
# inside a function CMAKE_CURRENT_LIST_DIR is the caller's).
set(WJS_IOS_PACKAGING_DIR "${CMAKE_CURRENT_LIST_DIR}/packaging")

# Code signing, for the signed ad-hoc build only. Both are empty for the unsigned
# compile check, which needs no Apple account, and both are given together on the
# configure line by release.yml (APPLE_TEAM_ID and APPLE_PROVISIONING_PROFILE_NAME;
# the profile specifier is the profile's NAME in the Apple portal, not its file).
# They sign the application target alone: signing every Xcode target, as a
# project-wide setting does, makes Xcode demand a bundle identifier of the static
# libraries and Qt's resource targets, which have none.
set(WJS_IOS_SIGNING_TEAM "" CACHE STRING
    "Apple team ID the application is signed for (empty: an unsigned build)")
set(WJS_IOS_SIGNING_PROFILE "" CACHE STRING
    "Name of the ad-hoc provisioning profile the application is signed with (empty: an unsigned build)")

# The kit's plugin targets that carry no QT_DEFAULT_PLUGIN property: Qt's plugin
# auto-linking reads it unwrapped as an operand of $<AND:...>, and an empty value
# is a generate-time error ("Parameters to $<AND> must resolve to either '0' or
# '1'"). It was observed on Qt 6.8.3's iOS kit by CountdownSolver; whether 6.12's
# has it is unknown, so only a plugin that has no value is given one, using what
# the desktop kits say: the ones the application needs are linked, the rest are
# opt-in. (Added in a function so the targets exist: find_package(Qt6) made them.)
function(wjs_ios_default_plugins)
    # Linked: the platform integration, the image formats, the TLS and network
    # reachability plumbing, the camera's permission request (the application asks
    # for the camera: Mode A), and the QML debug services Qt's own kits link.
    foreach(_plugin IN ITEMS
            QDebugMessageServiceFactoryPlugin QGifPlugin QICOPlugin QIOSIntegrationPlugin
            QJpegPlugin QLocalClientConnectionFactoryPlugin QQmlDebugServerFactoryPlugin
            QQmlDebuggerServiceFactoryPlugin QQmlInspectorServiceFactoryPlugin
            QQmlNativeDebugConnectorFactoryPlugin QQmlNativeDebugServiceFactoryPlugin
            QQmlPreviewServiceFactoryPlugin QQmlProfilerServiceFactoryPlugin
            QQuickProfilerAdapterFactoryPlugin QSCNetworkReachabilityNetworkInformationPlugin
            QSecureTransportBackendPlugin QSvgIconPlugin QSvgPlugin
            QTcpServerConnectionFactoryPlugin QDarwinCameraPermissionPlugin)
        if(TARGET Qt6::${_plugin})
            get_target_property(_default Qt6::${_plugin} QT_DEFAULT_PLUGIN)
            if(_default STREQUAL "_default-NOTFOUND")
                set_target_properties(Qt6::${_plugin} PROPERTIES QT_DEFAULT_PLUGIN "1")
            endif()
        endif()
    endforeach()
    # Opt-in only: the other integrations, and the permission plugins of services
    # the application does not use (no Info.plist usage text exists for them, so
    # linking one would crash the first time its check ran).
    foreach(_plugin IN ITEMS
            QDarwinBluetoothPermissionPlugin QDarwinCalendarPermissionPlugin
            QDarwinContactsPermissionPlugin QDarwinLocationPermissionPlugin
            QDarwinMicrophonePermissionPlugin QIosOptionalPlugin_NSPhotoLibraryPlugin
            QMinimalIntegrationPlugin QOffscreenIntegrationPlugin
            QTlsBackendCertOnlyPlugin QTuioTouchPlugin)
        if(TARGET Qt6::${_plugin})
            get_target_property(_default Qt6::${_plugin} QT_DEFAULT_PLUGIN)
            if(_default STREQUAL "_default-NOTFOUND")
                set_target_properties(Qt6::${_plugin} PROPERTIES QT_DEFAULT_PLUGIN "0")
            endif()
        endif()
    endforeach()
endfunction()

# What makes the application a program of this device: a bundle with its own
# Info.plist (packaging/Info.plist.in: the identifier, the version, the camera
# usage text, the orientations and the launch screen) and an asset catalogue with
# the icon and the launch colour (packaging/Assets.xcassets, from assets/icon by
# tools/package/make_ios_icon.py). CMake fills the template's ${MACOSX_BUNDLE_...}
# names in from the properties below; the dictionary and the dice are copied beside
# the executable, which is the bundle's root, by the application's own post-build
# step (apps/app/CMakeLists.txt).
function(wjs_platform_configure_app target)
    set(_assets "${WJS_IOS_PACKAGING_DIR}/Assets.xcassets")
    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${WJS_IOS_PACKAGING_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "WordJewelsSolver"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.iainchesworth.wordjewelssolver"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (C) Iain Chesworth and contributors"
        # iPhone and iPad.
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        # The icon set Info.plist.in's CFBundleIconName names; without this Xcode has
        # nothing to compile that slot from and the home screen shows a blank icon.
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")

    # A source path ending in .xcassets is what tells the Xcode generator to
    # compile the catalogue rather than copy it.
    target_sources(${target} PRIVATE "${_assets}")
    set_source_files_properties("${_assets}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    if(WJS_IOS_SIGNING_TEAM AND WJS_IOS_SIGNING_PROFILE)
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Distribution"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${WJS_IOS_SIGNING_TEAM}"
            XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${WJS_IOS_SIGNING_PROFILE}")
    elseif(WJS_IOS_SIGNING_TEAM OR WJS_IOS_SIGNING_PROFILE)
        message(FATAL_ERROR
            "WJS_IOS_SIGNING_TEAM and WJS_IOS_SIGNING_PROFILE go together: one of "
            "them is set and the other is empty. Give both for a signed build, or "
            "neither for an unsigned one (docs/building.md, \"iOS\").")
    endif()

    wjs_ios_default_plugins()

    # Mode A asks for the camera (NSCameraUsageDescription, Info.plist.in). In a
    # static Qt the request is answered by this plugin, so it is named outright
    # whatever default the kit gives it.
    set(_plugins_in "")
    set(_plugins_out "")
    if(TARGET Qt6::QDarwinCameraPermissionPlugin)
        list(APPEND _plugins_in Qt6::QDarwinCameraPermissionPlugin)
    endif()
    # Qt Multimedia has two backends here and the kit links both by default. The
    # FFmpeg one needs FFmpeg's libraries, which the kit does not carry, so linking
    # it fails (undefined av_* symbols, seen on the first hosted build). The
    # AVFoundation one is the platform's own, and has the camera.
    if(TARGET Qt6::QFFmpegMediaPlugin AND TARGET Qt6::QDarwinMediaPlugin)
        list(APPEND _plugins_out Qt6::QFFmpegMediaPlugin)
        list(APPEND _plugins_in Qt6::QDarwinMediaPlugin)
    endif()
    if(_plugins_in OR _plugins_out)
        qt_import_plugins(${target} INCLUDE ${_plugins_in} EXCLUDE ${_plugins_out})
    endif()
endfunction()

# Static Qt: nothing to deploy beside the program.
function(wjs_platform_deploy_qt_runtime target qml_dir)
endfunction()

# No CPack generator: an iOS build is archived and exported by Xcode.
function(wjs_platform_configure_packaging)
endfunction()
