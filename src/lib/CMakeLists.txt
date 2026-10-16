# The library holds all of the game logic. It is GUI-free and platform-free:
# nothing here includes an operating system header, and what differs between
# operating systems arrives through src/platforms/interfaces.

# Build-time facts (the version, the target platform's name) become constants
# in a header CMake generates, rather than macros the sources would declare.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/wjs/version.hpp.in"
    "${PROJECT_BINARY_DIR}/generated/wjs/version.hpp"
    @ONLY)

add_library(wjs_lib STATIC
    src/board.cpp
    src/dice.cpp
    src/dictionary.cpp
    src/game_rules.cpp
    src/grapheme_binder.cpp
    src/path.cpp
    src/phase_data.cpp
    src/scoring.cpp
    src/shake.cpp
    src/seeding.cpp
    src/solver.cpp)
add_library(wjs::lib ALIAS wjs_lib)

# Which wjs/profiling.hpp the zones in the sources resolve to: CMake chooses the
# directory, no preprocessor conditional does (cmake/Tracy.cmake). PUBLIC
# because the layers above the library are instrumented the same way, and the
# real variant includes Tracy's own headers, which wjs::tracy supplies.
target_include_directories(wjs_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/generated"
    "${WJS_PROFILING_VARIANT_DIR}")
target_compile_features(wjs_lib PUBLIC cxx_std_23)
target_link_libraries(wjs_lib
    PUBLIC
        wjs::tracy
    PRIVATE
        wjs::warnings
        wjs::sanitizers
        wjs::coverage)
