# Lua 5.4.7, vendored unmodified. See README.ttmod.md for why and how.
#
# lua.c is excluded on purpose: it contains main(), which must never end up in
# a library loaded into a game process. LUA_USE_LINUX / LUA_USE_POSIX are
# deliberately undefined (no dlsym/dlopen/readline in-process).
file(GLOB LUA_SRCS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)
list(REMOVE_ITEM LUA_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/lua.c)

add_library(lua54 STATIC ${LUA_SRCS})
target_include_directories(lua54 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Upstream C89: silence only what its own style provokes, never -Werror.
target_compile_options(lua54 PRIVATE -w)
set_target_properties(lua54 PROPERTIES C_STANDARD 99)