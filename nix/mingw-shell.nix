# Reproducible i686-windows MinGW shell (no sudo needed).
# Usage:
#   nix-shell nix/mingw-shell.nix --run 'cmake -S . -B build-win32 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
#     -DTTMOD_BUILD_WIN_RUNTIME=ON && cmake --build build-win32'
with import <nixpkgs> { };
pkgsCross.mingw32.mkShell { }
