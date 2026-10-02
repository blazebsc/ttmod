# Windows x86 (i686) MinGW-w64 cross toolchain.
# Expects i686-w64-mingw32-{gcc,g++} on PATH, e.g. via:
#   nix-shell nix/mingw-shell.nix
# Configure with:
#   cmake -S . -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
#     -DTTMOD_BUILD_WIN_RUNTIME=ON
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR X86)

set(TTMOD_MINGW_PREFIX "i686-w64-mingw32" CACHE STRING "MinGW triplet prefix")

find_program(TTMOD_MINGW_CC ${TTMOD_MINGW_PREFIX}-gcc)
find_program(TTMOD_MINGW_CXX ${TTMOD_MINGW_PREFIX}-g++)
if(NOT TTMOD_MINGW_CC OR NOT TTMOD_MINGW_CXX)
  message(FATAL_ERROR
    "MinGW i686 compilers not on PATH. Enter the provided shell first:\n"
    "  nix-shell nix/mingw-shell.nix\n"
    "Need without nix: Arch/CachyOS mingw-w64-gcc (+crt/headers/binutils), no sudo was available here.")
endif()
set(CMAKE_C_COMPILER ${TTMOD_MINGW_CC})
set(CMAKE_CXX_COMPILER ${TTMOD_MINGW_CXX})
find_program(CMAKE_RC_COMPILER ${TTMOD_MINGW_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
