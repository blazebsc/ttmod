# ADR-006: game Lua states get identity, not just a count

- Status: accepted (2026-10-06)
- Context: the bridge tracked states as a bare counter + last pointer
  (doc §57 wants identity/role/lifecycle). Role was folklore ("two
  states").
- Decision: `LuaStateInfo` registry (order, role inferred from loaded
  scripts: `Menu.lua` -> menu, engine boots -> engine, logged once),
  mutex-guarded, cleared on shutdown. No ownership change: the game
  still owns every state; raw pointers never leave the native layer.
- Consequences: logs now name states (`state #2 identified as menu`).
  Destroyed-state tracking stays out until something needs it.
