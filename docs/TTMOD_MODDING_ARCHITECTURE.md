# TTMod Multi-Runtime Modding Architecture

**Status:** Proposed / Architecture Specification  
**Scope:** TTMod runtime and modding model  
**Primary goal:** Define exactly how TTMod supports native mods, TTMod-managed scripting, and direct game/Telltale scripting.

---

## 1. Overview

TTMod is intended to be a **multi-runtime modding framework** rather than a single scripting system.

A mod may use one or more of three modding environments:

1. **Native runtime**
   - Native code loaded into the game's process.
   - Publicly exposed through a stable C-compatible ABI.
   - Intended for maximum control.
   - C, C++, Rust, Zig, Assembly, and other languages capable of implementing the ABI may be used.

2. **TTMod scripting runtime**
   - A scripting VM created and owned by TTMod.
   - Supports Lua and Luau.
   - Intended to be the normal, stable scripting environment for mod authors.
   - The VM is independent of the game's scripting VM.

3. **Game/Telltale scripting runtime**
   - The Lua runtime already created and owned by the game.
   - TTMod integrates with it rather than replacing it.
   - Intended for advanced mods that need direct access to Telltale's existing Lua environment, globals, userdata, and script APIs.
   - This runtime is Lua-based and is not automatically Luau.

The three environments are connected by TTMod's native runtime and public APIs, but their ownership and execution models remain separate.

---

# 2. Core architecture

The high-level model is:

```text
                                  TTMod
                                    │
          ┌─────────────────────────┼─────────────────────────┐
          │                         │                         │
          ▼                         ▼                         ▼
   Native Mod Runtime       TTMod Script Runtime        Game Script Runtime
          │                         │                         │
      C / C++ / Rust             Lua / Luau             Telltale Lua
      Zig / Assembly                  │                         │
          │                           │                         │
          └───────────────┬───────────┴───────────────┬─────────┘
                          │                           │
                          ▼                           ▼
                    TTMod Native Runtime       Game Lua Bridge
                          │                           │
                          └──────────────┬────────────┘
                                         │
                                         ▼
                                  Telltale Game
```

The fundamental rule is:

> **TTMod owns the TTMod scripting VM. The game owns its Lua runtime. Neither VM owns the other.**

Native code is the privileged bridge between TTMod and the game.

---

# 3. The three modding environments

## 3.1 Native modding

Native mods are code modules executing in the game's process.

Typical package:

```text
MyNativeMod/
├── manifest.json
└── plugin.dll
```

The module exports the TTMod native plugin entry point.

Conceptually:

```text
plugin.dll
    │
    ▼
TTMod Native Loader
    │
    ▼
C-compatible TTMod ABI
    │
    ▼
TTMod Runtime
    │
    ▼
Game
```

### Supported implementation languages

TTMod should formally specify a **C ABI**, not a C++ ABI.

This means the language used to implement a plugin is intentionally not part of the ABI.

Possible implementation languages include:

```text
C
C++
Rust
Zig
Assembly
D
Nim
and other languages capable of exposing the required C ABI
```

The plugin only needs to satisfy the required calling conventions, struct layouts, symbol/export rules, and ABI version.

### Native mod power

Native plugins are the maximum-power layer.

Depending on the permissions and APIs exposed by the current game profile, a native plugin may be able to:

- install hooks
- call supported game functions
- inspect game state
- access selected native engine systems
- interact with game memory
- interact with Telltale Lua
- interact with TTMod's event system
- register scripting APIs
- create custom native systems
- provide custom UI functionality
- communicate with other native or scripted mods

Native plugins are **not sandboxed**.

Installing a native mod should therefore be treated as executing arbitrary native code in the game process.

---

# 4. TTMod scripting runtime

The TTMod scripting runtime is a scripting environment **created and controlled by TTMod**.

It is separate from the game's Lua implementation.

The architecture is:

```text
TTMod Startup
    │
    ▼
Create TTMod Script Runtime
    │
    ├── Lua VM
    └── Luau VM
           │
           ▼
       Load Mod Scripts
```

## 4.1 Ownership

TTMod owns:

- VM creation
- VM destruction
- script loading
- module loading
- error handling
- execution policy
- memory policy
- exposed APIs
- scheduling
- mod lifecycle
- script isolation

The game does not own or manage this VM.

---

## 4.2 Lua and Luau

The TTMod scripting runtime supports:

```text
Lua
Luau
```

These are scripting targets for **TTMod's own VM**, not replacements for the game's VM.

Example Lua mod:

```lua
ttmod.log("Hello from TTMod Lua")

ttmod.events.on("game_start", function()
    ttmod.log("The game started")
end)
```

Example Luau mod:

```lua
local function announce(message: string)
    ttmod.log(message)
end

ttmod.events.on("game_start", function()
    announce("Hello from TTMod Luau")
end)
```

The design goal is that both languages use the same conceptual TTMod API.

---

# 5. Game/Telltale scripting runtime

The game runtime is fundamentally different.

The game creates its own Lua state(s).

TTMod does **not** create these states and should not claim ownership of them.

Conceptually:

```text
MCSM Process
└── Telltale Lua Runtime
    ├── Engine Lua State
    ├── Menu Lua State
    └── Other / future Lua States
```

The exact number and role of states is game/profile dependent.

TTMod should therefore model this as a **Game Lua Runtime** containing zero or more identified Lua states rather than assuming there is only one global `lua_State*`.

---

# 6. Why the game Lua runtime is different

A normal embedded Lua application usually does this:

```cpp
lua_State* L = luaL_newstate();
luaL_openlibs(L);
```

The host application creates, owns, uses, and destroys the state.

TTMod does not work that way for the game runtime.

Instead:

```text
Game
  │
  │ creates Lua state
  ▼
lua_newstate(...)
  │
  ▼
TTMod hook observes creation
  │
  ▼
original game state returned
  │
  ▼
TTMod may register/inject supported integrations
```

