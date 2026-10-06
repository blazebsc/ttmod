# TTMod — Next Steps Roadmap

## Purpose

This document describes what should happen next in the TTMod refactor, what is already in place, the recommended implementation order, and the final architecture the project should converge on.

The goal is to finish the core refactor without prematurely implementing every runtime feature at once. The project should first establish a stable domain model, deterministic mod planning, explicit runtime ownership, and safe scripting boundaries. Native plugins, TTMod Lua/Luau, and direct Telltale Lua integration can then be built on top of those foundations.

## 1. Current State

The refactor has already completed most of the security/correctness foundation and a large part of the domain-model groundwork.

### Already implemented or substantially implemented
- C++20 core configuration with C11 support where required.
- CMake presets for Linux, Windows/MinGW, ASan, UBSan, and fuzzing.
- Strict compiler warnings and sanitizer coverage.
- Centralized ModId validation.
- Centralized mod-relative path validation.
- JSON parsing based on nlohmann::json rather than the old handwritten parser.
- Duplicate-key rejection and bounded JSON input.
- Safer file I/O and atomic writes.
- Result<T> / Error infrastructure.
- Strong ModId type.
- Version and VersionConstraint types with bounds checking.
- Manifest parsing/validation split.
- Strong runtime and permission enums.
- Per-runtime entrypoint representation.
- ModPlan and dependency-graph infrastructure.
- Transaction-oriented mod-cache extraction.
- Security tests and fuzzing infrastructure.
- ScriptVm abstraction that does not expose lua_State* as part of the generic VM interface.
- ScriptApiRegistry with revocation/tombstone behavior.
- Value representation and GameObjectHandle groundwork.
- Initial game Lua state registry.
- CI hardening and formatting/security checks.
- Native/game-Lua architecture documentation.

### Most important current architectural reality

The project is now in a transition state:

```text
Security / correctness foundation       mostly complete
Domain model                             close to complete
Runtime ownership                        started
Script abstraction                      started
Game Lua integration                     still profile/hook heavy
TTMod Lua backend                         exists for Lua 5.4
Luau backend                              not implemented yet
Native plugin ABI                         needs formalization
Cross-runtime bridge                     planned
```

The next phase should therefore focus on making the new abstractions authoritative rather than adding large new feature sets.

## 2. Priority System

### P0 — Do these before major runtime feature work

These are the remaining items that define whether the refactor has a stable core.
- Finish Result<T> migration.
- Finalize the Result access/error-handling contract.
- Finish ModId migration throughout the codebase.
- Finish Version migration throughout the codebase.
- Make the validated manifest model authoritative.
- Finish ModSource and discovery boundaries.
- Make ModPlan the single authoritative output of discovery/resolution.
- Establish runtime ownership and lifecycle boundaries.
- Establish game-thread dispatch rules for game-owned operations.
- Formalize the multi-VM separation.
- Do not skip these just because the current implementation can already load simple mods.

### P1 — Build the runtime architecture

After the core domain is stable:
- GameLuaRuntime
- game Lua state classification/lifecycle
- GameDispatcher
- per-mod script environments
- script API registry concurrency/lifetime cleanup
- robust Value marshaling
- GameObjectHandle validation
- native plugin ABI/lifetime model
- game profile/capability model
- hook hot-path cleanup
- telemetry/diagnostics boundaries
- package/cache/config cleanup

### P2 — Add the final modding capabilities

Only after the above is stable:
- TTMod Lua runtime polish
- Luau backend
- direct Telltale Lua entrypoints
- cross-runtime communication APIs
- native plugin Lua/Luau bindings
- richer game APIs
- mod UI integration

## 3. Immediate Milestone

### Stable Core Domain + Runtime Ownership

The next major milestone should leave TTMod with a clean, deterministic core where all major systems consume the same validated data and execution plan.

The intended flow is:

