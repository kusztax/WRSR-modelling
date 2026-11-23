cmake_minimum_required(VERSION 3.20)

project(wrsr_rail_mod VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Host build (macOS/Linux, no toolchain file): only the platform-independent
# unit tests. The DLL itself needs the MinGW toolchain - see CMakePresets.json.
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(STATUS "Host build: compiling unit tests only (no DLL). "
                   "Use the 'mingw' preset to build wrsr_rail_mod.dll.")
    enable_testing()
    add_executable(aob_tests tests/aob_tests.cpp src/aob_pattern.cpp)
    target_include_directories(aob_tests PRIVATE src)
    add_test(NAME aob_tests COMMAND aob_tests)
    add_executable(priority_tests tests/priority_tests.cpp src/priority_policy.cpp)
    target_include_directories(priority_tests PRIVATE src)
    add_test(NAME priority_tests COMMAND priority_tests)
    return()
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SOVIET64.exe is x86_64: the plugin must be a 64-bit DLL.")
endif()

# ---------------------------------------------------------------------------
# MinHook, pinned. Downloaded at configure time; to build offline, clone it
# yourself and pass -DFETCHCONTENT_SOURCE_DIR_MINHOOK=/path/to/minhook.
# We compile its four sources ourselves instead of using its CMakeLists, which
# keeps the output name and flags under our control.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        c3fcafdc10146beb5919319d0683e44e3c30d537 # v1.3.4
    GIT_SHALLOW    FALSE
    SOURCE_SUBDIR  _do_not_add_subdirectory_)
FetchContent_MakeAvailable(minhook)

add_library(minhook STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)
target_compile_options(minhook PRIVATE -w)

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
add_library(wrsr_rail_mod SHARED
    src/dllmain.cpp
    src/logger.cpp
    src/hooks.cpp
    src/aob_pattern.cpp
    src/aob_scan.cpp
    src/env_info.cpp
    src/rail_probe.cpp
    src/signal_priority.cpp
    src/priority_policy.cpp
    src/script_switch.cpp
    src/lang.cpp
    src/research.cpp)

target_include_directories(wrsr_rail_mod PRIVATE src third_party/tesmio)
target_compile_definitions(wrsr_rail_mod PRIVATE
    "WIN32_LEAN_AND_MEAN=" NOMINMAX _WIN32_WINNT=0x0601
    WRSR_MOD_VERSION="${PROJECT_VERSION}")
target_compile_options(wrsr_rail_mod PRIVATE
    -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
    -Wno-cast-function-type -fno-strict-aliasing)

# No "lib" prefix: TesmioLoader/RML name the plugin after the file.
set_target_properties(wrsr_rail_mod PROPERTIES PREFIX "" OUTPUT_NAME "wrsr_rail_mod")

# Fully static runtime: the game folder has no libstdc++-6.dll /
# libgcc_s_seh-1.dll / libwinpthread-1.dll, and a missing dependency shows up
# only as "failed to load (126)" in the loader log. Also keeps our CRT private,
# which the Tesmio ABI requires (nothing is freed across the boundary).
target_link_options(wrsr_rail_mod PRIVATE
    -static -static-libgcc -static-libstdc++
    $<$<CONFIG:Release>:-s>)
target_link_libraries(wrsr_rail_mod PRIVATE minhook version)

# Ship the ini beside the DLL, as both loaders expect.
add_custom_command(TARGET wrsr_rail_mod POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/config/wrsr_rail_mod.ini
            $<TARGET_FILE_DIR:wrsr_rail_mod>/wrsr_rail_mod.ini)

# ---------------------------------------------------------------------------
# fake_host.exe: loads the DLL like TesmioLoader does, with a stub host table.
# Run it inside the CrossOver bottle to test the DLL without the game.
# ---------------------------------------------------------------------------
add_executable(fake_host tools/fake_host.cpp)
target_include_directories(fake_host PRIVATE third_party/tesmio)
target_compile_definitions(fake_host PRIVATE "WIN32_LEAN_AND_MEAN=" NOMINMAX)
target_link_options(fake_host PRIVATE -static -static-libgcc -static-libstdc++)
