# SPDX-License-Identifier: GPL-3.0-or-later
# ---------------------------------------------------------------------------
# PackagePreflight.cmake - run by CPack before it builds any package
# (CPACK_PRE_BUILD_SCRIPTS, set in cmake/Packaging.cmake).
#
# A Tracy client opens a TCP listener on every interface and answers UDP
# discovery broadcasts, independently of anything the application does
# (docs/profiling.md, "The profiler listens on the network"), so a build with it
# in is a development build and must not become a package that someone installs.
# The Release presets turn it off; this is the check that stops a Debug or a
# profiling tree being packaged by mistake, from a bare `cpack`, the IDE's
# package target or a script.
#
# A development package is a legitimate thing to make (the Windows CI leg
# packages its Debug build, to check the packaging itself), so it has to be asked
# for by name:
#
#   cpack -D CPACK_WJS_ALLOW_DEV_PACKAGE=ON
#
# The two variables below are written into CPackConfig.cmake by
# cmake/Packaging.cmake; CPack's own -D overrides the second.
# ---------------------------------------------------------------------------
if(CPACK_WJS_TRACY AND NOT CPACK_WJS_ALLOW_DEV_PACKAGE)
    message(FATAL_ERROR
        "Refusing to package this build: it was configured with the Tracy profiler "
        "(WJS_ENABLE_TRACY=ON), which listens on the network. A package is made "
        "from a Release preset (the *-release ones in CMakePresets.json), "
        "where it is off.\n"
        "To make a DEVELOPMENT package on purpose (for example to check the "
        "packaging itself), run: cpack -D CPACK_WJS_ALLOW_DEV_PACKAGE=ON")
endif()
if(CPACK_WJS_TRACY)
    message(WARNING
        "This is a development package: the Tracy profiler is in it. Do not "
        "publish it.")
endif()
