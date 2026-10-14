# Luau (third-party, vendored)

Upstream: https://github.com/luau-lang/luau (MIT, see LICENSE.txt;
Lua 5.x portions under lua_LICENSE.txt). Vendored at HEAD 8387cfb4
(2026-10-09): `Common/`, `VM/`, `Ast/`, `Compiler/`, `Bytecode/` - the
interpreter, the source-to-bytecode compiler it needs, and their shared
bits. Deliberately excluded: `CodeGen/` (native codegen - a JIT has no
place in a game process), `Require/` (module system - TTMod does its
own module loading, same rule as Lua's `require`), `Analysis/` (type
checker), `CLI/`, tests, fuzzers.

Why Luau at all (roadmap PR 31): a second scripting backend on the same
ScriptVm contract, so mods can choose it per `runtimes` entry. Same
semantics as the Lua backend by construction: same sandbox shape
(base/table/string/math/utf8, per-mod `_ENV` tables), same budget
mechanism (interrupt counter instead of hook mask - documented in
luau_vm.cpp), same `ttmod.*` bindings via the shared installer.

Luau API differences our backend bridges (all verified against the
vendored headers, not memory):
- chunks: `Luau::compile()` source->bytecode, then `luau_load()`
  (no `luaL_loadbufferx`); env passed as a stack index to `luau_load`.
- refs: `lua_ref`/`lua_unref` (no `luaL_*`, no registry-index arg).
- globals: `lua_pushvalue(L, LUA_GLOBALSINDEX)` (no pushglobaltable).
- budget: `lua_callbacks(L)->interrupt` at safepoints (no hook mask).
- base lib has no `load`/`dofile` (nothing to deny); `getfenv`/
  `setfenv` are denied instead (absent from Lua 5.4's base).
