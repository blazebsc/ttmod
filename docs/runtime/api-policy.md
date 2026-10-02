# M17 API stability policy

## Versioning
- Plugin ABI: integer versions, append-only fields. Host v5 loads manifests
  api 1–5; v1–v4 plugins run unmodified (proven in Wine side-by-side).
  - v5 appended `queue_ui_chunk` (Lua execution; EXPERIMENTAL — contract in
    `docs/runtime/plugins.md`).
- Manifest schema: additive optional fields only. Unknown fields are SKIPPED
  (parsers), never errors — old manifests keep working.
- Package format: `package_format` int, default 1; >1 rejected (can't fake
  understanding the future).
- Framework version (`ttmod::kVersion`, CMake project version): informational.

## Stability tiers
- STABLE: manifest core (id/version/api/games), discovery layout (`mods/`,
  `manifest.json`), resolver semantics (priority/conflict/scope), plugin
  load/init contract, log locations (`logs/`).
- EXPERIMENTAL: event payload details (new fields may append), `ttmod_state`
  shape (counters stable; new fields may append), cache internals.
- INTERNAL: IAT/detour mechanics, unpack timing, RVA tables, exit-dump format.

## Rules
- Never break a working mod without a major reason; then bump API, keep the
  old path loading (M10 range check is the gate).
- Never silently change semantics; log behavior-affecting decisions.
- Docs mark every surface with its tier (this file is the index).
- Hook surface grows demand-driven only: a hook is a contract + a patch
  site risk on a packed exe. Add hooks when a real mod needs them, not
  speculatively (Fabric-API lesson; see plugins.md v5 for the growth path).
