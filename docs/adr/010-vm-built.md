# ADR-010: TTMod-owned script VMs built (closes ADR-005)

- Status: accepted (2026-10-16)
- Context: ADR-005 deferred building a TTMod-owned VM until "a real mod
  needing TTMod-VM execution". The roadmap then demanded it (mixed
  runtimes, per-mod envs, capability-gated `ttmod.*`) and the script
  mods shipped: `examples/hello-lua` (Lua 5.4), `examples/hello-luau`
  (Luau), both confirmed in-game.
- Decision: `core/script/lua/` + `core/script/luau/` backends behind the
  `ScriptVm` contract (`core/script_vm.hpp`), per-mod environments,
  256MB memory caps, `run_mod_entrypoints` runner (`mod_lifecycle`),
  production wiring at framework init. Luau vendored under
  `third_party/luau/` (Common/VM/Ast/Compiler/Bytecode only).
- Consequences: ADR-005's "no VM" text is superseded (kept frozen as
  evidence); `runtimes: [lua]` / `[luau]` execute. Permissions stay a
  boundary for `ttmod.*` reads (`mods.read`), never confinement for
  native code.
- Reopen: a VM-shape change (new language backend, cross-mod services).
