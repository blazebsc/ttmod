# ADR-003: substitute at the setter, not the getter

- Status: accepted (2026-10-05)
- Context: `0x5684B0` (scolB) looked like the color setter and carried a
  substitution hook whose log lines suggested it worked. REA decompilation
  proved it is a property GETTER: it overwrites the probed buffer on
  success, so every `scolB-sub` line was theater.
- Decision: the substitution lives on `0x568430` (scol, the true setter,
  thiscall, `ret $0xc`), broad by necessity (descriptors are
  per-agent-instance and churn every rebuild), scoped by value shape
  (gray+bright) + theme gate. scolB hook removed.
- Consequences: verify hook placement by what the callee DOES with the
  buffer (read vs overwrite), never by log volume. A hook that fires often
  proves traffic, not effect.
