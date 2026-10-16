# CMake 4 is required; the version range keeps policy behaviour current up to 4.4.
cmake_minimum_required(VERSION 3.28...4.4)

# ---------------------------------------------------------------------------
# Vendored vcpkg (git submodule at deps/vcpkg).
#
# The dependency provider is pinned by the submodule so builds are fully
# reproducible without a machine-wide vcpkg or a VCPKG_ROOT env var. The
# CMakePresets point CMAKE_TOOLCHAIN_FILE at deps/vcpkg/scripts/buildsystems.
# If the submodule was not checked out, initialise it here so a fresh clone
# configures without extra steps.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/vcpkg/scripts/buildsystems/vcpkg.cmake")
    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        message(STATUS "Initialising vendored vcpkg submodule (deps/vcpkg)")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" submodule update --init --depth 1 -- deps/vcpkg
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE WJS_SUBMODULE_RESULT)
        if(NOT WJS_SUBMODULE_RESULT EQUAL 0)
            message(FATAL_ERROR
                "Failed to initialise the vcpkg submodule. Run:\n"
                "  git submodule update --init --depth 1 deps/vcpkg")
        endif()
    endif()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/vcpkg/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR
        "Vendored vcpkg not found at deps/vcpkg.\n"
        "Clone with submodules (git clone --recurse-submodules ...) or run:\n"
        "  git submodule update --init --depth 1 deps/vcpkg")
endif()

# ---------------------------------------------------------------------------
# Select vcpkg manifest features BEFORE project() (the vcpkg toolchain reads
# VCPKG_MANIFEST_FEATURES when it is loaded at project() time). Qt is a
# prebuilt dependency found via CMAKE_PREFIX_PATH (see cmake/FindQt6.cmake),
# not built by vcpkg.
# ---------------------------------------------------------------------------
if(NOT DEFINED WJS_BUILD_TESTS OR WJS_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

# Tracy is opt-in: the one switch (declared in cmake/Tracy.cmake) both makes
# the profiling calls real and asks vcpkg for the client and the tools.
if(WJS_ENABLE_TRACY)
    list(APPEND VCPKG_MANIFEST_FEATURES "profiling")
endif()

project(WordJewelsSolver
    VERSION 0.1.0
    DESCRIPTION "Finds every word on a Boggle board"
    HOMEPAGE_URL "https://github.com/iainchesworthlabs/wordjewelssolver"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global language settings: C++23 only, no compiler extensions.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clang-tidy / editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Put all runtime artefacts in one place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# The one place the target OS is decided (cmake/Platform.cmake); every other
# CMake file reads what the platform's fragment declared.
include(Platform)
include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(Tracy)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(WJS_BUILD_CLI "Build the wjs-cli command-line solver" ON)
# Off until CI installs Qt: the application needs a prebuilt Qt 6.12 found via
# QT_ROOT_DIR or CMAKE_PREFIX_PATH (cmake/FindQt6.cmake), which is not vcpkg's.
option(WJS_BUILD_APP "Build the Qt Quick application (needs Qt 6.12)" OFF)
option(WJS_BUILD_TESTS "Build the unit test suite" ON)
# The prototype of the 3D board scene (src/app/prototype/shake, and
# docs/design/shake-prototype.md). A program of its own, not part of the
# application: it links Qt Quick 3D, which Qt offers under the GPL-3.0 only, so
# the program it makes is GPL-3.0 only (docs/design/board.md). Needs WJS_BUILD_APP.
option(WJS_BUILD_SHAKE_PROTOTYPE "Build the 3D board-scene prototype (needs Qt Quick 3D; GPL-3.0 only)" OFF)

# The review gate. Phonics data (data/phonics) is written and reviewed by a
# person qualified to do it, and a file says so in its header (reviewed-by and
# reviewed-on). A file without them is test data. With this on, the build runs
# tools/data/check_phonics.ps1 -RequireReviewed before the library is built and
# stops if any file is test data, so a release cannot carry unreviewed content.
# On by default for the release build types; the release presets set it
# explicitly. A multi-config generator has no build type at configure time, so
# pass it there.
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    set(WJS_REQUIRE_REVIEWED_PHONICS_DEFAULT ON)
else()
    set(WJS_REQUIRE_REVIEWED_PHONICS_DEFAULT OFF)
endif()
option(WJS_REQUIRE_REVIEWED_PHONICS
    "Refuse to build while data/phonics holds a file without a review header"
    ${WJS_REQUIRE_REVIEWED_PHONICS_DEFAULT})

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
# Everything that differs between operating systems, behind the interfaces in
# src/platforms/interfaces. Compiled from the sources the selected platform's
# platform.cmake names.
add_subdirectory(src/platforms)

# The library holds ALL of the game logic and is completely GUI-free and
# platform-free.
add_subdirectory(src/lib)

find_program(WJS_PWSH NAMES pwsh)
if(WJS_REQUIRE_REVIEWED_PHONICS)
    if(NOT WJS_PWSH)
        message(FATAL_ERROR
            "WJS_REQUIRE_REVIEWED_PHONICS is ON, and it is checked by "
            "tools/data/check_phonics.ps1, which needs PowerShell 7 (pwsh). "
            "Install it, or configure with -DWJS_REQUIRE_REVIEWED_PHONICS=OFF "
            "for a build that is not to be released.")
    endif()
    # Always out of date, so every build looks at the data as it is now, not as
    # it was when CMake last ran. The library depends on it, so building any
    # target that links the library is gated.
    add_custom_target(wjs_phonics_review_gate ALL
        COMMAND "${WJS_PWSH}" -NoProfile -File
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/data/check_phonics.ps1"
            -Root "${CMAKE_CURRENT_SOURCE_DIR}" -RequireReviewed
        COMMENT "Checking that the phonics data has been reviewed"
        VERBATIM)
    add_dependencies(wjs_lib wjs_phonics_review_gate)
endif()

if(WJS_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

# The application is a thin Qt Quick layer over the library.
if(WJS_BUILD_APP)
    # Found here, not in src/app: imported targets are directory-scoped, and
    # tests/ links Qt::Core too.
    set(WJS_QT_COMPONENTS Core Gui Qml Quick QuickControls2)
    if(WJS_BUILD_SHAKE_PROTOTYPE)
        list(APPEND WJS_QT_COMPONENTS Quick3D)
    endif()
    find_package(Qt6 6.12 REQUIRED COMPONENTS ${WJS_QT_COMPONENTS})
    add_subdirectory(src/app)
elseif(WJS_BUILD_SHAKE_PROTOTYPE)
    message(WARNING "WJS_BUILD_SHAKE_PROTOTYPE needs WJS_BUILD_APP=ON; the prototype is not built.")
endif()

if(WJS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "WordJewelsSolver ${PROJECT_VERSION} for ${WJS_PLATFORM_DISPLAY_NAME}")
message(STATUS "  Build CLI   : ${WJS_BUILD_CLI}")
message(STATUS "  Build app   : ${WJS_BUILD_APP}")
message(STATUS "  Build tests : ${WJS_BUILD_TESTS}")
message(STATUS "  Tracy       : ${WJS_ENABLE_TRACY}")
message(STATUS "  Reviewed phonics data required : ${WJS_REQUIRE_REVIEWED_PHONICS}")
message(STATUS "  Build shake prototype : ${WJS_BUILD_SHAKE_PROTOTYPE}")