```text
Package / filesystem
        |
        v
    Discovery
        |
        v
 RawManifest / metadata
        |
        v
 Manifest validation
        |
        v
 Validated Mod model
        |
        v
 Dependency + conflict resolution
        |
        v
     ModPlan
        |
        +------------------+-------------------+
        |                  |                   |
        v                  v                   v
 Native runtime      TTMod VM runtime     Game Lua runtime
```

The key architectural rule is that discovery, validation, dependency resolution, and runtime execution should not each create their own slightly different interpretation of a mod.

## 4. Recommended PR Sequence

### PR 1 — Finish Result migration

**Branch:** `refactor/result-api`

**Tasks:**
- Replace remaining exception/string/bool-style error propagation where the new API is clearly superior.
- Make failure paths return Result<T> consistently in core infrastructure.
- Preserve useful contextual errors.
- Ensure callers cannot accidentally ignore important failures.
- Add tests for success/error propagation.

**Definition of done:**
- Core loading/parsing/validation/discovery paths consistently use Result.
- No subsystem has an ambiguous mix of ownership/error conventions without a reason.

### PR 2 — Finalize the Result access contract

**Branch:** `refactor/result-access`

**Decide and document the canonical access pattern for:**
- value()
- value_or(...)
- error()
- pointer/reference access where needed
- move extraction
- propagation between nested Result operations

The goal is to avoid every subsystem inventing its own style.

### PR 3 — Finish ModId migration

**Branch:** `refactor/core-mod-id`

**Tasks:**
- Replace remaining raw string IDs in authoritative internal APIs.
- Keep strings only at serialization/UI boundaries.
- Ensure dependency/conflict maps use ModId.
- Ensure hashing/comparison behavior is centralized.
- Add tests for equality, invalid IDs, normalization expectations, and serialization.

**Target rule:**

```text
JSON text -> ModId -> internal logic -> ModId -> JSON text
```

**not:**

```text
JSON text -> std::string -> everything
```

### PR 4 — Finish Version migration

**Branch:** `refactor/core-version`

**Tasks:**
- Replace remaining version strings in authoritative logic.
- Use VersionConstraint for compatibility checks.
- Audit numeric conversion and prefix parsing for overflow/edge cases.
- Add boundary tests for malformed and oversized versions.

### PR 5 — Make validated Manifest authoritative

**Branch:** `refactor/manifest-authoritative`

**The codebase should have one clear distinction:**

```text
RawManifest
    -> parse only
```

```text
validate_manifest(...)
    -> validate / normalize / convert
```

```text
ModManifest
    -> trusted domain object
```

After this PR, downstream systems should ideally consume ModManifest rather than repeatedly validating raw strings/JSON themselves.

**The validated manifest should own concepts such as:**
- identity
- presentation metadata
- compatibility
- dependencies
- conflicts
- overrides
- `permissions/capabilities`
- runtime declarations
- entrypoints

## 5. Mod Discovery and Planning

### PR 6 — Introduce/finish ModSource

**Branch:** `refactor/mod-source`

Separate where a mod came from from what the mod is.

**Potential sources include:**
- loose directory
- `package/archive`
- cache entry
- development/test source

This makes discovery and packaging rules explicit instead of spreading source-specific logic through loaders.

### PR 7 — Make discovery authoritative

**Branch:** `refactor/discovery`

Discovery should produce validated mod candidates and report structured problems.

It should not execute scripts, load plugins, or apply game hooks.

Conceptually:

```text
Filesystem/package inspection
        -> candidate
        -> parse
        -> validate
        -> discovered mod
```

### PR 8 — Make ModPlan the single execution plan

**Branch:** `refactor/modplan`

All runtime consumers should use the same plan.

**The plan should contain deterministic information such as:**
- load order
- enabled mods
- disabled mods
- blocked mods
- invalid mods
- skipped mods
- dependency failures
- `conflicts/problems`
- runtime entrypoints to consider

The native plugin loader, TTMod script loader, resource system, and UI integration should not independently resolve the mod graph.

