# Shared CMake helpers (TTMod). Included by the top-level CMakeLists.txt.
include_guard(GLOBAL)

# One example plugin DLL (Stage 20): consistent naming, static runtime,
# staged under example-plugins/<dir>/ with OUTPUT_NAME (default "plugin";
# menu-theme overrides to match its manifest "plugin" path).
function(ttmod_add_example_plugin target dir)
  cmake_parse_arguments(ARG "" "OUTPUT_NAME" "" ${ARGN})
  if(NOT ARG_OUTPUT_NAME)
    set(ARG_OUTPUT_NAME "plugin")
  endif()
  add_library(${target} SHARED examples/${dir}/plugin.cpp)
  target_include_directories(${target} PRIVATE core/include)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME "${ARG_OUTPUT_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/example-plugins/${dir}")
  if(MINGW)
    target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
endfunction()

# One unit test executable + CTest entry. Keeps tests/ wiring uniform so
# adding a test is one line and names stay stable for `ctest -R`.
# NAME is the short ctest name (e.g. sigmatch); the exe is test_<name>.
function(ttmod_add_unit_test name src)
  add_executable(test_${name} ${src})
  target_link_libraries(test_${name} PRIVATE ttmod_core)
  ttmod_apply_warnings(test_${name})
  ttmod_apply_sanitizers(test_${name})
  add_test(NAME ${name} COMMAND test_${name})
endfunction()
