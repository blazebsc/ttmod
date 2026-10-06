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
  core/src/modid.cpp
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

add_executable(ttmod
  tools/cli/main.cpp
  tools/cli/common.cpp
  tools/cli/cmd_detect.cpp
  tools/cli/cmd_package.cpp
  tools/cli/cmd_inspect.cpp
  tools/cli/cmd_map.cpp
  tools/cli/cmd_mods.cpp)
target_link_libraries(ttmod PRIVATE ttmod_core)
ttmod_apply_warnings(ttmod)
ttmod_apply_sanitizers(ttmod)

ttmod_add_unit_test(sigmatch tests/unit/test_sigmatch.cpp)

ttmod_add_unit_test(profile tests/unit/test_profile.cpp)

ttmod_add_unit_test(manifest tests/unit/test_manifest.cpp)

ttmod_add_unit_test(pathnorm tests/unit/test_pathnorm.cpp)

ttmod_add_unit_test(resolver tests/unit/test_resolver.cpp)

ttmod_add_unit_test(events tests/unit/test_events.cpp)

ttmod_add_unit_test(gamestate tests/unit/test_gamestate.cpp)

ttmod_add_unit_test(package tests/unit/test_package.cpp)
target_link_libraries(test_package PRIVATE miniz)

ttmod_add_unit_test(modstate tests/unit/test_modstate.cpp)

ttmod_add_unit_test(discovery tests/unit/test_discovery.cpp)

ttmod_add_unit_test(cache tests/unit/test_cache.cpp)

ttmod_add_unit_test(offline tests/unit/test_offline.cpp)

ttmod_add_unit_test(lua_bridge tests/unit/test_lua_bridge.cpp)

ttmod_add_unit_test(modconfig tests/unit/test_modconfig.cpp)

enable_testing()
ttmod_add_unit_test(uiqueue tests/unit/test_uiqueue.cpp)
ttmod_add_unit_test(themecolor tests/unit/test_themecolor.cpp)
ttmod_add_unit_test(security tests/security/test_security.cpp)
ttmod_add_unit_test(runtime tests/unit/test_runtime.cpp)
ttmod_add_unit_test(result tests/unit/test_result.cpp)
ttmod_add_unit_test(modid tests/unit/test_modid.cpp)
ttmod_add_unit_test(manifest_split tests/unit/test_manifest_split.cpp)
find_program(PYTHON3_BIN python3)
if(PYTHON3_BIN)
  add_test(NAME lua_dis COMMAND ${PYTHON3_BIN} ${CMAKE_SOURCE_DIR}/tests/lua/test_lua_dis.py)
  set_tests_properties(lua_dis PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME modmenu_config COMMAND ${PYTHON3_BIN} ${CMAKE_SOURCE_DIR}/tests/lua/test_modmenu_config.py)
  set_tests_properties(modmenu_config PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME menumods_chunk COMMAND ${PYTHON3_BIN} ${CMAKE_SOURCE_DIR}/tests/lua/test_menumods_chunk.py)
  set_tests_properties(menumods_chunk PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME menumods_ui COMMAND ${PYTHON3_BIN} ${CMAKE_SOURCE_DIR}/tests/lua/test_menumods_ui.py)
  set_tests_properties(menumods_ui PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME menuadd_wrap COMMAND ${PYTHON3_BIN} ${CMAKE_SOURCE_DIR}/tests/lua/test_menuadd_wrap.py)
  set_tests_properties(menuadd_wrap PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

# Fuzz targets (Stage I): Clang + libFuzzer only, never default builds.
# cmake --preset linux-fuzz (or -DTTMOD_BUILD_FUZZ=ON with clang).
option(TTMOD_BUILD_FUZZ "Build libFuzzer harnesses (Clang only)" OFF)
if(TTMOD_BUILD_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "TTMOD_BUILD_FUZZ requires Clang")
  endif()
  foreach(fuzz_target fuzz_manifest fuzz_validate)
    add_executable(${fuzz_target} tests/fuzz/${fuzz_target}.cpp)
    target_link_libraries(${fuzz_target} PRIVATE ttmod_core)
    target_compile_options(${fuzz_target} PRIVATE -fsanitize=fuzzer,address -fno-omit-frame-pointer)
    target_link_options(${fuzz_target} PRIVATE -fsanitize=fuzzer,address)
  endforeach()
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
  add_library(dinput8 SHARED loader/windows/proxy/dinput8_proxy.cpp)
  # MinHook (BSD-2-Clause, Tsuda Kageyu; see third_party/minhook/LICENSE.txt).
  add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde32.c
    third_party/minhook/src/hde/hde64.c)
  target_include_directories(minhook PUBLIC third_party/minhook/include)
  add_library(ttmod_framework SHARED loader/windows/runtime/framework.cpp loader/windows/hooks/hooks.cpp loader/windows/plugins/plugins.cpp loader/windows/runtime/mods.cpp loader/windows/runtime/events.cpp loader/windows/lua/lua_bridge.cpp loader/windows/menu/menu_bridge.cpp ${TTMOD_CORE_SRCS})