### PR 9 — Finalize dependency execution semantics

**Branch:** `refactor/dependency-execution`

**Specify exactly:**
- dependency ordering
- conflict ordering
- override precedence
- cycle reporting
- disabled dependency behavior
- optional dependencies
- failure propagation

The same semantics must apply to all runtimes.

## 6. Runtime Ownership

### PR 10 — Profile-owned capabilities

**Branch:** `refactor/profile-capabilities`

Create a capability model that answers what the current game profile can safely expose.

**Example capability identifiers:**
- game.read
- game.write
- game.events
- ui
- resources
- filesystem.read
- filesystem.write
- mods.read
- mods.write
- game.lua
- game.memory
- hooks
- native

Permissions belong to the mod manifest; capabilities belong to what TTMod/the current game profile actually supports.

A requested permission should therefore pass through a policy check before being granted.

### PR 11 — Runtime owner

**Branch:** `refactor/runtime`

Create an explicit runtime owner responsible for lifecycle and coordination.

**It should own or coordinate:**
- mod lifecycle
- runtime startup/shutdown
- mod plan
- script runtimes
- native plugins
- game integration
- dispatchers
- diagnostics

The runtime owner should not become a giant god-object. Prefer narrow subsystem interfaces behind it.

### PR 12 — Define runtime modes

**Branch:** `refactor/runtime-mode`

**Define operational modes where needed, for example:**
- normal
- safe
- development
- `headless/test`

Modes should control policy and available capabilities, not mutate the fundamental domain model.

## 7. Game Lua Runtime

### PR 13 — Introduce GameLuaRuntime

**Branch:** `refactor/game-lua-runtime`

Treat the game's Lua implementation as a separate runtime owned by the game.

Do not model it as a single permanent global lua_State*.

**The conceptual state model is:**

```text
GameLuaRuntime
├── EngineState
├── MenuState
└── OtherState[]
```

The exact classification is game/profile dependent.

**Responsibilities:**
- discover/register game Lua states
- classify states
- track lifetime
- enforce ownership rules
- expose safe integration points
- coordinate execution on the correct game thread

TTMod must not casually destroy game-owned Lua states.

### PR 14 — Introduce GameDispatcher

**Branch:** `refactor/game-dispatcher`

Create an explicit mechanism for scheduling work onto the game thread or other required execution context.

This becomes essential once native plugins and TTMod scripts can request game operations asynchronously.

**Rules should include:**
- which APIs require the game thread
- whether operations can be queued
- ordering guarantees
- failure behavior when the game state disappears
- shutdown behavior

### PR 15 — Harden game Lua hooks

**Branch:** `refactor/game-lua-hooks`

Keep the profile-specific/hooking code isolated from generic runtime interfaces.

**The hook layer should be responsible for:**
- calling convention correctness
- ABI correctness
- minimal hook work
- state registration
- narrow bridge operations
- restoration/chaining behavior

Avoid putting policy, dependency resolution, or heavy scripting work directly inside the hook callback.

## 8. TTMod VM and Script Isolation

### PR 16 — Enforce the two-VM model

**Branch:** `refactor/two-vm-boundary`

**The architecture must explicitly enforce:**
- TTMod VM != Game Lua Runtime

The TTMod VM is owned by TTMod.

The Game Lua Runtime is owned by the game.

They can communicate through controlled bridges but must never be treated as one runtime.

### PR 17 — Per-mod environments

**Branch:** `refactor/script-environments`

Use one TTMod VM with separate environments for each mod rather than creating a separate VM for every mod by default.

Conceptually:

```text
TTMod Lua/Luau VM
├── Mod A environment
├── Mod B environment
├── Mod C environment
└── shared TTMod runtime services
```

This provides isolation without multiplying VM instances unnecessarily.

A separate VM per mod should only be considered later if profiling demonstrates a real benefit.