The game remains the owner.

---

# 7. Game Lua bridge

The current TTMod approach uses a hook around Lua state creation to discover states as they are created.

Conceptually:

```text
Game thread
    │
    │ calls lua_newstate
    ▼
TTMod detour
    │
    ├── call original lua_newstate
    │
    ├── receive lua_State*
    │
    ├── identify/record state
    │
    ├── optionally perform tightly-scoped setup
    │
    └── return original state
    │
    ▼
Game continues
```

This is an integration boundary, not a replacement Lua runtime.

---

# 8. Game Lua state ownership rule

The fundamental rule is:

```text
Game Lua State
    ↓
Owned by game

TTMod Lua State
    ↓
Owned by TTMod
```

TTMod may integrate with the game state, but must not treat it as a TTMod-owned object.

In particular, TTMod should not:

- call `lua_close` on a game state
- destroy a game state
- assume TTMod owns the allocator
- retain unsafe references indefinitely
- call into the state from an arbitrary TTMod worker thread
- expose raw `lua_State*` handles to ordinary mod scripts

---

# 9. Current game-Lua execution model

The current bridge deliberately uses a narrow execution window.

The important safety assumptions are:

```text
1. The original lua_newstate call occurs on the game's thread.
2. The state has just been created.
3. No normal game Lua work is running on the new state yet.
4. TTMod can execute tightly-scoped integration code.
5. The Lua stack is balanced before returning to the game.
6. TTMod does not arbitrarily call that state forever from a framework thread.
```

This is significantly safer than creating a TTMod worker thread that continuously calls into the game's Lua state.

---

# 10. Two Lua worlds

TTMod intentionally has two independent scripting worlds:

```text
┌────────────────────────────┐
│       TTMod Lua World      │
│                            │
│ TTMod-owned VM             │
│ Lua / Luau                 │
│ Mod scripts                │
│ TTMod API                  │
└─────────────┬──────────────┘
              │
              │ Native bridge
              │
┌─────────────▼──────────────┐
│      Game Lua World        │
│                            │
│ Game-owned VM/runtime      │
│ Telltale Lua               │
│ Engine scripts             │
│ Menu scripts               │
│ Telltale globals/userdata  │
└────────────────────────────┘
```

These worlds should not directly share Lua objects.

---

# 11. Cross-VM communication

Communication between VMs should occur through explicit TTMod native APIs and value conversion.

Do **not** pass raw Lua VM objects directly between the VMs.

For example:

```text
Game Lua table
    │
    ▼
Native representation
    │
    ▼
TTMod API
    │
    ▼
TTMod Lua table
```

Basic value conversions may include:

```text
nil       → nil
boolean   → boolean
number    → number
string    → string
array     → array
object    → table/object representation
```

Game-specific userdata should not automatically become transferable userdata.

Instead, TTMod should expose safe/stable representations such as:

```text
Game userdata
    ↓
opaque GameObject handle
    ↓
TTMod API
```

or:

```text
Game userdata
    ↓
GameObjectData
    ↓
TTMod Lua
```

depending on the API.

---

# 12. Game-thread dispatch

A TTMod script should not automatically be allowed to call arbitrary game Lua operations from any thread.

A recommended architecture is:

```text
TTMod Script Thread
        │
        │ request
        ▼
Game Dispatcher / Command Queue
        │
        ▼
Game Thread
        │
        ├── Native game function
        └── Game Lua state
        │
        ▼
Result
        │
        ▼
TTMod VM
```

This creates one controlled point for:

- thread validation
- state validation
- scheduling
- synchronization
- result marshaling
- game lifecycle checks

---

# 13. TTMod API

The public TTMod API should be the stable abstraction shared by:

- TTMod Lua
- TTMod Luau
- native plugins

Possible top-level namespaces:

```text
ttmod
├── game
├── events
├── mods
├── config
├── ui
├── resources
├── logging
└── native
```

Example:

```lua
ttmod.events.on("game_start", function()
    local player = ttmod.game.get_player()
    ttmod.log(player.name)
end)
```

The implementation behind `ttmod.game.get_player()` is intentionally hidden.

It may use:

```text
native engine function
 game Lua
memory lookup
profile-specific hook
```

without changing the public API.

---

# 14. Telltale-specific API

Direct game-Lua functionality should be clearly distinguished from the stable TTMod API.

A possible organization is:

```text
ttmod.telltale
```

for explicitly advanced functionality.

For example:

```lua
ttmod.telltale.menu(...)
ttmod.telltale.lua(...)
```

This namespace should communicate that the API is:

- game-specific
- profile-dependent
- potentially unstable
- more dangerous
- more tightly coupled to reverse-engineered internals

Not every game or profile will necessarily provide every Telltale capability.

---

# 15. Native API

Native plugins should receive a versioned C-compatible API.

Conceptually:

```c
struct ttmod_host {
    uint32_t struct_size;
    uint32_t api_version;

    /* API function pointers */
};
```

The plugin entry point should be C-compatible, for example:

```c
int ttmod_plugin_init(const ttmod_host* host);
```

The exact ABI is versioned independently from the scripting APIs.

---

# 16. Why the ABI should be C

A C ABI allows native mods to be created in many languages.

The interface is:

```text
C ABI
  ↑
  ├── C
  ├── C++
  ├── Rust
  ├── Zig
  ├── Assembly
  ├── D
  ├── Nim
  └── other compatible languages
```

The plugin ABI should not expose C++ standard-library types such as:

```text
std::string
std::vector
std::map
std::optional
```

through the stable interface.

The ABI should use stable C-compatible representations.

---

# 17. Native module packaging

A native mod will generally result in a native module such as:

```text
plugin.dll
```

The mod author may write:

