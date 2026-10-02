# M15 architecture audit (no MCSM2 exe needed)

## Method
Walked every layer asking: "does this assume MCSM1/x86/one build?" Refactored
nothing without a concrete problem; findings below.

## Common (game-independent)
- PE parsing, signatures, path normalization (Windows semantics), resolver,
  event bus, manifests, packages, state, discovery, cache, CLI plumbing.
- `classify_path` resdesc/ttarch2 categories: Telltale-generic (all Tool
  games use resdesc + ttarch), not MCSM1-specific.
- Plugin ABI: game/season strings, additive versioning.

## Isolated game-specific (correct as-is)
- `select_profile` build table (MCSM1 sizes/hashes) - data, not logic.
- `parse_episode` 101–108 pattern - MCSM1-only, fenced in gamestate + docs.
- `hooks_init` gates on profile id `mcsm1_pc_x86` - adapter dispatch; extend
  per game, never generalize by assumption.
- dinput8 proxy strategy - valid because MCSM1 imports DINPUT8 (verified);
  other games need their own bootstrap evidence.
- `m.arch` x86 gate in the plugin loader - MCSM1 policy; multi-arch needs a
  profile-driven machine table (known future work, not implemented).

## Fixed during audit
- Stale init log claiming "M1: no hooks" - corrected.
- `create_package` silently dropping root `plugin.dll` - fixed (allowlist).

## Unknown (awaiting MCSM2 exe)
Engine generation delta, archive version delta, Lua delta, bootstrap fit.
x64 routing already lands on `unknown-x64`/`defined-not-implemented`.
