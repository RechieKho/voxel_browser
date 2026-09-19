# Voxel Browser — Replication & Interest Management

> Companion to `docs/protocol.md`. Covers entity replication: interest
> management, the snapshot message, and the librg spike (spec §18 Q3).

## Model (spec §8.4)

The server owns all entity state. Each tick, per player, the replication system
computes the set of entities that player can *see* (their interest set), diffs it
against what was sent last tick, and emits `S2C_EntitySnapshot` on lane 2
(unreliable, sequence-gated). Spawns/despawns are folded in as explicit records
and re-sent until acked.

## Phase 1: hand-rolled interest

`inc/vb/net/interest.hpp` — `InterestSet`: a per-owner grid of visible `NetId`s
keyed by a coarse interest cell (default 32 m, independent of the 32³ voxel
chunk). `InterestGrid::visible_for(owner_pos, radius_cells)` returns the ids in
cells within `radius` of the owner's cell. `diff(previous, current)` yields
`{entered, stayed, left}` so the snapshot carries adds / updates / removes.

`S2C_EntitySnapshot` (`inc/vb/protocol/snapshot.hpp`):

| Field                | Type            | Notes                                    |
| -------------------- | --------------- | --------------------------------------- |
| `server_tick`        | `u32`           | monotonic; client interpolates against it |
| `last_acked_input_seq` | `u32`         | for the local player (0 until Phase 3)    |
| `entered[]`          | records         | full state for newly-visible entities     |
| `updated[]`          | records         | position/rotation deltas                  |
| `removed[]`          | `u32 net_id`    | left the interest set                     |

Record: `{ u32 net_id, u16 kind, dvec3 pos, vec2 rot(yaw,pitch), vec3 vel, u8 flags }`.

**Phase 1 test (the exit criterion):** two headless clients join one server;
each is placed at a distance and receives the other's `entered` record only when
within interest range, and a `removed` when moved out. See
`tests/unit/replication_test.cpp`.

## librg spike (v7.4.0) — findings

Repo: `github.com/zpl-c/librg`, pure C99, **single self-contained header**
(`code/librg.h` bundles its own `vendor/zpl.h` — *no separate zpl dependency*).
Define `LIBRG_IMPL` in exactly one TU.

### What it does

- `librg_world_create()` → one world on the server, one on each client.
- Configure the interest grid: `librg_config_chunksize_set` /
  `librg_config_chunkamount_set` / `librg_config_chunkoffset_set`. These "chunks"
  are librg's interest cells, unrelated to voxel chunks.
- `librg_entity_track(world, id)` per entity; `librg_entity_owner_set(world, id,
  owner_id)` marks the player who controls it; each tick
  `librg_entity_chunk_set(world, id, librg_chunk_from_realpos(world, x,y,z))`
  keeps its cell current.
- **Interest is chunk-radius based**, not a true sphere: `chunk_radius` (in
  cells) passed to `librg_world_write` / `librg_world_query`.
- **Framing**: `librg_world_write(world, owner, radius, buf, &size, ud)` walks
  the owner's visible set and invokes your `LIBRG_WRITE_CREATE/UPDATE/REMOVE`
  event handlers, each of which `memcpy`s an opaque payload into
  `librg_event_buffer_get(...)`. `librg_world_read(world, owner, buf, size, ud)`
  on the client applies it via `LIBRG_READ_*` handlers. librg owns the
  create/update/remove envelope; **we own the per-entity payload bytes**.
- Does **not** touch sockets — it is middleware. We take the blob from
  `librg_world_write` and send it as our `S2C_EntitySnapshot` payload on lane 2.

### Decision (§18 Q3)

**Adopt librg for interest culling + create/update/remove framing; keep our own
codec for the per-entity payload and our own envelope/lane routing.** Rationale:
its chunk-radius query and delta bookkeeping (re-send-until-acked, per-owner
visibility) are exactly what §8.4 needs and are painful to reimplement well; the
payload stays ours so wire changes still flow through `docs/protocol.md` +
`kEngineProtocolVersion`.

### Wired in (`VB_WITH_REPLICATION`)

`InterestGrid` (`inc/vb/replication/interest.hpp` + `src/replication/interest.cpp`)
now picks its backend at compile time from `VB_WITH_REPLICATION`, with no change
to its public interface or to any caller (`ServerSession` only ever calls
`upsert`/`remove`/`visible_from`/`diff_interest`):

- **On** (default): `upsert()` tracks the `NetId` as a librg entity (`librg_entity_track`)
  and self-owns it (`librg_entity_owner_set(world, id, id)` — required so the
  id can later be used as a query owner) the first time it's seen, then keeps
  its librg chunk current every call (`librg_entity_chunk_set` +
  `librg_chunk_from_realpos`). `visible_from` calls `librg_world_query(world,
  self, radius, ...)` (growing the result buffer and retrying if it overflows,
  per the real `LIBRG_API`) and filters `self` back out of the result — librg's
  query always force-includes entities owned by the querying id, which is
  exactly `self` here. `remove()` calls `librg_entity_untrack`.
- **Off**: the original Phase 1 hand-rolled linear scan, for anyone who'd
  rather not pull in librg as a dependency. Same public interface either way.

librg's actual v7.4.0 API differs from the older article this doc originally
summarized (no `librg_world_create`-then-`LIBRG_WRITE_*` framing callbacks at
this call site — that layer still exists in librg for wire framing but isn't
needed here since we already have our own `S2C_EntitySnapshot` codec and only
wanted the interest query). The real, current API surface used is:
`librg_world_create/destroy`, `librg_config_chunk{size,amount,offset}_set`,
`librg_entity_track/untrack/tracked`, `librg_entity_owner_set`,
`librg_entity_chunk_set`, `librg_chunk_from_realpos`, and `librg_world_query`
(see `code/header/{general,entity,query}.h` in the librg repo).

`librg.h`'s `LIBRG_IMPL` translation unit lives in
`src/replication/librg_impl.c`, built as its own target (`vb_librg_impl`) so
the project's `-Werror` flags never see librg's C99 — same isolation pattern
as `vb_raygui_impl`.

Chunk size maps 1:1 to `InterestGrid`'s `cell_size_` (default 32 world units,
independent of the 32³ voxel chunk — coincidence, not a shared constant).

### Known limitation — world extent

librg chunk ids are bounded: `chunkamount.x * chunkamount.y * chunkamount.z`
must fit a signed 32-bit int internally, and `librg_chunk_from_realpos` casts
each axis to `int16_t` chunks. `interest.cpp` configures 1024 chunks/axis,
which keeps that product (~1.07e9) well under `INT32_MAX` while covering
±512 × `cell_size` world units per axis around the origin. An entity that
strays outside that range gets `LIBRG_CHUNK_INVALID` from librg and is simply
excluded from everyone's interest set until it moves back in range (no crash,
no exception — just silently not replicated). Large open worlds will need a
bigger `chunkamount` (traded against the `int32` chunk-id overflow risk above)
or a coordinate scheme with a movable local origin; neither exists yet.