### PR 18 — Fix ScriptApiRegistry lifetime/concurrency contract

**Branch:** `refactor/script-api-registry`

The current tombstone/revocation idea is good for preventing dangling Lua closures, but the lookup/invocation contract should be tightened.

Avoid returning a pointer/reference whose lifetime is only protected until a mutex is unlocked.

**Prefer designs such as:**
- registry.invoke(token, args)

or return a stable/copyable API entry object.

Add multithreaded tests if registry access is intentionally concurrent.

### PR 19 — Finalize Value and GameObjectHandle

**Branch:** `refactor/script-values`

**Rules:**
- no raw lua_State* in generic Value
- no sharing Lua objects between VMs
- no native pointer exposed as a normal game object identity
- handles must be validated against authoritative native state
- generation counters prevent stale-handle reuse

Treat the handle as opaque from the script author's perspective.

Readable fields may exist for serialization/debugging, but validity must still be checked natively.

## 9. Native Plugin ABI

### PR 20 — Formalize native plugin ABI

**Branch:** `refactor/plugin-abi`

Native mods should use a stable C-compatible boundary even though the main implementation can remain C++.

Conceptually:

extern "C" TTModPluginApi* ttmod_plugin_init(const TTModHostApi* host);

The exact ABI must be documented and versioned before external plugin authors depend on it.

**The ABI should define:**
- versioning
- initialization
- shutdown
- capability negotiation
- callbacks
- error reporting
- memory ownership
- string ownership
- thread rules
- logging
- game dispatch
- script binding registration

Do not expose C++ standard-library types across the ABI.

### PR 21 — Native plugin lifetime model

**Branch:** `refactor/plugin-lifetime`

**Define precisely:**
- when a plugin is loaded
- when initialization happens
- when callbacks become valid
- shutdown ordering
- hot unload support or explicit non-support
- what happens when another subsystem still holds a callback

A safe first release may intentionally forbid unloading live native plugins.

## 10. Game Profiles and Hook Isolation

### PR 22 — Game profile abstraction

**Branch:** `refactor/game-profiles`

Move game-specific knowledge behind a profile boundary.

**A profile should describe things like:**
- executable/build identity
- known addresses/signatures
- Lua ABI details
- script/resource paths
- supported capabilities
- hook availability

Do not spread title/build-specific constants throughout the core runtime.

### PR 23 — Hook hot-path optimization

**Branch:** `perf/hook-hot-path`

The current hook paths should be kept functionally correct first, then optimized.

**Avoid expensive work such as:**
- repeated filesystem parsing
- unnecessary logging
- repeated path resolution
- unnecessary string conversion
- repeated allocations

Precompute resolution data where possible and move diagnostics/heavy processing outside the hot path.

Do not optimize based on guesswork; profile first.

## 11. Telemetry and Diagnostics

### PR 24 — Structured runtime diagnostics

**Branch:** `refactor/runtime-telemetry`

**Create a consistent diagnostic model for:**
- mod discovery problems
- validation failures
- dependency failures
- runtime startup failures
- script errors
- game Lua bridge failures
- native plugin failures

Prefer structured events over arbitrary logging strings.

The hook path should not perform synchronous heavy telemetry work unless it is proven safe and cheap.

## 12. Cache, Packages, and Configuration

### PR 25 — Cache identity/documentation alignment

**Branch:** `refactor/cache-identity`

The implementation currently uses content-related identity information including a non-cryptographic content hash, file size, schema/package information.

The documentation should match the implementation exactly.

**Important rule:**

The current hash is a cache identity mechanism, not a cryptographic integrity/authentication guarantee.

### PR 26 — Stronger cache replacement semantics

**Branch:** `refactor/cache-atomic-replace`

The transactional cache approach is good, but crash behavior should be reviewed carefully.

**The desired invariant is:**

```text
old valid cache
        OR
new valid cache
```

rather than leaving the system with neither after a crash at the wrong point.

