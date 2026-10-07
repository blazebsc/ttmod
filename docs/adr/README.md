# Architecture Decision Records

Closed verdicts. Read these before re-litigating hover, hooks, or theme
decisions - each records what was tried, what the evidence said, and what
would reopen it.

- [001](001-hover-restore-impossible.md) - hover white cannot be restored from Lua
- [002](002-uifx-reverted.md) - render flag-flattening reverted (flag means click-armed)
- [003](003-setter-not-getter.md) - substitute at the setter, not the getter
- [004](004-theme-color-in-core.md) - theme color decisions live in core, tested once
- [005](005-runtime-declarations.md) - manifest declares runtimes/permissions; execution mapping deferred
- [006](006-state-registry.md) - game Lua states get identity, not just a count
- [007](007-version-grammar.md) - bounded version grammar, prerelease ignored, not SemVer
- [008](008-result-access-contract.md) - Result access: explicit, ref-qualified, move via std::move(r).value()
