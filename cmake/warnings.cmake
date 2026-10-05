# Baseline warnings (TTMod). Conservative on purpose: warn, don't fail.
# Stronger checks (conversion, sign-compare strictness) land incrementally
# after the tree proves clean - never enable everything at once.
# Usage: ttmod_apply_warnings(<target>) — first-party targets only,
# never vendored third_party/ code.
include_guard(GLOBAL)

option(TTMOD_ENABLE_WARNINGS "Compile first-party targets with baseline warnings" ON)

function(ttmod_apply_warnings target)
  if(NOT TTMOD_ENABLE_WARNINGS)
    return()
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wformat=2 -Wundef -Wshadow)
  endif()
endfunction()