ttmod_apply_warnings(ttmod_framework)
ttmod_apply_sanitizers(ttmod_framework)
  target_include_directories(ttmod_framework PUBLIC core/include)
  target_include_directories(ttmod_framework PRIVATE loader/windows loader/windows/proxy loader/windows/hooks loader/windows/plugins loader/windows/lua loader/windows/menu loader/windows/platform loader/windows/runtime third_party/minhook/include third_party/nlohmann/include)
  target_link_libraries(ttmod_framework PRIVATE minhook miniz)
  # Embedded Mods-menu Lua (generated header, never committed).
  find_package(Python3 COMPONENTS Interpreter)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/menumods_ui.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed_lua.py
      ${CMAKE_SOURCE_DIR}/loader/windows/menu/menumods_ui.lua
      ${CMAKE_CURRENT_BINARY_DIR}/menumods_ui.h kMenuModsUi
    DEPENDS ${CMAKE_SOURCE_DIR}/loader/windows/menu/menumods_ui.lua
            ${CMAKE_SOURCE_DIR}/tools/embed_lua.py
    COMMENT "Embedding menumods_ui.lua")
  add_custom_target(menumods_ui_h DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/menumods_ui.h)
  add_dependencies(ttmod_framework menumods_ui_h)
  target_include_directories(ttmod_framework PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  # Example plugins: staged as example-plugins/<dir>/plugin.dll.
  ttmod_add_example_plugin(hello_mcsm hello-mcsm)
  ttmod_add_example_plugin(title_mcsm title-mcsm)
  # Dear ImGui (MIT, vendored): parked diagnostics tooling, off by default.
  # Native UI is the strategy; enable explicitly for overlay experiments.
  option(TTMOD_BUILD_IMGUI "Build vendored Dear ImGui (parked diagnostics)" OFF)
  if(TTMOD_BUILD_IMGUI)
  add_library(imgui STATIC
    third_party/imgui/src/imgui.cpp
    third_party/imgui/src/imgui_draw.cpp
    third_party/imgui/src/imgui_tables.cpp
    third_party/imgui/src/imgui_widgets.cpp
    third_party/imgui/src/imgui_impl_dx9.cpp
    third_party/imgui/src/imgui_impl_win32.cpp)
  target_include_directories(imgui PUBLIC third_party/imgui/include)
  endif()
  ttmod_add_example_plugin(titleprefix titleprefix)
  ttmod_add_example_plugin(event_log event-log)
  ttmod_add_example_plugin(modmenu_core modmenu-core)
  ttmod_add_example_plugin(demo_config demo-config)
  ttmod_add_example_plugin(session_clock session-clock)
  ttmod_add_example_plugin(modcount modcount)
  # Named after the mod, matching manifest "plugin": "plugins/menu-theme.dll".
  ttmod_add_example_plugin(menu_theme menu-theme OUTPUT_NAME menu-theme)
  # Drop-in names: dinput8.dll / ttmod_framework.dll (no lib prefix).
  set_target_properties(dinput8 PROPERTIES PREFIX "" SUFFIX ".dll")
  set_target_properties(ttmod_framework PROPERTIES PREFIX "" SUFFIX ".dll")
  if(MINGW)
    # Self-contained drop-in DLLs: no libstdc++/winpthread DLLs to ship next to the game.
    target_link_options(dinput8 PRIVATE -static-libgcc -static-libstdc++ -static -Wl,--kill-at)
    target_link_options(ttmod_framework PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
endif()