Platform-specific replacement semantics may need to differ between Windows and POSIX systems.

### PR 27 — Package I/O hardening

**Branch:** `refactor/package-safety`

**Audit package extraction and inspection for:**
- per-file size limits
- total archive limits
- path traversal
- symlink/reparse-point behavior
- decompression bombs
- malformed archives
- TOCTOU between inspection and extraction

Keep package inspection and extraction policy centralized.

### PR 28 — Configuration model cleanup

**Branch:** `refactor/config`

**Continue the configuration hardening work:**
- strict type validation
- bounded values
- explicit defaults
- atomic writes
- clear schema/versioning

Configuration should not be allowed to bypass the same safety principles as mod manifests.

## 13. UI/Menu Architecture

### PR 29 — Separate MenuModel from runtime internals

**Branch:** `refactor/menu-model`

Keep menu/UI state independent from native/game-runtime implementation details.

**UI should consume structured information such as:**
- discovered mods
- enabled/disabled state
- load failures
- warnings
- capabilities
- runtime status

The menu should not directly own mod loading logic.

## 14. TTMod Lua

### PR 30 — Stabilize TTMod Lua backend

**Branch:** `feat/ttmod-lua`

The Lua 5.4 backend already exists and should now be treated as a consumer of the finalized ScriptVm architecture rather than the architecture itself.

**Tasks:**
- finalize lifecycle
- create per-mod environments
- bind the stable ttmod.* API
- enforce capabilities
- define error policy
- define memory/execution limits where appropriate
- add stack-balance tests
- add API compatibility tests

## 15. Luau

### PR 31 — Add Luau backend

**Branch:** `feat/ttmod-luau`

Do not start this until the common script API and value model are stable.

The Luau backend should implement the same conceptual ScriptVm contract as Lua.

The target is:

```text
Lua 5.4 ----+
            +--> common TTMod scripting API
Luau -------+
```

Avoid adding Lua-specific behavior to the public API unless it has an explicitly justified compatibility layer.

## 16. Direct Telltale Lua Mods

### PR 32 — Telltale Lua entrypoints

**Branch:** `feat/telltale-lua-mods`

Support a declared telltale-lua runtime for advanced mods.

Example manifest concept:

```json
{
  "runtimes": [
    "telltale-lua"
  ],
  "entrypoints": {
    "telltale-lua": "game.lua"
  }
}
```

**Important restrictions:**
- game-owned Lua state
- profile/build dependent
- game-thread rules apply
- no generic raw lua_State* in normal TTMod scripts
- TTMod does not replace the game's Lua runtime

This should be documented as an advanced capability rather than the default scripting path.

## 17. Cross-Runtime Bridge

### PR 33 — Controlled runtime communication

**Branch:** `feat/cross-runtime-bridge`

**A mod should be able to combine runtimes, for example:**

```text
UltimateMod/
├── manifest.json
├── plugin.dll
├── main.luau
└── game.lua
```

Use a native bridge and explicit dispatch/marshaling rather than sharing runtime objects.

**Communication mechanisms should be based on concepts such as:**
- events
- commands
- serialized/copyable values
- GameObjectHandle
- game-thread dispatch
- host services

Never share raw Lua tables or raw lua_State* values between runtimes.

## 18. Native Plugin Script Bindings

### PR 34 — Native registration of Lua/Luau APIs

**Branch:** `feat/native-script-bindings`

Allow native plugins to register additional APIs into TTMod Lua/Luau through a stable host interface.

Example concept:

```text
Native plugin
      |
      v
TTMod host API
      |
      v
Script API registry
      |
      +----> Lua
      |
      +----> Luau
```

This is one of the most powerful combinations in the final architecture, but it should only be added after plugin ABI and script registry lifetimes are stable.

## 19. Testing Roadmap

Testing should grow with each subsystem rather than being deferred until the end.

### Core unit tests

