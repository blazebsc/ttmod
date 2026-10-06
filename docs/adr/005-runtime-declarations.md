# ADR-005: manifest declares runtimes/permissions; execution mapping deferred

- Status: accepted (2026-10-06)
- Context: doc §§19-21 wants `runtimes`/`permissions` declarations plus a
  TTMod-owned Lua/Luau VM, enforcement, and multi-runtime execution.
- Decision: parse + validate + surface now (`RuntimeSpec`, strict name
  allowlists, CLI display, tests). No TTMod-owned VM is built: native and
  telltale-lua paths already execute; `lua`/`luau` validate today and run
  when a VM exists. Permissions are declarations, not enforcement -
  native code cannot be sandboxed by declaration (doc §21 says so itself).
- Consequences: building the VM, enforcement, mod-to-mod services, and
  Luau transformer stay deferred until a demanding mod exists.
- Reopen: a real mod needing TTMod-VM execution.
