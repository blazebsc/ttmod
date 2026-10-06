Lua 5.4.7 is vendored here, unmodified, from the upstream release tarball.

Source: https://www.lua.org/ftp/lua-5.4.7.tar.gz
License: MIT (see below). Copyright (C) 1994-2024 Lua.org, PUC-Rio.

WHAT WAS CHANGED
  Nothing. src/ is byte-identical to the upstream release. The build rules
  live in third_party/lua/CMakeLists.txt rather than upstream's Makefile, and
  that is the only difference.

WHY IT IS VENDORED
  The TTMod scripting VM must be TTMod-owned (doc §§2, 4, 23, 71): a separate
  VM from the game's Lua, never sharing a lua_State. Two alternatives were
  rejected:

    find_package(Lua)  fails on BOTH CI jobs today - the native job has no
                       liblua5.4-dev and the win32 job has no mingw Lua at
                       all. It would also make the Lua version baked into the
                       shipped DLL a property of whoever built it.
    FetchContent       needs network at configure time, and this project
                       builds offline and offline in CI.

  A vendored static library also keeps verify_win32.sh happy: the framework
  already links -static and rejects MinGW runtime DLL imports, so shipping a
  dynamic Lua would break that gate.

BUILD NOTES (third_party/lua/CMakeLists.txt)
  LUA_USE_LINUX and LUA_USE_POSIX are deliberately NOT defined: Lua's own
  POSIX module pulls in dlsym/readline/dlopen for no benefit inside a game
  process, and readline would be an unexpected dependency. The TTMod VM opens
  no files through Lua anyway - module loading is TTMod's job (doc §4.1).

  The upstream Makefile is not used. lua.c is deliberately NOT compiled: it
  contains main(), which must never end up in a library the game loads.

Version: 5.4.7. Bumping means replacing src/ wholesale and re-running the
script tests.