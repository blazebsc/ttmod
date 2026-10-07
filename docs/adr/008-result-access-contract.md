# ADR-008: Result access is explicit and ref-qualified

**Status:** closed (Roadmap PR 2, 2026-10-06)

## Context

Core callers sometimes moved a value with `std::move(r.value())`. This moves
the contained value only after calling the lvalue accessor, which makes
move-only values awkward and hides the intended extraction from the `Result`
API. Nested failures also acquired caller-specific operation names through
manual field mutation, as in `apply_enabled_change`.

Without a shared contract, each subsystem can invent a different style for
checking, borrowing, moving, and re-attributing a result.

## Decision

Keep access explicit and make it reflect the value category of the result:

- Check `ok()` before accessing `value()` in the same scope.
- Borrow through `value() const&` / `value() &`; extract through
  `std::move(r).value()`.
- Do not write `std::move(r.value())`: move from the `Result`, not from the
  lvalue accessor's reference.
- Use `try_value()` for pointer-style access without copying.
- Use `value_or()` only when the fallback is semantically correct; its
  rvalue overload moves an available value out.
- Forward nested `Error` values unchanged by default. At a public boundary
  that names its own operation, use `with_operation`.
- Do not add implicit or operator access (`operator bool`, `operator*`, or
  `operator->`); callers must make success checks and access visible.

Calling `value()` on failure or `error()` on success remains a caller bug and
aborts.

## Why not operator access

`Result` is used at trust boundaries where a missed failure must stay visible.
Implicit conversion, dereference, or arrow syntax would make access look
ordinary and weaken the explicit success check without solving a demonstrated
need.

## Consequences

Move-only values can be extracted directly, while lvalue access remains a
borrow. Nested errors retain their original details unless a public API
deliberately names its own operation. This decision can be reopened only by a
concrete use case that cannot be expressed by the explicit accessors without
weakening those guarantees.
