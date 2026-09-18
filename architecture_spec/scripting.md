# Scripting (Lua) — Full Reference

> Full detail for this topic; linked from `ARCHITECTURE_SPEC.md`. Ground truth — do not duplicate here.

## 10. Scripting (Lua)

### 10.1 Runtime

- **Lua 5.4** (PUC-Lua) embedded via a thin C++ wrapper (`vb::script::Vm`).
  Optionally `sol2` as the binding layer (header-only, ergonomic) — decision
  recorded in `REMAINING_TASKS.md`.
- One VM on the **server** for the content pack (authoritative logic).
- One **restricted** VM on the **client** purely for UI event callbacks defined
  by `ui/*.lua` in the pack. It has *no* access to world mutation, filesystem, os,
  io, or network — only a `ui` table and read-only client state.

### 10.2 Sandbox

Both VMs run with a curated global environment:

- Removed: `os.execute`, `os.exit`, `os.getenv`, `io.*`, `require` (replaced),
  `load`/`loadstring` of arbitrary bytecode, `debug.*` (except `traceback`),
  raw `package` access.
- `require` is reimplemented to resolve only within the pack's virtual
  filesystem (the synced assets), never the host disk.
- Instruction-count hook (`lua_sethook`) aborts runaway callbacks; a per-callback
  wall-clock budget is enforced by the tick loop.
- Memory: custom allocator with a ceiling; allocation failure surfaces as a Lua
  error, not a crash.

### 10.3 C++ ⇆ Lua API surface (server)

Registration (call-time: pack load only):

- `vb.register_block(def) -> BlockId` — `def.max_damage` (0 = instant break,
  the default) and `def.crack_texture` (optional override, §10.7) join the
  existing fields.
- `vb.register_item(def)`
- `vb.register_entity(kind_def)` — the **kind is the class**: `on_spawn`,
  `on_tick`, `on_hit`, `on_death`. Each entity `vb.world.spawn(kind, pos)`
  creates is an independent **object** — its `ScriptState` component (§7.1)
  holds a per-instance Lua table, passed as `self` to every callback, so two
  entities of the same kind track separate data the way object instances do.
  Base components (`get_pos`, ...) stay accessor methods on `self`, matching
  the existing `player:`/`entity:` style; kind-specific fields are free-form
  on `self` itself.
- `vb.register_biome(def)`
- `vb.worldgen.set_pipeline(node_tree_def)`
- `vb.register_craft(recipe)`
- `vb.register_keybind(name)` — declares a custom input slot (§10.6); frozen
  at `freeze()` like every other registry above.

Runtime:

- World: `vb.world.get_block(x,y,z)`, `vb.world.set_block(x,y,z,id)`,
  `vb.world.raycast(origin, dir, max)`, `vb.world.spawn(kind, pos)`.
- Entities: `entity:get_pos()`, `entity:set_velocity()`, `entity:remove()`,
  `entity:get_inventory()`, component-ish accessors for base components.
- Players: `player:send_message(text)`, `player:open_ui(name, ctx)`,
  `player:give(itemstack)`, `player:take(itemstack) -> bool`,
  `player:get_name()`.
- Events (subscribe): `vb.on("player_join" | "player_leave" | "block_break" |
  "block_place" | "player_interact" | "chat" | "tick" | "player_input" |
  "block_break_begin" | "block_break_tick" | "block_health_tick", handler)`.
  Handlers may return `false` to veto vetoable events; `player_input` may
  instead return a replacement input table (§10.6); `block_break_tick` and
  `block_health_tick` return numbers, not booleans (§10.7).
- Scheduling: `vb.after(seconds, fn)`, `vb.every(seconds, fn)`.
- Storage: `vb.storage` — a persisted key/value table (JSON-backed) for
  pack-global world data (counters, config). `vb.db.get/set/delete(key)` is
  the separate, generic per-key store for script-owned records (players,
  sessions, anything) — see §10.6.

### 10.4 UI API surface (client VM) — immediate-mode, reactive

- `ui.define(name, render_fn)` — `render_fn(state)` is called **every UI
  frame** the screen is open, and declares the widget tree for that frame
  (panels, labels, buttons, lists, item grids, text inputs). The C++ side
  walks whatever it returns and issues the matching `raygui` calls directly.
  `raygui` is itself immediate-mode, so this needs no virtual-DOM diff — a
  state mutation (from an event handler, or a value pushed down from the
  server) just changes what `render_fn` returns next frame, giving the same
  reactive feel as a retained-mode framework without the bookkeeping.
- Callbacks: `on_click`, `on_change`, `on_close` — these send a `C2S_UiEvent`
  RPC to the server VM (`player:open_ui` context round-trips), so UI logic that
  matters is still server-authoritative. Purely cosmetic state (hover, scroll
  position) can be mutated locally and read straight back by `render_fn`.
- This replaces the older static-declaration model; `content/base/ui/
  {inventory,pause}.lua` need rewriting to the `render_fn` shape when this
  lands, not just extending (tracked in `REMAINING_TASKS.md` Phase 6).

### 10.5 Event flow example (block break)

