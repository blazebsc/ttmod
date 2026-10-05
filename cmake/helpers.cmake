# Shared CMake helpers (TTMod). Included by the top-level CMakeLists.txt.
include_guard(GLOBAL)

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
