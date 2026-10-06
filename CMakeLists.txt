cmake_minimum_required(VERSION 3.20)
project(ttmod VERSION 0.12.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# NOTE: no CMAKE_C_EXTENSIONS OFF - vendored miniz needs GNU extensions
# (fseeko/ftello). First-party C++ stays strict via CMAKE_CXX_EXTENSIONS OFF.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(helpers)
include(warnings)
include(sanitizers)

# One canonical core source list (ADR: single owner). Both the native static
# lib and the Windows framework DLL consume it - adding a core file is one
# edit, and neither binary can silently miss it.
set(TTMOD_CORE_SRCS
  core/src/detect.cpp
  core/src/sigmatch.cpp
  core/src/profile.cpp
  core/src/log.cpp
  core/src/manifest.cpp
  core/src/pathnorm.cpp
  core/src/resolver.cpp
  core/src/events.cpp
  core/src/gamestate.cpp
  core/src/modgraph.cpp
  core/src/package.cpp
  core/src/modstate.cpp
  core/src/discovery.cpp
  core/src/cache.cpp
  core/src/ttarch.cpp
  core/src/logmap.cpp
  core/src/lua_bridge.cpp
  core/src/modconfig.cpp
  core/src/uiqueue.cpp
  core/src/theme_color.cpp
  core/src/validate.cpp
  core/src/version.cpp
)

add_library(ttmod_core ${TTMOD_CORE_SRCS})
ttmod_apply_warnings(ttmod_core)
ttmod_apply_sanitizers(ttmod_core)
target_include_directories(ttmod_core PUBLIC core/include)

# miniz (MIT, vendored) for .ttmod ZIP handling.
add_library(miniz STATIC
  third_party/miniz/src/miniz.c
  third_party/miniz/src/miniz_tdef.c
  third_party/miniz/src/miniz_tinfl.c
  third_party/miniz/src/miniz_zip.c)
target_include_directories(miniz PUBLIC third_party/miniz/include)
target_link_libraries(ttmod_core PRIVATE miniz)
# nlohmann/json (MIT, vendored single header): strict manifest/config
# parsing. PRIVATE - parse details never cross the core boundary.
target_include_directories(ttmod_core PRIVATE third_party/nlohmann/include)

add_executable(ttmod-detect tools/detect_main.cpp)
target_link_libraries(ttmod-detect PRIVATE ttmod_core)
ttmod_apply_warnings(ttmod-detect)
ttmod_apply_sanitizers(ttmod-detect)

add_executable(ttmod tools/ttmod_main.cpp)
target_link_libraries(ttmod PRIVATE ttmod_core)
ttmod_apply_warnings(ttmod)
ttmod_apply_sanitizers(ttmod)

ttmod_add_unit_test(sigmatch tests/test_sigmatch.cpp)

ttmod_add_unit_test(profile tests/test_profile.cpp)

ttmod_add_unit_test(manifest tests/test_manifest.cpp)

ttmod_add_unit_test(pathnorm tests/test_pathnorm.cpp)

ttmod_add_unit_test(resolver tests/test_resolver.cpp)

ttmod_add_unit_test(events tests/test_events.cpp)

ttmod_add_unit_test(gamestate tests/test_gamestate.cpp)

ttmod_add_unit_test(package tests/test_package.cpp)
target_link_libraries(test_package PRIVATE miniz)

ttmod_add_unit_test(modstate tests/test_modstate.cpp)

ttmod_add_unit_test(discovery tests/test_discovery.cpp)

ttmod_add_unit_test(cache tests/test_cache.cpp)

ttmod_add_unit_test(offline tests/test_offline.cpp)

ttmod_add_unit_test(lua_bridge tests/test_lua_bridge.cpp)

ttmod_add_unit_test(modconfig tests/test_modconfig.cpp)

enable_testing()
ttmod_add_unit_test(uiqueue tests/test_uiqueue.cpp)
ttmod_add_unit_test(themecolor tests/test_themecolor.cpp)
ttmod_add_unit_test(security tests/test_security.cpp)
find_program(PYTHON3_BIN python3)
if(PYTHON3_BIN)
  add_test(NAME lua_dis COMMAND ${PYTHON3_BIN} ${CMAKE_SOURCE_DIR}/tests/test_lua_dis.py)
  set_tests_properties(lua_dis PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME modmenu_config COMMAND ${PYTHON3_BIN} ${CMAKE_SOURCE_DIR}/tests/test_modmenu_config.py)
  set_tests_properties(modmenu_config PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME menumods_chunk COMMAND ${PYTHON3_BIN} ${CMAKE_SOURCE_DIR}/tests/test_menumods_chunk.py)
  set_tests_properties(menumods_chunk PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME menumods_ui COMMAND ${PYTHON3_BIN} ${CMAKE_SOURCE_DIR}/tests/test_menumods_ui.py)
  set_tests_properties(menumods_ui PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME menuadd_wrap COMMAND ${PYTHON3_BIN} ${CMAKE_SOURCE_DIR}/tests/test_menuadd_wrap.py)
  set_tests_properties(menuadd_wrap PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

# M1 Windows runtime (proxy + framework DLLs). Windows x86 toolchain only;
# OFF everywhere else. Sources are _WIN32-guarded and never built on Linux.
option(TTMOD_BUILD_WIN_RUNTIME "Build Windows proxy+framework DLLs (VS2022 x86 / mingw-w64)" OFF)
if(TTMOD_BUILD_WIN_RUNTIME)
  # The Windows runtime is 32-bit x86 only (the game is PE32). Fail early
  # instead of shipping an unloadable x64 DLL.
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "TTMOD_BUILD_WIN_RUNTIME requires a 32-bit x86 toolchain (i686)")
  endif()
  add_library(dinput8 SHARED loader/windows/dinput8_proxy.cpp)
  # MinHook (BSD-2-Clause, Tsuda Kageyu; see third_party/minhook/LICENSE.txt).
  add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde32.c
    third_party/minhook/src/hde/hde64.c)
  target_include_directories(minhook PUBLIC third_party/minhook/include)
  add_library(ttmod_framework SHARED loader/windows/framework.cpp loader/windows/hooks.cpp loader/windows/plugins.cpp loader/windows/mods.cpp loader/windows/events.cpp loader/windows/lua_bridge.cpp loader/windows/menu_bridge.cpp ${TTMOD_CORE_SRCS})