```
client click ─▶ C2S_BlockEdit ─▶ server: reach/tool check
  ─▶ Lua "block_break" event (veto?) ─▶ BlockEditSystem applies
  ─▶ chunk.revision++, light dirty, mesh dirty
  ─▶ Lua on_break callback (drops, sfx trigger, ...)
  ─▶ S2C_BlockEditResult + S2C_ChunkDelta to all interested players
```

### 10.6 Custom input channel & generic storage

**Input.** `vb.register_keybind(name)` builds a frozen, ordered registry, same
as blocks/entities. The registered set is synced to the client at handshake
(same shape as `S2C_BlockRegistry`, §4.3 lineage), and from then on the wire
only ever carries a bounded bitset indexed by registration order — there is
no arbitrary key+string encoding, so an unregistered key cannot be
represented at all. That closed schema is the flood defense, not a
post-receipt filter; a per-connection rate limit on top is defense in depth
(§17). This channel is additive to `PlayerInput`'s existing movement/look
fields, which are untouched. Server-side only: `vb.on("player_input",
handler)` fires in `IngestInputSystem` (§7.2) before `MovementIntegrationSystem`
runs, and may veto or replace the tick's input — e.g. blocking movement
entirely, or reinterpreting it as a dash/ability.

**Storage.** `vb.storage` (§10.3) stays pack-global. `vb.db.get(key)` /
`vb.db.set(key, value)` / `vb.db.delete(key)` is a separate, generic
per-key store — `key` is whatever the script chooses (`"user:" .. name`,
`"session:" .. token`, ...). The engine has no notion of "logged in": a
connection is just a connection, exactly as today, until a pack's own login
flow (built on `vb.db` + the input/UI APIs above) looks up a record and
decides to recognize it. Joining a world is not authenticating, the same way
loading a webpage isn't — that only happens if and when the pack implements
it. Since packs that do build a login flow need to hash credentials, and the
sandbox deliberately strips `os`/`io` (§10.2) making pure-Lua hashing both
slow and easy to get wrong, a minimal `vb.crypto.hash(...)` primitive is
planned so that a pack that chooses to implement auth doesn't have to roll
its own crypto. The engine still takes no position on auth as a concept.
Backend: the current single `storage.json` blob doesn't scale to one record
per identity — `vb.db` needs an actual per-key store (SQLite is the leading
candidate) once implemented.

### 10.7 Shared block-damage breaking

For any block with `max_damage > 0` (§5.2), breaking is a **shared damage
pool** rather than instant: multiple players may contribute concurrently
("breaking together"), progress is visible to everyone nearby, and the
engine ships zero built-in policy for how fast damage accrues or whether/how
it heals — those are entirely Lua's call, following the same
mechanism-vs-policy split used for input interception (§10.6).

- **State**: server tracks a sparse `pos → {damage, max_damage,
  last_touched_tick}` map — only blocks with damage > 0 exist in it. This
  rides the *existing* interest/replication system (§8.4) as a transient
  record rather than a new wire channel: it appears in nearby players'
  snapshots when damage becomes > 0 and disappears when it returns to 0, the
  same spawn/despawn diffing every other replicated object already gets.
  It does **not** touch chunk revisions or mesh invalidation — a block's
  damage is not a block-data change until the actual break commits.
- **Contributing**: `C2S_BlockBreakBegin{pos, face}` / `C2S_BlockBreakStop{pos}`
  bracket a player holding on a target (reach/tool checks same as
  `C2S_BlockEdit` today). While held, `vb.on("block_break_begin", handler)`
  gates entry (vetoable — reach/tool/protection), then
  `vb.on("block_break_tick", handler)` fires once per tick **per
  contributing player**, returning the damage delta to add this tick. The
  engine sums all concurrent contributors' deltas and clamps at
  `max_damage`; it has no opinion on tool speed, enchantments, or anything
  else that delta is computed from.
- **Healing**: `vb.on("block_health_tick", handler)` fires once per tick for
  every block currently holding damage — `(pos, damage, max_damage,
  ticks_since_last_hit)` in, a new damage value (or nothing, meaning
  unchanged) out. No heal, full heal, gradual decay, heal-after-N-idle-ticks
  — all of it is the handler's decision; the engine only tracks
  `last_touched_tick` and calls the hook. A pack that registers no handler
  gets permanent damage (no healing at all).
- **Completion**: when summed damage reaches `max_damage`, the engine drives
  the *existing*, unchanged `C2S_BlockEdit`/`BlockEditSystem`/`on_break`
  pipeline (§8.5, §10.5) to actually break the block — this system only
  gates when that pipeline fires, it doesn't replace it.
- **Rendering — default + override, not Lua's job**: the engine ships one
  baseline generic crack overlay (progressive stages by `damage/max_damage`)
  so breaking looks right with zero scripting. A block may override it via
  `crack_texture` (§5.2), same override-by-name convention as every other
  registry in this doc. **Dependency:** this needs the real texture/atlas
  system that's still pending (`REMAINING_TASKS.md` 4.3/5.1 — client is
  untextured cubes today), so crack textures can't land before that does.
- **Cost**: `block_break_tick`/`block_health_tick` are bounded by the number
  of blocks currently being damaged, which is small and player-driven, not
  proportional to world size — same reasoning as why the custom-keybind
  per-connection cost in §10.6 is acceptable.