```text
C
C++
Rust
Zig
etc.
```

The language is an authoring choice.

The runtime only sees the resulting native ABI-compatible module.

Future TTMod tooling may compile native source for the author, but the runtime still ultimately needs native machine code loaded into the game process.

---

# 18. Script mod packaging

A TTMod Lua mod may look like:

```text
MyLuaMod/
├── manifest.json
└── main.lua
```

A Luau mod:

```text
MyLuauMod/
├── manifest.json
└── main.luau
```

A direct Telltale-Lua mod may look like:

```text
MyGameLuaMod/
├── manifest.json
└── game.lua
```

The manifest determines which runtime the entry point belongs to.

---

# 19. Multi-runtime mods

A single mod may use multiple runtimes.

Example:

```text
UltimateMod/
├── manifest.json
├── main.luau
├── game.lua
└── plugin.dll
```

Possible manifest concept:

```json
{
    "id": "ultimate-mod",
    "version": "1.0.0",
    "runtimes": [
        "native",
        "luau",
        "telltale-lua"
    ]
}
```

The three components then serve different purposes:

```text
main.luau
    ↓
TTMod VM

game.lua
    ↓
Game Lua Runtime

plugin.dll
    ↓
Native Runtime
```

This is the maximum-capability mod model.

---

# 20. Runtime selection

Suggested runtime identifiers:

```text
lua
luau
telltale-lua
native
```

A mod may declare one or more.

Examples:

```json
{
    "runtimes": ["luau"]
}
```

```json
{
    "runtimes": ["lua"]
}
```

```json
{
    "runtimes": ["telltale-lua"]
}
```

```json
{
    "runtimes": ["native"]
}
```

```json
{
    "runtimes": ["native", "luau", "telltale-lua"]
}
```

The exact manifest syntax is subject to the final manifest/schema design.

---

# 21. Capability and permission model

Runtime selection and permissions should be separate concepts.

A mod saying:

```json
"runtimes": ["luau"]
```

means it needs a Luau execution environment.

It should not automatically receive unrestricted native or Telltale access.

Possible capabilities:

```text
game.read
game.write
game.events
ui
resources
filesystem.read
filesystem.write
mods.read
mods.write
game.lua
game.memory
hooks
native
```

A high-power mod might request:

```json
{
    "runtimes": [
        "native",
        "luau",
        "telltale-lua"
    ],
    "permissions": [
        "game.read",
        "game.write",
        "game.lua",
        "game.memory",
        "hooks"
    ]
}
```

The permission system is primarily an explicit declaration and UX boundary.

Native code remains trusted process code and cannot be made safely sandboxed merely by declaring permissions.

---

# 22. Security model

The trust hierarchy should be explicit.

## TTMod Lua / Luau

Potentially controlled by TTMod.

TTMod can impose:

- API restrictions
- filesystem restrictions
- execution limits
- memory limits
- module restrictions
- event restrictions
- error isolation

The exact sandbox policy depends on the chosen VM implementation.

## Telltale Lua

Not a true sandbox.

It executes in the game's scripting environment and can interact with game-provided APIs and objects.

Treat this as advanced/trusted scripting.

## Native

Not sandboxed.

A native plugin can execute arbitrary native code in the process.

Users should be warned accordingly.

---

# 23. VM isolation

The two scripting worlds must not share ownership.

Bad architecture:

```text
TTMod VM
    │
    └── shares lua_State with game
```

Correct architecture:

```text
TTMod VM
    │
    └── TTMod-owned state

Game Lua Runtime
    │
    ├── Engine state
    ├── Menu state
    └── Other states
```

Communication goes through:

```text
Native bridge
+
marshaled values
+
explicit commands
```

---

# 24. Why not replace the game's Lua with Luau

TTMod should not require replacing the game's Lua implementation.

Replacing the game VM would introduce compatibility requirements for:

- Lua C API behavior
- stack semantics
- registry behavior
- userdata
- metatables
- coroutines
- allocator behavior
- calling conventions
- internal runtime assumptions
- game-specific extensions
- engine-specific bytecode/script behavior

The game may depend on behavior that is not represented by the generic Lua language specification.

Therefore:

```text
TTMod VM → Luau
Game VM  → existing Telltale Lua
```

is the preferred architecture.

---

# 25. Optional Luau-to-game-Lua tooling

A future toolchain could optionally support a constrained workflow where a developer writes a Luau-compatible subset and transforms it into code that can run in the game's Lua implementation.

This should not be described as "the game is running Luau."

Instead:

```text
Luau-compatible source
        ↓
optional transformer/compiler
        ↓
game-compatible Lua source
        ↓
Telltale Lua
```

This is optional tooling and must only claim compatibility for the supported language subset.

---

# 26. Public API design rule

The stable public API should describe **what the mod wants to do**, not how the game internally implements it.

Bad:

```lua
ttmod.lua_state.call_function(...)
```

Better:

```lua
ttmod.game.get_player()
```

The implementation can change from:


```text
Telltale Lua
```

to:

```text
native engine call
```

without changing the mod API.

This is essential for supporting additional Telltale games and different game builds.

---

# 27. Game profiles

Game support should be profile based.

Conceptually:

```text
GameProfile
├── game identity
├── version/build identity
├── architecture
├── executable identity
├── capabilities
├── native addresses/signatures
├── hook definitions
├── Telltale Lua integration
└── game-specific limitations
```

Generic TTMod code should not contain large numbers of hardcoded checks such as:

```cpp
if (profile == "mcsm1_pc_x86") {
    ...
}
```

Instead, the profile should provide capabilities and definitions.

---

# 28. Game detection vs game support

These are separate.

The system should be capable of saying:

```text
Detected:
    Minecraft Story Mode
    PC
    x86
    Build X
```

while also saying:

```text
Support:
    recognized
    unsupported
```

The pipeline should be:

```text
Executable
    ↓
Identify
    ↓
Match Game
    ↓
Evaluate Support
    ↓
Select Profile
```

An unknown or unsupported build should remain fail-safe.

---

# 29. Runtime boot sequence

A conceptual boot sequence is:

```text
1. Process starts
2. Native proxy/runtime becomes available
3. Minimal bootstrap initializes
4. Wait for required game code/modules to become available
5. Identify executable/game/build
6. Select profile
7. Establish runtime directories/state
8. Discover mods
9. Validate manifests and sources
10. Resolve dependencies/conflicts
11. Prepare package cache
12. Initialize resource resolver
13. Initialize native plugin subsystem
14. Initialize TTMod scripting VM
15. Establish Game Lua integration
16. Initialize UI/menu integration
17. Dispatch lifecycle events
18. Enter normal runtime
```

The exact order may vary by game profile.

---

# 30. Game Lua lifecycle

Game Lua integration should be lifecycle-aware.

A game Lua state may be:

```text
Created
    ↓
Observed
    ↓
Identified
    ↓
Available for limited integration
    ↓
Game uses it
    ↓
Destroyed by game
```

TTMod should not assume that a state exists forever.

A state registry should be able to represent appropriate lifecycle states such as:

```text
Unknown
Engine
Menu
Destroyed
Unavailable
```

The raw pointer should remain internal to the native layer.

---

# 31. Lua stack discipline

Any native integration with a game Lua state must maintain stack discipline.

For a scoped operation:

```text
record stack top
    ↓
push/load/call
    ↓
handle error
    ↓
restore expected stack state
```

An invariant should be:

```text
Stack after TTMod operation == expected stack state
```

unless the operation's contract explicitly specifies returned values.

This should be tested.

---

# 32. Lua error handling

Lua errors must not silently corrupt either VM.

TTMod Lua:

```text
script error
    ↓
capture structured error
    ↓
disable/recover affected script according to policy
    ↓
continue TTMod where possible
```

Game Lua:

```text
integration error
    ↓
capture error
    ↓
restore stack/state as safely as possible
    ↓
avoid breaking the game
```

Game-facing integrations should prioritize fail-safe behavior.

---

# 33. Event architecture

All runtimes should be able to consume common TTMod events where appropriate.

Example event flow:

```text
Game
  ↓
native/game hook
  ↓
TTMod Event Bus
  ├── Native plugins
  ├── TTMod Lua
  └── TTMod Luau
```

Possible events:

```text
runtime_start
game_detected
game_ready
game_start
scene_change
episode_change
mod_loaded
mod_unloaded
menu_open
menu_close
shutdown
```

Events should be defined independently of the scripting language.

---

# 34. Native plugins registering scripting APIs

Native plugins may be able to extend TTMod's script environments.

Conceptually:

```text
Native Plugin
      │
      ├── register Lua API
      ├── register Luau API
      └── register TTMod events
```

Example:

```lua
graphics.draw_model(...)
graphics.capture_frame(...)
```

could be provided by a native graphics plugin rather than being hardcoded into TTMod core.

The public API contract for such extensions should be versioned.

---

# 35. Mod-to-mod communication

The framework should support controlled mod-to-mod communication.

Possible model:

```text
Mod A
  │
  │ registers service/API
  ▼
TTMod Mod Registry
  │
  ▼
Mod B
```

Example:

```lua
local inventory = ttmod.mods.get("inventory")

if inventory then
    local items = inventory.get_items()
end
```

Dependencies and API versions should be explicit where one mod requires another.

---

# 36. Mod lifecycle

A mod should have a predictable lifecycle.

Conceptually:

```text
Discovered
    ↓
Parsed
    ↓
Validated
    ↓
Dependency-resolved
    ↓
Prepared
    ↓
Loaded
    ↓
Initialized
    ↓
Running
    ↓
Stopping
    ↓
Unloaded
```

A failed stage should produce a structured state rather than a vague boolean.

---

# 37. Dependency resolution

Dependencies should be handled as a graph.

Example:

```text
A → B
B → C
```

The resolver should produce an initialization order such as:

```text
C
B
A
```

The resolver should detect:

- missing dependencies
- incompatible versions
- conflicts
- duplicate IDs
- dependency cycles
- disabled dependencies
- incompatible runtime requirements

The dependency resolver should be independent of whether the mod is:

```text
native
Lua
Luau
Telltale Lua
```

---

# 38. Package and unpacked mod equivalence

A mod packaged as:

```text
my-mod.ttmod
```

and an unpacked development mod:

```text
mods/my-mod/
```

should resolve to the same logical mod model.

Both should go through the same:

```text
ID validation
manifest validation
path validation
dependency resolution
runtime selection
permission processing
```

The source type should be an implementation detail:

```text
Directory
Package
Cache
```

---

# 39. Native mod loading

The native plugin loader should conceptually perform:

```text
1. Locate native module
2. Validate architecture
3. Validate required exports
4. Validate mod/runtime compatibility
5. Load module
6. Build stable host ABI
7. Call plugin initialization
8. Register plugin services/hooks/events
9. Mark plugin running
```

Native code should not be treated as trusted merely because the manifest is valid.

The native DLL remains arbitrary executable code.

---

# 40. Plugin ABI evolution

Public ABI structs should include size/version information where practical.

Example:

```c
struct ttmod_host {
    uint32_t struct_size;
    uint32_t api_version;
    ...
};
```

The host can then determine which fields/features a plugin understands.

The same principle should be used for other public ABI structures.

ABI compatibility should be tested with dedicated ABI-test plugins.

---

# 41. Native code and game calling conventions

Because TTMod targets legacy native games, game-facing function declarations must explicitly account for:

- architecture
- calling convention
- pointer width
- structure layout
- alignment
- parameter ordering
- return type
- game build

These definitions belong in game profiles or backend-specific code.

