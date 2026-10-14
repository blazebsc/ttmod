# Runtime diagnostics (roadmap PR 24)

**Status:** IMPLEMENTED (event model + plugin migration; other subsystems
migrate opportunistically, never as a flag day).

## Format

One line per event, countable by category:

```text
diag|<kind>|<code>|<mod>|<detail>
```

- `kind`: discovery | validation | dependency | startup | script |
  bridge | plugin (`ttmod::to_string(DiagKind)` pins the names).
- `code`: stable kebab id (`rejected-no-native-runtime`, `initialized`,
  `failed-revoked`). Codes reuse the historical log words so old greps
  (rejected, initialized, failed, grants) keep hitting.
- `mod`: owning mod id, or empty.
- `detail`: human text; newlines flattened (single-line invariant).

Pinned by `tests/unit/test_diag.cpp` (exact format, empty mod, newline
flattening, kind names).

## Rules

- Prefer events over arbitrary strings for NEW diagnostics in the
  categories above. Do not rewrite old lines speculatively - migrate a
  TU when you touch it for other reasons (the plugin loader is the
  proof: reject/init/grants/revoke lines are events now).
- Plugin-authored text (`host->log`) stays free-form: the framework
  cannot structure what plugins write.
- Hook-path rule: emitting costs what the sink costs (today a
  log-file open/write/close per line), so hot paths must already be
  capped before emitting. See the scol/loadresource caps - PR 23's
  audit confirmed every hot caller is bounded.

## Grep recipes

```sh
cd "Minecraft - Story Mode"
awk '/TTMod framework v/{buf=""} {buf=buf $0 "\n"} END{printf "%s",buf}' logs/ttmod.log \
  | grep -E "^.*diag\|plugin\|"
awk '/TTMod framework v/{buf=""} {buf=buf $0 "\n"} END{printf "%s",buf}' logs/ttmod.log \
  | grep -E "diag\|(rejected|failed)"
```

## DO NOT CLAIM DONE

- Only the plugin loader emits events so far; discovery/validation/
  dependency/script/bridge lines are still free-form (migrate on touch).
- No aggregation or query API - the log file plus grep is the sink.
  A ring buffer or counter API is a separate proposal, not this page.
