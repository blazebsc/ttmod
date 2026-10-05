# ADR-001: hover white cannot be restored from Lua

- Status: accepted (2026-10-04)
- Context: themed menu rows flash white on hover and stay white after
  mouse-off. The engine has no mouse-off deselect and no hover/selected
  color property (324-name read-only sweep + full Lua binding enumeration
  + native traffic analysis all negative). No Lua code runs on unhover.
- Decision: do not pursue Lua-side restore (wrappers, trigger callbacks,
  chore blanking, prototype paint - all tried, all dead). Theme colors the
  rest state; selection highlight stays engine-owned. The native `scol`
  substitute covers engine-side writes; render-internal highlight is out
  of scope (see ADR-002).
- Consequences: `theme-audit` exists to prove writes, not to fix them.
  Reopen only with a newly discovered unhover event or property.