ttmod_apply_warnings(ttmod_framework)
ttmod_apply_sanitizers(ttmod_framework)
  target_include_directories(ttmod_framework PUBLIC core/include)
  target_include_directories(ttmod_framework PRIVATE loader/windows third_party/minhook/include third_party/nlohmann/include)
  target_link_libraries(ttmod_framework PRIVATE minhook miniz)
  # Embedded Mods-menu Lua (generated header, never committed).
  find_package(Python3 COMPONENTS Interpreter)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/menumods_ui.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed_lua.py
      ${CMAKE_SOURCE_DIR}/loader/windows/menumods_ui.lua
      ${CMAKE_CURRENT_BINARY_DIR}/menumods_ui.h kMenuModsUi
    DEPENDS ${CMAKE_SOURCE_DIR}/loader/windows/menumods_ui.lua
            ${CMAKE_SOURCE_DIR}/tools/embed_lua.py
    COMMENT "Embedding menumods_ui.lua")
  add_custom_target(menumods_ui_h DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/menumods_ui.h)
  add_dependencies(ttmod_framework menumods_ui_h)
  target_include_directories(ttmod_framework PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  # Example plugin (M3): output staged as example-plugins/hello-mcsm/plugin.dll.
  add_library(hello_mcsm SHARED examples/hello-mcsm/plugin.cpp)
  target_include_directories(hello_mcsm PRIVATE core/include)
  set_target_properties(hello_mcsm PROPERTIES PREFIX "" OUTPUT_NAME "plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-plugins/hello-mcsm")
  if(MINGW)
    target_link_options(hello_mcsm PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  add_library(title_mcsm SHARED examples/title-mcsm/plugin.cpp)
  target_include_directories(title_mcsm PRIVATE core/include)
  set_target_properties(title_mcsm PROPERTIES PREFIX "" OUTPUT_NAME "plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-plugins/title-mcsm")
  if(MINGW)
    target_link_options(title_mcsm PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  # Dear ImGui (MIT, vendored). Kept for future diagnostics tooling; the
  # parked D3D overlay example was removed (native UI is the strategy).
  add_library(imgui STATIC
    third_party/imgui/src/imgui.cpp
    third_party/imgui/src/imgui_draw.cpp
    third_party/imgui/src/imgui_tables.cpp
    third_party/imgui/src/imgui_widgets.cpp
    third_party/imgui/src/imgui_impl_dx9.cpp
    third_party/imgui/src/imgui_impl_win32.cpp)
  target_include_directories(imgui PUBLIC third_party/imgui/include)
  add_library(titleprefix SHARED examples/titleprefix/plugin.cpp)
  target_include_directories(titleprefix PRIVATE core/include)
  set_target_properties(titleprefix PROPERTIES PREFIX "" OUTPUT_NAME "plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-plugins/titleprefix")
  if(MINGW)
    target_link_options(titleprefix PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  add_library(event_log SHARED examples/event-log/plugin.cpp)
  target_include_directories(event_log PRIVATE core/include)
  set_target_properties(event_log PROPERTIES PREFIX "" OUTPUT_NAME "plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-plugins/event-log")
  if(MINGW)
    target_link_options(event_log PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  add_library(modmenu_core SHARED examples/modmenu-core/plugin.cpp)
  target_include_directories(modmenu_core PRIVATE core/include)
  set_target_properties(modmenu_core PROPERTIES PREFIX "" OUTPUT_NAME "plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-plugins/modmenu-core")
  if(MINGW)
    target_link_options(modmenu_core PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  add_library(demo_config SHARED examples/demo-config/plugin.cpp)
  target_include_directories(demo_config PRIVATE core/include)
  set_target_properties(demo_config PROPERTIES PREFIX "" OUTPUT_NAME "plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-plugins/demo-config")
  if(MINGW)
    target_link_options(demo_config PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  add_library(session_clock SHARED examples/session-clock/plugin.cpp)
  target_include_directories(session_clock PRIVATE core/include)
  set_target_properties(session_clock PROPERTIES PREFIX "" OUTPUT_NAME "plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-plugins/session-clock")
  if(MINGW)
    target_link_options(session_clock PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  add_library(modcount SHARED examples/modcount/plugin.cpp)
  target_include_directories(modcount PRIVATE core/include)
  set_target_properties(modcount PROPERTIES PREFIX "" OUTPUT_NAME "plugin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-plugins/modcount")
  if(MINGW)
    target_link_options(modcount PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  add_library(menu_theme SHARED examples/menu-theme/plugin.cpp)
  target_include_directories(menu_theme PRIVATE core/include)
  # Named after the mod, matching manifest "plugin": "plugins/menu-theme.dll".
  # A generic "plugin.dll" here is what shipped undeclared and never loaded.
  set_target_properties(menu_theme PROPERTIES PREFIX "" OUTPUT_NAME "menu-theme"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-plugins/menu-theme")
  if(MINGW)
    target_link_options(menu_theme PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  # Drop-in names: dinput8.dll / ttmod_framework.dll (no lib prefix).
  set_target_properties(dinput8 PROPERTIES PREFIX "" SUFFIX ".dll")
  set_target_properties(ttmod_framework PROPERTIES PREFIX "" SUFFIX ".dll")
  if(MINGW)
    # Self-contained drop-in DLLs: no libstdc++/winpthread DLLs to ship next to the game.
    target_link_options(dinput8 PRIVATE -static-libgcc -static-libstdc++ -static -Wl,--kill-at)
    target_link_options(ttmod_framework PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
endif()
