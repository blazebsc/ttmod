cmake_minimum_required(VERSION 3.20)
project(ttmod VERSION 0.12.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ttmod_core
  core/src/detect.cpp
  core/src/sigmatch.cpp
  core/src/profile.cpp
  core/src/log.cpp
  core/src/manifest.cpp
  core/src/pathnorm.cpp
  core/src/resolver.cpp
  core/src/events.cpp
  core/src/gamestate.cpp
  core/src/moddeps.cpp
  core/src/package.cpp
  core/src/modstate.cpp
  core/src/discovery.cpp
  core/src/cache.cpp
  core/src/ttarch.cpp
  core/src/logmap.cpp
  core/src/lua_bridge.cpp
  core/src/modconfig.cpp
  core/src/uiqueue.cpp
)
target_include_directories(ttmod_core PUBLIC core/include)

# miniz (MIT, vendored) for .ttmod ZIP handling.
add_library(miniz STATIC
  third_party/miniz/src/miniz.c
  third_party/miniz/src/miniz_tdef.c
  third_party/miniz/src/miniz_tinfl.c
  third_party/miniz/src/miniz_zip.c)
target_include_directories(miniz PUBLIC third_party/miniz/include)
target_link_libraries(ttmod_core PRIVATE miniz)

add_executable(ttmod-detect tools/detect_main.cpp)
target_link_libraries(ttmod-detect PRIVATE ttmod_core)

add_executable(ttmod tools/ttmod_main.cpp)
target_link_libraries(ttmod PRIVATE ttmod_core)

add_executable(test_sigmatch tests/test_sigmatch.cpp)
target_link_libraries(test_sigmatch PRIVATE ttmod_core)

add_executable(test_profile tests/test_profile.cpp)
target_link_libraries(test_profile PRIVATE ttmod_core)

add_executable(test_manifest tests/test_manifest.cpp)
target_link_libraries(test_manifest PRIVATE ttmod_core)

add_executable(test_pathnorm tests/test_pathnorm.cpp)
target_link_libraries(test_pathnorm PRIVATE ttmod_core)

add_executable(test_resolver tests/test_resolver.cpp)
target_link_libraries(test_resolver PRIVATE ttmod_core)

add_executable(test_events tests/test_events.cpp)
target_link_libraries(test_events PRIVATE ttmod_core)

add_executable(test_gamestate tests/test_gamestate.cpp)
target_link_libraries(test_gamestate PRIVATE ttmod_core)

add_executable(test_package tests/test_package.cpp)
target_link_libraries(test_package PRIVATE ttmod_core miniz)

add_executable(test_modstate tests/test_modstate.cpp)
target_link_libraries(test_modstate PRIVATE ttmod_core)

add_executable(test_discovery tests/test_discovery.cpp)
target_link_libraries(test_discovery PRIVATE ttmod_core)

add_executable(test_cache tests/test_cache.cpp)
target_link_libraries(test_cache PRIVATE ttmod_core)

add_executable(test_offline tests/test_offline.cpp)
target_link_libraries(test_offline PRIVATE ttmod_core)

add_executable(test_lua_bridge tests/test_lua_bridge.cpp)
target_link_libraries(test_lua_bridge PRIVATE ttmod_core)

add_executable(test_modconfig tests/test_modconfig.cpp)
target_link_libraries(test_modconfig PRIVATE ttmod_core)

enable_testing()
add_test(NAME sigmatch COMMAND test_sigmatch)
add_test(NAME profile COMMAND test_profile)
add_test(NAME manifest COMMAND test_manifest)
add_test(NAME pathnorm COMMAND test_pathnorm)
add_test(NAME resolver COMMAND test_resolver)
add_test(NAME events COMMAND test_events)
add_test(NAME gamestate COMMAND test_gamestate)
add_test(NAME package COMMAND test_package)
add_test(NAME modstate COMMAND test_modstate)
add_test(NAME discovery COMMAND test_discovery)
add_test(NAME cache COMMAND test_cache)
add_test(NAME offline COMMAND test_offline)
add_test(NAME lua_bridge COMMAND test_lua_bridge)
add_test(NAME modconfig COMMAND test_modconfig)
add_executable(test_uiqueue tests/test_uiqueue.cpp)
target_link_libraries(test_uiqueue PRIVATE ttmod_core)
add_test(NAME uiqueue COMMAND test_uiqueue)
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
  add_library(dinput8 SHARED loader/windows/dinput8_proxy.cpp)
  # MinHook (BSD-2-Clause, Tsuda Kageyu; see third_party/minhook/LICENSE.txt).
  add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde32.c
    third_party/minhook/src/hde/hde64.c)
  target_include_directories(minhook PUBLIC third_party/minhook/include)
  add_library(ttmod_framework SHARED loader/windows/framework.cpp loader/windows/hooks.cpp loader/windows/plugins.cpp loader/windows/mods.cpp loader/windows/events.cpp loader/windows/lua_bridge.cpp loader/windows/menu_bridge.cpp core/src/detect.cpp core/src/profile.cpp core/src/log.cpp core/src/sigmatch.cpp core/src/manifest.cpp core/src/pathnorm.cpp core/src/resolver.cpp core/src/events.cpp core/src/gamestate.cpp core/src/moddeps.cpp core/src/package.cpp core/src/modstate.cpp core/src/discovery.cpp core/src/cache.cpp core/src/lua_bridge.cpp core/src/modconfig.cpp core/src/uiqueue.cpp)
  target_include_directories(ttmod_framework PUBLIC core/include)
  target_include_directories(ttmod_framework PRIVATE loader/windows third_party/minhook/include)
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
