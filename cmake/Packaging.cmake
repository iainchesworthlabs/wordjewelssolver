# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# Packaging.cmake - CPack. Included once, from the top-level CMakeLists.txt,
# after every target's install() rules have been declared: the packages it makes
# are exactly those rules (apps/cli/CMakeLists.txt, apps/app/CMakeLists.txt and
# cmake/Notices.cmake), so nothing here lists a file a second time.
#
# One package, holding both programs, deliberately. A package is the two
# programs and the files they read - wjs-cli is small, the window cannot run
# without the dictionary and the dice, and a person who downloads either wants
# the solver that finds the words. Splitting them into a component each would
# duplicate the dictionary and the notices, which is the only thing a
# per-component split would buy here. The library is not installed at all: it is
# a set of headers and a static archive for this tree's own programs, not a
# third-party SDK (docs/faq.md says the same).
#
# A ZIP is always offered: it needs no tool beyond CPack itself, on every host
# and as the fallback where a platform's own packager is not installed. The
# platform's native formats are added by the platform fragment's
# wjs_platform_configure_packaging() hook (libs/platforms/<name>/platform.cmake):
# NSIS on Windows only when makensis is found, DragNDrop on macOS, and TGZ/DEB/
# RPM on Linux when their tools are there. A missing tool drops its generator
# and says so; it never fails the configure or the build, so `cpack -G ZIP`
# works on a machine that has nothing extra installed.
#
# Which generators `cpack` runs is CPACK_GENERATOR below. The package presets in
# CMakePresets.json deliberately carry no "generators" field: `cpack --preset`
# would pass that to cpack as -G and override this computed list, so the
# graceful degradation would apply to a bare `cpack` only. A -G on the command
# line still overrides it, which is what `cpack -G ZIP` in CI and docs uses.
#
# The generated packages land in build/<preset>/packages/ (CPACK_PACKAGE_DIRECTORY
# below, all generators), with CPack's staging tree in _CPack_Packages/ beside
# them.
# ---------------------------------------------------------------------------
include_guard(GLOBAL)

set(CPACK_PACKAGE_NAME "WordJewelsSolver")
set(CPACK_PACKAGE_VENDOR "Iain Chesworth")
set(CPACK_PACKAGE_CONTACT "Iain Chesworth")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")

# A prerelease (a v0.1.0-rc.1 tag, a rehearsal run) is packaged under its own
# version, so its files are not named like the final release's. The release
# workflow passes the part after the tag's hyphen. It is empty for a release and
# for every development build, which keeps the plain project version.
set(WJS_PACKAGE_PRERELEASE "" CACHE STRING
    "Prerelease identifier appended to the package version (for example rc.1); empty for a final release")
if(WJS_PACKAGE_PRERELEASE)
    if(NOT WJS_PACKAGE_PRERELEASE MATCHES "^[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*$")
        message(FATAL_ERROR
            "WJS_PACKAGE_PRERELEASE is '${WJS_PACKAGE_PRERELEASE}'; it takes SemVer "
            "prerelease identifiers: letters, digits and hyphens, separated by dots "
            "(rc.1, dryrun.17).")
    endif()
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}-${WJS_PACKAGE_PRERELEASE}")
endif()
set(CPACK_PACKAGE_INSTALL_DIRECTORY "WordJewelsSolver")
# The GPL text: the installer shows it before installing, and
# cmake/Notices.cmake installs it as LICENSE.txt beside the programs (the
# application's own licence is GPL-3.0-only, which NOTICES.txt states).
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

set(CPACK_GENERATOR "ZIP")

# The notice and the licence go beside the programs (cmake/Notices.cmake) on
# every platform this project can be installed on.
wjs_install_notices()