They should not leak into generic mod APIs.

---

# 42. Threading model

The framework should define three broad thread categories:

```text
Game Thread
TTMod Runtime/Script Threads
Worker Threads
```

Rules:

### Game thread

Allowed to interact with game-native/game-Lua facilities subject to the profile.

### TTMod script thread

Runs the TTMod-owned scripting VM.

May request game operations through a dispatcher.

### Worker threads

Perform work such as:

- package processing
- background file operations
- hashing
- logging
- tooling

They should not directly manipulate game Lua or game-native state unless a documented subsystem explicitly permits it.

---

# 43. Hook hot-path rule

Hooks on functions such as `CreateFileW` are performance-critical.

Hot paths should avoid:

- synchronous disk I/O
- JSON parsing
- mod discovery
- package extraction
- unnecessary heap allocation
- expensive string operations
- blocking locks

Preferred:

```text
Hook
 ↓
cheap normalized lookup
 ↓
precomputed resolver result
 ↓
call original
 ↓
lightweight event/telemetry
```

Background work belongs outside the hook.

---

# 44. Logging architecture

Use two concepts:

## Human logs

Readable output:

```text
INFO
WARN
ERROR
```

## Runtime telemetry

Structured machine-readable events.

Example:

```text
category = "plugin"
action   = "validated"
mod      = "example"
```

Telemetry should not require tools to parse human-readable log strings.

A useful architecture is:

```text
Game / Runtime Thread
        ↓
Bounded Event Queue
        ↓
Logger/Telemetry Worker
        ↓
Text / JSONL output
```

No synchronous file writes from hot hooks.

---

# 45. UI and menu architecture

The menu system should eventually be layered:

```text
Config / Mod State
       ↓
Menu Model
       ↓
Lua/UI Serializer
       ↓
Telltale Menu Lua
```

The core configuration model should not know how Lua source code is serialized.

This allows future frontends without rewriting configuration logic.

---

# 46. Current Telltale menu integration

The existing TTMod research indicates that the game's menu scripting environment is distinct from other game Lua states.

Therefore, menu injection should be treated as a Game Lua backend:

```text
TTMod
  ↓
GameLuaRuntime
  ↓
Menu Lua State
  ↓
Menu.lua integration
```

It should not become the generic TTMod script VM.

---

# 47. Direct Telltale Lua scripting

Direct game-Lua mods are intentionally advanced.

They may use APIs such as game-provided functions and objects:

```lua
AgentGetProperty(...)
AgentGetChild(...)
Menu_Add(...)
```

where those APIs exist in the relevant game/profile.

These scripts are inherently coupled to the game and build.

They should therefore be:

- explicitly declared
- documented as advanced
- profile-aware
- tested against supported builds
- expected to break when the underlying game internals change

---

# 48. Stable API vs advanced API

TTMod should have a clear separation:

```text
Stable:
    ttmod.*

Advanced:
    ttmod.telltale.*
    ttmod.native.*
```

The stable API should work as consistently as possible across supported games.

The advanced APIs expose power at the cost of compatibility.

---

# 49. Example: normal Luau mod

Directory:

```text
mods/
└── example/
    ├── manifest.json
    └── main.luau
```

Manifest:

```json
{
    "id": "example",
    "version": "1.0.0",
    "runtimes": ["luau"]
}
```

Script:

```lua
ttmod.events.on("game_start", function()
    ttmod.log("Example loaded")
end)
```

Execution:

```text
main.luau
    ↓
TTMod Luau VM
    ↓
TTMod API
    ↓
Native Runtime
    ↓
Game
```

---

# 50. Example: direct game-Lua mod

Directory:

```text
mods/
└── menu-example/
    ├── manifest.json
    └── game.lua
```

Manifest concept:

```json
{
    "id": "menu-example",
    "version": "1.0.0",
    "runtimes": ["telltale-lua"]
}
```

Execution:

```text
game.lua
    ↓
Game Lua Runtime
    ↓
Selected Telltale Lua State
    ↓
Game scripts / engine APIs
```

This is not executed by the TTMod Luau VM.

---

# 51. Example: native mod

Directory:

```text
mods/
└── native-example/
    ├── manifest.json
    └── plugin.dll
```

Execution:

```text
plugin.dll
    ↓
Native loader
    ↓
TTMod C ABI
    ↓
Runtime
    ↓
Game
```

The source language might have been:

```text
C
C++
Rust
Zig
```

but the runtime sees the same ABI.

---

# 52. Example: ultimate mod

Directory:

```text
mods/
└── ultimate/
    ├── manifest.json
    ├── plugin.dll
    ├── main.luau
    └── game.lua
```

Runtime layout:

```text
                 Ultimate Mod
                       │
        ┌──────────────┼──────────────┐
        │              │              │
    plugin.dll      main.luau      game.lua
        │              │              │
      Native        TTMod VM       Game VM
        │              │              │
        └──────────────┼──────────────┘
                       │
                  TTMod Runtime
                       │
                      MCSM
```

This mod can combine all three modding systems.

---

# 53. Scripting API consistency

Lua and Luau should ideally have matching API semantics.

For example:

```text
ttmod.events.on
ttmod.events.emit
ttmod.log
ttmod.mods.get
ttmod.game.get_state
ttmod.config.get
ttmod.ui
```

The API documentation should define behavior independently of language syntax.

Language bindings then map those semantics into Lua and Luau.

---

# 54. Luau-specific facilities

Luau may expose additional language features that standard Lua does not.

These should be considered language features, not TTMod API differences.

For example:

```text
type annotations
Luau-specific syntax
Luau runtime capabilities
```

should not change the meaning of:

```text
ttmod.events
ttmod.game
ttmod.mods
```

where possible.

---

# 55. Standard Lua support

Standard Lua should remain available for users who prefer its smaller, established language/runtime model.