**Cover:**
- ModId
- Version
- VersionConstraint
- path validation
- manifest validation
- `permissions/capabilities`
- dependency resolution
- conflict handling
- ModPlan
- cache identity

### Security tests

**Continue testing:**
- path traversal
- malformed JSON
- duplicate keys
- oversized JSON
- integer overflow
- archive bombs
- invalid compressed paths
- symlink/reparse behavior
- cache corruption

### Fuzzing

**Maintain fuzz targets for:**
- manifest parsing
- manifest validation
- path validation
- package/archive handling
- version parsing
- script binding inputs

Fuzzing should remain part of normal development, not just a one-time security exercise.

### Runtime tests

**Add tests for:**
- TTMod VM lifecycle
- per-mod environment isolation
- script API revocation
- value marshaling
- GameObjectHandle stale generation rejection
- game dispatcher behavior
- game Lua state registration/lifecycle
- game-thread enforcement

### Integration tests

**Test a complete mod through:**

```text
package
 -> discovery
 -> validation
 -> dependency resolution
 -> ModPlan
 -> runtime selection
 -> startup
 -> shutdown
```

Include mixed-runtime mods once the three runtime paths are implemented.

### ABI tests

**Once the native ABI is finalized:**
- ABI compatibility tests
- struct-size/version checks
- host/plugin negotiation tests
- callback lifetime tests
- string/memory ownership tests
- Windows x86 plugin loading tests

## 20. CI Roadmap

Keep the current hardened matrix and expand it as runtime features arrive.

Recommended baseline:

```text
Linux + GCC
Linux + Clang
Linux + ASan
Linux + UBSan
Linux + fuzz target build
Windows x86 + MinGW
Formatting/security validation
```

Later add runtime-specific jobs as dependencies allow.

The Windows x86 target is particularly important because the game/plugin ABI ultimately needs to match the target process architecture.

## 21. Documentation Roadmap

The architecture documentation should stay ahead of or exactly aligned with the implementation.

Required long-term docs include:

```text
REFACTOR_PLAN.md
    broad refactor strategy
TTMOD_MODDING_ARCHITECTURE.md
    finalized three-runtime architecture
TTMOD_NEXT_STEPS.md
    current implementation roadmap
docs/runtime/mod-packages.md
    package/cache behavior
docs/runtime/permissions.md
    permissions and capabilities
docs/runtime/lifecycle.md
    runtime and mod lifecycle
docs/runtime/game-lua.md
    game-owned Lua integration
docs/runtime/native-abi.md
    native plugin ABI
docs/runtime/scripting.md
    Lua/Luau API
```

Documentation must distinguish implementation status.

**Recommended labels:**
- Implemented
- In Progress
- Planned
- Experimental
- Profile-dependent

This prevents contributors or AI agents from assuming a conceptual subsystem already exists when it is only specified.

## 22. Git and PR Workflow

For the rest of the refactor, prefer small focused PRs over giant rewrites.

**A good PR should generally contain:**
- 1 architectural change
- tests
- documentation updates
- no unrelated cleanup

**Good examples:**
- `refactor/result-api`
- `refactor/mod-id`
- `refactor/version-types`
- `refactor/manifest-domain`
- `refactor/mod-source`
- `refactor/discovery`
- `refactor/modplan`
- `refactor/runtime`
- `refactor/game-lua-runtime`
- `refactor/two-lua-vms`
- `refactor/plugin-abi`
- `perf/hook-hot-path`

**Avoid combining:**
- new runtime + ABI redesign + package rewrite + UI rewrite

into one PR.

This makes regressions easier to identify and lets the project keep building throughout the refactor.

## 23. Things to Deliberately Postpone

Do not let these derail the core refactor unless there is a concrete requirement.

### Do not change the language standard just because the refactor is large

Keep C++20 for now.

### Do not replace proven low-level dependencies without a reason

Do not replace MinHook, miniz, or equivalent infrastructure simply for novelty.

### Do not replace the game's Lua implementation

