# Cross-compile toolchain: any host (macOS ARM64, Linux) -> Windows x86_64 PE.
#
# Works with both common MinGW-w64 flavours:
#   * GCC   : `brew install mingw-w64`            -> x86_64-w64-mingw32-g++
#   * Clang : llvm-mingw (github.com/mstorsjo/llvm-mingw) -> x86_64-w64-mingw32-clang++
#
# Select with -DWRSR_MINGW_FLAVOR=gcc|clang (default gcc). If the compiler is
# not on PATH, point WRSR_MINGW_ROOT at the toolchain root (the dir with bin/).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(WRSR_MINGW_TRIPLE "x86_64-w64-mingw32" CACHE STRING "MinGW target triple")
set(WRSR_MINGW_FLAVOR "gcc" CACHE STRING "gcc (brew mingw-w64) or clang (llvm-mingw)")
set(WRSR_MINGW_ROOT "" CACHE PATH "Optional toolchain root containing bin/")

# Toolchain files are re-read for try_compile; forward our settings.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
     WRSR_MINGW_TRIPLE WRSR_MINGW_FLAVOR WRSR_MINGW_ROOT)

if(WRSR_MINGW_ROOT)
    set(_hint "${WRSR_MINGW_ROOT}/bin/")
else()
    set(_hint "")
endif()

if(WRSR_MINGW_FLAVOR STREQUAL "clang")
    set(CMAKE_C_COMPILER   "${_hint}${WRSR_MINGW_TRIPLE}-clang")
    set(CMAKE_CXX_COMPILER "${_hint}${WRSR_MINGW_TRIPLE}-clang++")
else()
    set(CMAKE_C_COMPILER   "${_hint}${WRSR_MINGW_TRIPLE}-gcc")
    set(CMAKE_CXX_COMPILER "${_hint}${WRSR_MINGW_TRIPLE}-g++")
endif()
set(CMAKE_RC_COMPILER "${_hint}${WRSR_MINGW_TRIPLE}-windres")

# Never pick up host (macOS/Linux) headers or libraries.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