Lua mods should not need Luau features.

The TTMod API should document which operations require:

```text
Lua
Luau
both
```

and avoid unnecessary runtime-specific dependencies.

---

# 56. Game Lua language version

The game Lua runtime is determined by the game's implementation.

TTMod should **not assume** that the game's scripting environment is identical to a modern standalone Lua installation.

Game-specific compatibility belongs in the profile/backend.

The manifest runtime:

```text
telltale-lua
```

means:

> Run using the Lua environment provided by the target game/profile.

---

# 57. State registry

The game Lua subsystem should maintain a registry similar to:

```text
GameLuaRuntime
{
    states:
        EngineState
        MenuState
        OtherState[]
}
```

Each state may contain:

```text
lua_State*
identity
creation information
thread information
capabilities
availability
lifecycle state
```

The raw pointer should remain internal to the native layer.

---

# 58. No raw VM handles in normal script APIs

Normal TTMod scripts should not receive:

```text
lua_State*
```

or equivalent raw interpreter handles.

Instead:

```text
Lua script
    ↓
TTMod object
    ↓
native implementation
```

This keeps the VM implementation replaceable and prevents scripts from bypassing safety and lifecycle rules.

---

# 59. Game object handles

When game objects need to be exposed to TTMod scripting, consider opaque handles.

Example concept:

```text
GameObjectHandle
```

The script might see:

```lua
local player = ttmod.game.get_player()
player:get_name()
```

while native code internally stores whatever game pointer/identity is necessary.

This provides a stable API without exposing raw engine pointers to scripts.

---

# 60. Lifetime rules for game objects

Game-backed script objects must not assume that the underlying game object exists forever.

Possible states:

```text
Valid
Destroyed
Invalid
Stale
Unavailable
```

The API should detect or safely handle stale objects.

The exact ownership model must be profile-specific where necessary.

---

# 61. Native-to-script events

A native hook may generate:

```text
Game event
    ↓
TTMod Event Bus
    ↓
TTMod Lua callbacks
    ↓
Luau callbacks
    ↓
Native plugin callbacks
```

Callbacks should not be executed directly from arbitrary hot hooks when doing so would be unsafe.

Instead, queue events where necessary.

---

# 62. Script scheduling

The TTMod scripting system should have explicit scheduling.

Possible primitives:

```text
on event
defer
next tick
timer
interval
game-thread request
```

The exact API is not finalized here.

The critical principle is:

> **TTMod scheduling and game-thread scheduling are different concepts.**

The TTMod VM can schedule script execution without automatically making the game Lua state safe to call.

---

# 63. Script failure isolation

A failure in one TTMod script should not automatically terminate:

- the game
- the native runtime
- unrelated mods
- other scripting VMs

Example:

```text
Mod A Lua error
    ↓
disable/report Mod A callback
    ↓
Mod B continues
    ↓
TTMod continues
    ↓
Game continues
```

Direct Telltale Lua scripts may not receive the same isolation guarantees because they execute inside the game environment.

---

# 64. Mod runtime classification

A useful conceptual classification is:

```text
TTMod Lua:
    controlled, portable, stable

TTMod Luau:
    controlled, modern, stable

Telltale Lua:
    powerful, game-specific, advanced

Native:
    maximum power, fully trusted
```

This is not a strict power ranking. Each environment exposes a different class of capability.

---

# 65. Recommended manifest concepts

The final manifest format is separate from this architecture, but it should be capable of expressing:

```text
identity
version
game compatibility
runtimes
permissions
entrypoints
native plugin
Lua entrypoint
Luau entrypoint
Telltale Lua entrypoint
```

For example:

```json
{
    "id": "example",
    "version": "1.0.0",
    "runtimes": ["luau"],
    "entrypoints": {
        "luau": "main.luau"
    }
}
```

Or:

```json
{
    "id": "ultimate",
    "version": "1.0.0",
    "runtimes": [
        "native",
        "luau",
        "telltale-lua"
    ],
    "entrypoints": {
        "native": "plugin.dll",
        "luau": "main.luau",
        "telltale-lua": "game.lua"
    }
}
```

This is illustrative and does not lock the final schema.

---

# 66. Build-time vs runtime responsibilities

Runtime:

```text
loading
validation
scheduling
API registration
VM execution
native module loading
game integration
```

Development/build tooling:

```text
compile C/C++/Rust/Zig
package mods
validate manifests
lint Lua/Luau
generate metadata
build native modules
```

A future TTMod CLI may hide native build complexity from mod authors.

For example:

```text
ttmod build
```

could compile a native mod and package it.

The runtime does not need to compile C++ source itself.

---

# 67. Why the runtime should not compile C++ mods

The runtime should receive native machine code, normally through a native module.

Compiling C++ inside a running game would introduce unnecessary:

- compiler dependencies
- security risks
- disk/toolchain requirements
- runtime complexity
- platform/toolchain differences

Keep compilation in the development toolchain.

---

# 68. External tools

Not every mod-related program needs to run inside the game process.

TTMod may eventually have:

```text
TTMod Manager
TTMod CLI
Package builder
Debugger
Profile editor
Reverse-engineering tools
```

These can be written in different languages.

The in-process runtime should remain focused on:

```text
native game integration
mod execution
scripting
hooks
```

---

# 69. Language strategy

The TTMod runtime language and the mod authoring languages are separate decisions.

The project itself can be implemented in:

```text
C / C++ / Rust / etc.
```

while mod authors may use:

```text
Native:
    C
    C++
    Rust
    Zig
    ...

TTMod VM:
    Lua
    Luau

Game VM:
    Telltale Lua
```

There is no requirement that the runtime implementation language match the scripting language.

---

# 70. Recommended conceptual module structure

A future implementation may use:

