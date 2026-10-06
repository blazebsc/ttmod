# ADR-004: theme color decisions live in core, tested once

- Status: accepted (2026-10-05)
- Context: the accent rule (parse `#RRGGBB`, gray-bright threshold, int vs
  float scales) was implemented twice - Lua (`theme_substitute`) and C++
  (`hook_scol` + a hand parser) - with only hover sessions covering the
  native half. Drift between the two is a wrong-color bug.
- Decision: `core/theme_color.hpp` owns `parse_accent` +
  `should_substitute`, covered by `tests/unit/test_themecolor.cpp` golden
  vectors. The native hook calls core; the Lua suite asserts the same
  literals (pointer comment, kept in sync by review). The hook keeps only
  config-file reading + the mods.json gate (platform work core can't do).
- Consequences: hue bugs get one unit test, not one hover session each.
