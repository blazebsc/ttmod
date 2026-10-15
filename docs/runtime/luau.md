# Luau runtime (PR 31)

**Status:** IMPLEMENTED (backend + tests; production wiring follows the
script thread + real-VM work, same as the Lua backend). Mods select it
with `"runtimes": ["luau"]` + `"entrypoints": {"luau": "main.luau"}`.

Same contract as the Lua backend (`core/script/luau/luau_vm.cpp` mirrors
`core/script/lua/lua_vm.cpp` case-for-case, and
`tests/script/test_luau_vm.cpp` mirrors the Lua suite): same six ScriptVm
operations, same sandbox shape (base/table/string/math/utf8 only), same
per-mod environments (writes isolated, `ttmod.*` shared), same budget
enforcement, same `ttmod.*` bindings, same runner stages, same native
plugin binding namespace.

## What Luau adds for mod authors

- Gradual typing annotations (erased at compile; never checked at
  runtime — the vendored build has no type checker by design)
- `+=` and compound assignment, `continue`, string interpolation
  (backticks), if-then-else expressions
- Luau `table` extras (`freeze`, `clone`, `create`, `find`)

## Sandboxed, same as Lua

Absent (nil, not callable): `os`, `io`, `package`, `debug`,
`dofile`, `loadfile`, `collectgarbage`. Luau-specific denials:
`getfenv`/`setfenv` (Luau's env-escape adjacent names; Lua 5.4's base
never had them). Budgets use Luau's safepoint interrupt (an approximate
step cap — a `while true do end` trips immediately; straight-line code
between safepoints is not counted).

## Not yet

- Nothing structural: the framework links the Luau library and Luau mods
  run in production (same registry, same bindings, per-mod envs) since
  the PR 31 wiring. The DLL grew ~7MB for the vendored interpreter -
  the price of a second backend by design, not accident.
