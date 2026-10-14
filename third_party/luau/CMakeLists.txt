# Luau, vendored unmodified (VM + Compiler + Ast + Common, HEAD 8387cfb4).
# See README.ttmod.md. Interpreter only: no CodeGen (native codegen),
# no Require (module system), no Analysis (type checker), no CLI.
file(GLOB LUAU_SRCS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/Bytecode/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Common/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/VM/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Ast/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Compiler/src/*.cpp
)

add_library(luau STATIC ${LUAU_SRCS})
target_include_directories(luau PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Bytecode/include
  ${CMAKE_CURRENT_SOURCE_DIR}/Common/include
  ${CMAKE_CURRENT_SOURCE_DIR}/VM/include
  ${CMAKE_CURRENT_SOURCE_DIR}/Ast/include
  ${CMAKE_CURRENT_SOURCE_DIR}/Compiler/include
)
# Upstream style: silence it wholesale, never -Werror (mirrors lua54).
target_compile_options(luau PRIVATE -w)
set_target_properties(luau PROPERTIES CXX_STANDARD 17)
