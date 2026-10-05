# Sanitizer options (TTMod). Linux native builds only - never the Windows
# runtime (MinGW ASan is unsupported) and never vendored third_party/ code.
# Usage: cmake --preset linux-asan (or -DTTMOD_ENABLE_ASAN=ON).
include_guard(GLOBAL)

option(TTMOD_ENABLE_ASAN "Build with AddressSanitizer (Linux native only)" OFF)
option(TTMOD_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer (Linux native only)" OFF)

function(ttmod_apply_sanitizers target)
  set(_flags "")
  if(TTMOD_ENABLE_ASAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _flags -fsanitize=address -fno-omit-frame-pointer)
  endif()
  if(TTMOD_ENABLE_UBSAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _flags -fsanitize=undefined -fno-omit-frame-pointer)
  endif()
  if(_flags)
    target_compile_options(${target} PRIVATE ${_flags})
    target_link_options(${target} PRIVATE ${_flags})
  endif()
endfunction()