```text
TTMod
├── Core
│   ├── errors
│   ├── ids
│   ├── versions
│   ├── manifests
│   ├── discovery
│   ├── dependencies
│   ├── packages
│   └── cache
│
├── Runtime
│   ├── lifecycle
│   ├── events
│   ├── scheduler
│   ├── permissions
│   └── mod registry
│
├── Native
│   ├── plugin loader
│   ├── ABI
│   ├── hooks
│   └── native API
│
├── Script
│   ├── Lua backend
│   ├── Luau backend
│   ├── scheduler
│   └── bindings
│
├── Game
│   ├── profiles
│   ├── PE detection
│   ├── game hooks
│   ├── state
│   └── capability system
│
├── GameLua
│   ├── state registry
│   ├── state identification
│   ├── Lua ABI
│   ├── dispatcher
│   └── menu integration
│
└── UI
    ├── model
    ├── menu bridge
    └── serializers
```

---

# 71. Critical invariants

The following rules should be treated as architecture-level invariants.

## Ownership

```text
TTMod VM:
    owned by TTMod

Game Lua:
    owned by game

Native plugin:
    owns only the resources explicitly assigned to it
```

## No cross-VM object ownership

Do not move raw Lua VM objects between TTMod Lua and Game Lua.

## Game-thread discipline

Do not call game Lua/native state from arbitrary worker threads.

## Stable API

Mods should depend on TTMod abstractions rather than raw game addresses.

## Fail-safe behavior

Unknown or unsupported game builds should remain unmodified where possible.

## Native trust

Native plugins are arbitrary code and must be treated as trusted software.

## Determinism

Discovery, dependency resolution, package creation, and other externally observable operations should be deterministic.

---

# 72. Example complete runtime diagram

```text
                                MCSM
                                 │
               ┌─────────────────┴─────────────────┐
               │                                   │
         Native Engine                       Game Lua Runtime
               │                                   │
        Win32 / hooks                    ┌──────────┼──────────┐
               │                         │          │          │
               │                      Engine      Menu      Other
               │                       state      state      states
               │                         │          │
               └───────────────┬─────────┴──────────┘
                               │
                         TTMod Native Runtime
                               │
         ┌─────────────────────┼─────────────────────┐
         │                     │                     │
         ▼                     ▼                     ▼
   Native Mods            TTMod Lua              TTMod Luau
   C/C++/Rust/etc.           VM                      VM
         │                     │                     │
         └─────────────────────┼─────────────────────┘
                               │
                           TTMod API
                               │
                  ┌────────────┴────────────┐
                  │                         │
             Game Dispatcher          Event Bus
                  │                         │
                  └────────────┬────────────┘
                               │
                              MCSM
```

---

# 73. Example execution: TTMod Luau calls game state

Suppose:

```lua
local player = ttmod.game.get_player()
print(player.name)
```

Possible execution:

```text
1. Luau VM executes script.
2. `ttmod.game.get_player()` calls a TTMod binding.
3. Binding submits/executes a GameRequest according to thread policy.
4. Native runtime chooses the profile-specific implementation.
5. Implementation obtains the player through native game APIs or Telltale Lua.
6. Native result is converted into a TTMod-owned representation.
7. Representation is returned to Luau.
8. Luau continues execution.
```

The Luau script does not know which backend implementation was used.

---

# 74. Example execution: game Lua calls TTMod

Suppose TTMod injects a supported native function into a game Lua state.

Flow:

```text
1. Game Lua calls TTMod-provided global/function.
2. Lua C closure enters TTMod native code.
3. Native code validates the state/context.
4. TTMod operation executes.
5. Results are pushed according to the game's Lua ABI.
6. Stack is restored according to the function contract.
7. Control returns to game Lua.
```

This is direct integration with the game's VM.

---

# 75. Example execution: native plugin registers Luau function

Flow:

```text
1. Native plugin initializes.
2. Plugin requests scripting API registration.
3. TTMod validates the plugin/runtime permission.
4. TTMod registers a binding in the TTMod scripting runtime.
5. Luau mod calls the new API.
6. TTMod dispatches to the native plugin.
7. Native plugin performs its operation.
8. Result is marshaled back to Luau.
```

This makes the scripting system extensible.

---

# 76. Example execution: native plugin communicates with game Lua

Flow:

```text
Native plugin
    ↓
TTMod GameLua API
    ↓
Game Dispatcher
    ↓
Game thread
    ↓
Selected Telltale Lua state
    ↓
Game Lua operation
    ↓
Result marshaled back
```

The plugin should not casually retain or call raw `lua_State*` objects outside documented lifecycle rules.

---

# 77. Recommended documentation terminology

Use these names consistently:

### "TTMod VM"

The TTMod-owned Lua/Luau scripting runtime.

### "Game Lua Runtime"

The game-owned collection of Lua states.

### "Game Lua State"

One specific `lua_State*` inside the Game Lua Runtime.

### "Telltale Lua"

The Lua language/runtime exposed by the Telltale game.

### "Native Runtime"

TTMod's in-process native implementation.

### "Native Plugin"

A mod module loaded into the process through the native C ABI.

### "TTMod API"

Stable public functionality exposed to mods.

### "Game Backend"

Profile-specific implementation used to talk to the target game.

### "Bridge"

A boundary between TTMod and a game-owned subsystem, especially Game Lua.

---

# 78. Things that should NOT be conflated

Do not use these terms as if they mean the same thing:

```text
TTMod Lua
≠
Telltale Lua

TTMod VM
≠
Game VM

Lua
≠
Luau

Game Lua Runtime
≠
one lua_State*

Native Plugin
≠
Telltale Lua script

TTMod API
≠
Telltale API
```

This vocabulary is important because much of the project's complexity comes from these being separate systems.

---

# 79. Long-term extensibility

The architecture should permit adding another game without redesigning all mod APIs.

Example:

```text
TTMod API
     │
     ├── MCSM1 Backend
     ├── MCSM2 Backend
     ├── TWD Backend
     └── Future Telltale Backend
```

Each backend supplies:

```text
game detection
capabilities
native hooks
Game Lua integration
game-specific state access
profile restrictions
```

The script API remains as stable as possible.

---

# 80. Recommended mod author experience

The default experience should be simple:

```text
Create mod
    ↓
choose Lua or Luau
    ↓
use ttmod.* API
    ↓
package
    ↓
install
```

Advanced experience:

```text
Use Telltale Lua
    ↓
direct game scripting
```

Expert experience:

```text
Use native C/C++/Rust/Zig/etc.
    ↓
native hooks / engine access
```

Ultimate experience:

```text
Native + Luau/Lua + Telltale Lua
```

---

# 81. Recommended final modding model

TTMod should be described as:

> **A multi-runtime modding framework that provides a TTMod-owned scripting environment, direct integration with the game's existing Telltale Lua runtime, and a native plugin interface. Mods may use one or more of these environments together.**

The three primary paths are:

```text
1. Native
   C / C++ / Rust / Zig / other C-ABI languages

2. TTMod VM
   Lua / Luau

3. Game VM
   Telltale Lua
```

The core relationship is:

```text
                   TTMod
                     │
       ┌─────────────┼─────────────┐
       │             │             │
    Native        TTMod VM      Game VM
       │             │             │
 C/C++/Rust      Lua / Luau    Telltale Lua
       │             │             │
       └─────────────┼─────────────┘
                     │
               Native Runtime
                     │
                    Game
```

---

# 82. Design decision summary

## Decision 1

**Use two independent scripting worlds.**

```text
TTMod VM ≠ Game Lua Runtime
```

## Decision 2

**TTMod VM supports Lua and Luau.**

```text
main.lua
main.luau
```

are normal TTMod scripting entrypoints.

## Decision 3

**The game keeps its own Lua runtime.**

TTMod integrates with it rather than replacing it.

## Decision 4

**Direct Telltale Lua is an advanced modding mode.**

It is powerful but game-specific and less stable.

## Decision 5

**Native mods use a C-compatible ABI.**

This allows multiple implementation languages.

## Decision 6

**A mod can use multiple runtimes simultaneously.**

For example:

```text
plugin.dll
main.luau
game.lua
```

## Decision 7

**TTMod API is the stable abstraction.**

Game-specific implementation details stay behind the API where possible.

## Decision 8

**Native code and Game Lua are trusted/advanced environments.**

They are not equivalent to a sandboxed TTMod script.

## Decision 9

**Game-thread access is explicit.**

Cross-thread calls into the game runtime are not assumed to be safe.

## Decision 10

**Game profiles own game-specific details.**

Addresses, signatures, calling conventions, state identification, and capabilities belong to the appropriate backend/profile.

---

# 83. Implementation checklist

## Architecture

- [ ] Separate TTMod VM from Game Lua Runtime.
- [ ] Create a GameLuaRuntime state registry.
- [ ] Define a native Game Dispatcher.
- [ ] Define common TTMod API semantics.
- [ ] Separate stable APIs from Telltale-specific APIs.

## TTMod VM

- [ ] Embed Lua.
- [ ] Embed Luau.
- [ ] Define language-independent API behavior.
- [ ] Define script lifecycle.
- [ ] Define scheduling.
- [ ] Define script failure policy.
- [ ] Define memory/execution limits.

## Game Lua

- [ ] Preserve game ownership of `lua_State*`.
- [ ] Hook/discover game-created states.
- [ ] Identify engine/menu/etc. states.
- [ ] Define state lifecycle.
- [ ] Define stack-safety rules.
- [ ] Define supported game-Lua operations.
- [ ] Define game-thread dispatch.

## Native

- [ ] Stable C ABI.
- [ ] ABI version negotiation.
- [ ] Struct-size versioning.
- [ ] Plugin lifecycle.
- [ ] Native API registration.
- [ ] Native scripting bindings.
- [ ] Hook API.
- [ ] Profile-specific native backend.

## Mod format

- [ ] Runtime declaration.
- [ ] Entry-point declaration.
- [ ] Permissions/capabilities.
- [ ] Native plugin support.
- [ ] Lua support.
- [ ] Luau support.
- [ ] Telltale Lua support.
- [ ] Multiple runtimes per mod.

## Security

- [ ] Explicit native-trust warning.
- [ ] Game-Lua trust warning.
- [ ] TTMod scripting sandbox policy.
- [ ] API permission enforcement.
- [ ] Mod isolation policy.
- [ ] Secure cross-VM value marshaling.

## Testing

- [ ] TTMod Lua tests.
- [ ] Luau tests.
- [ ] Game Lua bridge tests.
- [ ] Lua stack-balance tests.
- [ ] Game-thread dispatch tests.
- [ ] Native ABI tests.
- [ ] Multi-runtime mod integration test.
- [ ] Error isolation tests.
- [ ] Profile compatibility tests.

---

# 84. Final architecture principle

The purpose of this design is **not** to restrict modders to one safe abstraction.

The purpose is to provide **multiple levels of access without confusing their ownership models**.

A normal developer can write:

```lua
ttmod.log("Hello")
```

An advanced script developer can use:

```lua
TelltaleGameFunction(...)
```

inside the game Lua environment.

An expert native developer can write:

```cpp
// native hook / engine integration
```

And a total-conversion-level mod can combine:

```text
C/C++/Rust/Zig
+
Luau
+
Telltale Lua
```

The result is a framework that provides:

```text
easy scripting
        +
modern scripting
        +
direct engine scripting
        +
unrestricted native modding
```

while maintaining clear boundaries between:

```text
TTMod-owned execution
Game-owned execution
Native execution
```

That separation is the foundation of the TTMod architecture.