TTMod should integrate with the Telltale runtime rather than attempting to replace it.

### Do not build a speculative universal game abstraction too early

Game profiles should be narrow and evidence-based.

### Do not implement Luau before the common scripting layer is stable

Otherwise Lua and Luau behavior will drift apart.

### Do not build separate VMs per mod by default

Start with one TTMod-owned VM and per-mod environments.

### Do not optimize hook paths before profiling

Correctness, call-convention safety, and deterministic behavior come first.

## 24. Final Target Architecture

**The final system should look conceptually like this:**

```text
                         TTMod
                           |
          +----------------+----------------+
          |                |                |
          v                v                v
     Mod Manager       Runtime Owner     Diagnostics
          |                |
          v                +------------------------------+
       Discovery           |              |              |
          |                v              v              v
          v           Native Runtime   TTMod VM      Game Lua Runtime
   Manifest/Plan          |              |               |
          |                |              |               +-- Engine State
          |                |              |               +-- Menu State
          |                |              |               +-- Other States
          |                |              |
          |                |              +-- Mod A env
          |                |              +-- Mod B env
          |                |              +-- Mod C env
          |                |
          |                +-- Native plugins
          |
          +-----------------------------------------------+
                                                          |
                                                          v
                                                    Game Dispatcher
                                                          |
                                                          v
                                                   Game-thread work
```

The three modding paths are:

- 1. Native
   - in-process native plugin
   - stable C ABI
   - maximum power

- 2. TTMod VM
   - Lua 5.4
   - Luau
   - recommended general scripting path

- 3. Game VM
   - Telltale Lua
   - game-owned
   - advanced/profile-dependent

A single mod may use one, two, or all three.

## 25. Final Runtime Relationship

The most important conceptual boundary is:

```text
                  +-----------------------+
                  |       TTMod VM        |
                  |     Lua / Luau        |
                  +-----------+-----------+
                              |
                         controlled bridge
                              |
                              v
                  +-----------------------+
                  |   Game Lua Runtime    |
                  |  Telltale-owned Lua   |
                  +-----------------------+
```

**Native code may sit across both sides as a controlled integration layer:**

```text
             Native Plugin / TTMod Core
                    /            \
                   /              \
                  v                v
             TTMod VM        Game Lua Runtime
```

But each runtime keeps its own ownership, lifecycle, state, and object model.

## 26. Immediate Next Three PRs

**After the current work, the strongest next sequence is:**
- 1. refactor(result): finish Result/Error migration
- 2. refactor(result): finalize Result access contract
- 3. refactor(core): finish ModId migration
**Then:**
- 4. finish Version migration
- 5. make validated Manifest authoritative
- 6. ModSource
- 7. discovery
- 8. ModPlan integration

Only after that should the project move deeply into runtime ownership.

## 27. Definition of Success for the Refactor

The refactor is successful when the following are true:
- Parsing produces validated domain objects.
- Internal systems use strong types instead of raw strings for core concepts.
- One authoritative ModPlan controls execution order and enablement.
- Permissions and game-profile capabilities are separate concepts.
- Runtime ownership is explicit.
- TTMod's VM is clearly separate from the game's Lua runtime.
- Game Lua state ownership and thread rules are explicit.
- Native plugins cross a documented C-compatible ABI.
- Lua and Luau use the same conceptual TTMod API.
- Direct Telltale Lua is available as an advanced runtime path.
- Multiple runtimes can coexist inside one mod without sharing unsafe runtime state.
- Cross-runtime communication uses explicit marshaling/handles/dispatch.
- Package/cache/config operations are bounded and transactional where required.
- CI continuously tests the supported build matrix.
- Architecture docs accurately describe what exists versus what is planned.

At that point, TTMod is not just a collection of mod-loading features; it has a coherent runtime architecture that can support increasingly powerful native, TTMod-scripted, and direct game-Lua mods without collapsing those concerns into one unsafe subsystem.
