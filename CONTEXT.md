# CONTEXT.md — TTMod domain glossary

Names for the seams. Use these terms exactly; do not invent synonyms.

- **menu state** — the game's Lua state that hosts menu scripts (`Menu.lua`
  and everything the Mods menu is built in). Distinct from the **engine
  state** (engine scripts). Plugin chunks run on the engine state unless
  replayed; menu code must live as bare globals (`_G` is nil).
- **paint** — writing the accent into `Text Color` (label clones) and
  `Selection Color` (button clone) via `AgentSetProperty`. Accent form is
  0..1 floats in named `{r,g,b,a}` fields on the Lua path; the rule also
  accepts 0..255 ints (same test, either scale) — see
  `core/theme_color.hpp` for the single source of the rule.
- **substitute** — the native `scol` hook rewriting near-gray-bright color
  structs to the accent in place. Catches engine-side writes that bypass Lua.
- **probe** — retired diagnosis detours (`asp`, `scolB`, `rol`, `uifx`). Their
  questions are answered (see `docs/adr/`); do not re-add them.
- **wrapper** — a Lua global replacement (e.g. `Menu_Add`,
  `theme_wrap_asp`) that tail-calls the original. Install idempotently;
  chunk top-level must only DEFINE, never CALL (libs open after capture).
- **drain** — `theme_drain()`: runs on every `Menu_Add`, retries unpopulated
  widgets and runs the read-only audit.
- **audit** — `theme_audit()`: re-reads painted labels, logs overwrites with
  `own:`/`menu:` attribution. Skips not-yet-painted labels (template reads
  false-flag).
- **accent** — the configured `#RRGGBB` in `config/menu.theme.json`.
- **scope** — `"all"` (every menu) vs `"ttmod"` (Mods screens only).
- **anchor** — byte patterns validating a live-memory hook target before
  MinHook installs. Mismatch installs nothing, game continues.
- **chunk** — a Lua string the bridge runs on the game state
  (`bridge_run_chunk`: balanced-stack + error-sink).
- **runtime** — a declared execution environment (`lua`, `luau`,
  `telltale-lua`, `native`); see `RuntimeSpec`. Declaration only until a
  TTMod-owned VM exists (ADR-005).
- **permission** — a declared capability request (`game.read`, `hooks`,
  …); validated against the §21 allowlist, not enforced (ADR-005).
- **state registry** — observed identity + role per game Lua state
  (ADR-006); the game owns every state.
