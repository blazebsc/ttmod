# M8 game state, read-only (PROVEN under Wine)

## Surface
- `ttmod_state get_state()` (ABI v3, additive): episodes seen loading,
  archive/resdesc/save/other open counters, save dir. Fixed-buffer C struct.
- `events_state_summary()` at detach → `ttmod.exit.log` one-liner.
- Live query proven by event-log (`state eps=8 archives=197 …`).
- Exit proven: `state: episodes=[1,2,3,4,5,6,7,8] archives=242 resdesc=154
  saves=129 others=123 save_dir=c:/users/blake7/documents/telltale
  games/minecraft - story mode`.

## Semantics (read carefully)
Every field means "OBSERVED DURING THIS SESSION", never a live engine value:
episodes = episode archives seen opening (boot touches all 8 S1 episodes);
saves = session/prefs files seen opening. No memory is read. No polling:
the tracker is fed synchronously by the file-event path.

## Explicitly Unknown (not exposed)
Current scene/characters/dialogue/choices/variables/flags/inventory/camera,
save-file CONTENTS (prefs.prop/estore are binary PROP — parsing is M12
offline-tooling work), script VM internals. The API will grow only with
evidence; it will not guess.

## Threading
Tracker mutated under the event bridge mutex during dispatch; `get_state`
snapshots under the same mutex. Callbacks must remain non-blocking (M7 rule).