# A build with Tracy in it is a development build, and its programs load Tracy's
# client library at start-up: where Tracy is a shared library (vcpkg's dynamic
# Windows triplet) the package has to carry it beside them, or the packaged
# programs do not start (STATUS_DLL_NOT_FOUND). Only a development package is
# ever made this way (cmake/PackagePreflight.cmake); a Release build links no
# Tracy, and where Tracy is a static library there is nothing to install.
if(WJS_ENABLE_TRACY AND TARGET Tracy::TracyClient)
    get_target_property(_wjs_tracy_type Tracy::TracyClient TYPE)
    if(_wjs_tracy_type STREQUAL "SHARED_LIBRARY")
        install(IMPORTED_RUNTIME_ARTIFACTS Tracy::TracyClient
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
    unset(_wjs_tracy_type)
endif()

if(NOT WJS_PLATFORM_DESKTOP)
    message(STATUS
        "Packaging: ${WJS_PLATFORM_DISPLAY_NAME} is not a desktop this project "
        "packages; no CPack configuration is written.")
    return()
endif()

# The platform's own generators, if its tools are there (see the header).
wjs_platform_configure_packaging()

# A ZIP's contents are under one directory named after the package (CPack's
# default for the archive generators, which is left alone here - the DEB and RPM
# generators default the other way and must keep doing so, or the package would
# install into /WordJewelsSolver-.../). Unpacking the archive therefore makes
# one folder, `WordJewelsSolver-<version>-<system>/`, with bin/ inside it.

# ---------------------------------------------------------------------------
# NSIS, on Windows, when makensis was found. The two programs are installed
# side by side in the bindir, so the Start Menu entry for the window resolves
# to $INSTDIR\bin\WordJewelsSolver.exe (CPACK_NSIS_EXECUTABLES_DIRECTORY
# defaults to "bin", the same directory the install rules use). The console
# program gets no shortcut of its own: a .lnk to it would open a console, print
# the usage and close before anyone could read it.
# ---------------------------------------------------------------------------
if("NSIS" IN_LIST CPACK_GENERATOR)
    set(CPACK_NSIS_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
    set(CPACK_NSIS_DISPLAY_NAME "${CPACK_PACKAGE_NAME} ${PROJECT_VERSION}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    if(TARGET WordJewelsSolver)
        set(CPACK_PACKAGE_EXECUTABLES "WordJewelsSolver" "WordJewelsSolver")
    endif()

    # The programs are 64-bit, and the installer itself is a 32-bit NSIS program:
    # without this, $PROGRAMFILES is "Program Files (x86)".
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    # The installer's and the uninstaller's icon, and the one Programs and
    # Features shows. The same mark as the executable's (packaging/app.rc.in).
    set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/assets/icon/app.ico")
    set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/assets/icon/app.ico")
    if(TARGET WordJewelsSolver)
        set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\WordJewelsSolver.exe")
        # The last page of the installer offers to start the program.
        set(CPACK_NSIS_MUI_FINISHPAGE_RUN "WordJewelsSolver.exe")
        # A desktop shortcut besides the Start Menu entry, removed with the program.
        set(CPACK_NSIS_CREATE_ICONS_EXTRA
            "CreateShortCut '$DESKTOP\\WordJewelsSolver.lnk' '$INSTDIR\\bin\\WordJewelsSolver.exe'")
        set(CPACK_NSIS_DELETE_ICONS_EXTRA
            "Delete '$DESKTOP\\WordJewelsSolver.lnk'")
    endif()
    # Programs and Features: publisher, support and about links, and the text at
    # the foot of the installer window.
    set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_NSIS_HELP_LINK "${PROJECT_HOMEPAGE_URL}/issues")
    if(WJS_PACKAGE_PRERELEASE)
        set(CPACK_NSIS_BRANDING_TEXT "${CPACK_PACKAGE_NAME} ${PROJECT_VERSION}-${WJS_PACKAGE_PRERELEASE}")
    else()
        set(CPACK_NSIS_BRANDING_TEXT "${CPACK_PACKAGE_NAME} ${PROJECT_VERSION}")
    endif()
    set(CPACK_NSIS_BRANDING_TEXT_TRIM_POSITION LEFT)
endif()

# ---------------------------------------------------------------------------
# DEB, on Linux, when dpkg-deb and dpkg-shlibdeps were found. The package name
# is lower case, as Debian requires, and the file name is dpkg's own
# <name>_<version>_<arch>.deb form. dpkg-shlibdeps writes the shared-library
# dependencies it can resolve.
#
# A DEB or an RPM is made for the command-line solver only. The Linux platform
# fragment offers them when the application is NOT built: the application needs a
# Qt 6.12 with Quick 3D, which no distribution's Qt packages are, so a Depends:
# line naming "libqt6core6 (>= 6.8)" would install a package that fails to start,
# and one naming 6.12 could not be installed. The application is distributed as
# an AppImage that carries its Qt (libs/platforms/linux/packaging).
# ---------------------------------------------------------------------------
if("DEB" IN_LIST CPACK_GENERATOR)
    set(CPACK_DEBIAN_PACKAGE_NAME "wordjewelssolver")
    if(WJS_PACKAGE_PRERELEASE)
        # dpkg sorts "0.1.0~rc.1" before "0.1.0"; a hyphen would make rc.1 the revision.
        set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}~${WJS_PACKAGE_PRERELEASE}")
    endif()
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

# ---------------------------------------------------------------------------
# RPM, on Linux, when rpmbuild was found: the command-line solver, GPL-3.0-or-later
# (the application, GPL-3.0-only because it links Qt Quick 3D, is the AppImage's).
# AUTOREQPROV writes the shared-library requirements it can resolve.
# ---------------------------------------------------------------------------
if("RPM" IN_LIST CPACK_GENERATOR)
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    if(WJS_PACKAGE_PRERELEASE)
        # An RPM version cannot contain a hyphen and a release sorts after it, so a
        # prerelease is release 0.<identifier>, below the final release's 1.
        string(REPLACE "-" "_" _wjs_rpm_pre "${WJS_PACKAGE_PRERELEASE}")
        set(CPACK_RPM_PACKAGE_VERSION "${PROJECT_VERSION}")
        set(CPACK_RPM_PACKAGE_RELEASE "0.${_wjs_rpm_pre}")
        unset(_wjs_rpm_pre)
    endif()
    set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
endif()

# ---------------------------------------------------------------------------
# DragNDrop, on macOS: the .app and the programs in a disk image with the usual
# Applications link. The application's dictionary and dice are inside the
# bundle (WJS_PLATFORM_INSTALL_APP_DATA_DIR, libs/platforms/macos), because the
# program looks for them beside its own executable. No macOS host has built
# the application with Qt yet, so the image itself is unverified (docs/ci.md).
# ---------------------------------------------------------------------------
if("DragNDrop" IN_LIST CPACK_GENERATOR)
    set(CPACK_DMG_VOLUME_NAME "${CPACK_PACKAGE_NAME} ${PROJECT_VERSION}")
endif()

# CPack refuses to package a build that has the Tracy profiler in it, unless a
# development package is asked for by name (cmake/PackagePreflight.cmake says why).
# The two variables reach the script through CPackConfig.cmake, which only
# carries CPACK_ ones. A refusal writes no package, says why and makes cpack
# (and so the package target) exit non-zero.
set(CPACK_WJS_TRACY "${WJS_ENABLE_TRACY}")
set(CPACK_WJS_ALLOW_DEV_PACKAGE OFF)
set(CPACK_PRE_BUILD_SCRIPTS "${PROJECT_SOURCE_DIR}/cmake/PackagePreflight.cmake")

# Remembered before include(CPack): the module ends by pointing CPACK_GENERATOR
# at CPACK_SOURCE_GENERATOR for the source package it also configures, so after
# it returns CPACK_GENERATOR no longer names the binary package's generators.
# The generated CPackConfig.cmake has the right value either way; this is what
# the messages and the target below read.
set(WJS_PACKAGE_GENERATORS "${CPACK_GENERATOR}")

include(CPack)

# `cmake --build <build> --target package` runs cpack from the build tree with
# the configuration above, for an IDE or a one-word command; cpack itself and
# the presets are documented in docs/building.md.
add_custom_target(package-${PROJECT_NAME}
    COMMAND "${CMAKE_CPACK_COMMAND}" -C "$<CONFIG>" --config "${CPACK_OUTPUT_CONFIG_FILE}"
    COMMENT "Running CPack (${WJS_PACKAGE_GENERATORS}) in ${CPACK_PACKAGE_DIRECTORY}"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    VERBATIM)
set_target_properties(package-${PROJECT_NAME} PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)

message(STATUS "Packaging: ${WJS_PACKAGE_GENERATORS} -> ${CPACK_PACKAGE_DIRECTORY}")
