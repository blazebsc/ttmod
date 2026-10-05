# ADR-002: render flag-flattening reverted (flag means click-armed)

- Status: accepted (2026-10-05)
- Context: render dispatch draws highlight visuals via `0x56F820(800, flag,
  ...)` with flag=1 on rollover rows. A hook forcing flag to 0 rendered
  every row as normal - and killed all menu input the same session.
- Decision: the flag drives click dispatch (armed/activatable), not just
  highlight. Never force it. `TTMOD_UIFX=0` confirmed the causality; the
  hook was removed, not gated.
- Consequences: render-path interventions must preserve dispatch semantics
  (recolor, never suppress). Reopen only with per-widget targeting that
  leaves game buttons provably working.
