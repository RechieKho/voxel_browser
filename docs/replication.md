# Voxel Browser — Replication & Interest Management

> Companion to `docs/protocol.md`. Covers entity replication: interest
> management, the snapshot message, and the librg spike (spec §19 Q3).

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

### Decision (§19 Q3)

**Adopt librg for interest culling + create/update/remove framing; keep our own
codec for the per-entity payload and our own envelope/lane routing.** Rationale:
its chunk-radius query and delta bookkeeping (re-send-until-acked, per-owner
visibility) are exactly what §8.4 needs and are painful to reimplement well; the
payload stays ours so wire changes still flow through `docs/protocol.md` +
`kEngineProtocolVersion`.

**Not yet wired in.** The Phase 1 hand-rolled `InterestGrid` implements the same
diff semantics behind a narrow interface, so swapping librg underneath it later
is localised. Turn on `-DVB_WITH_REPLICATION=ON`; `Dependencies.cmake` fetches
librg `v7.4.0` only (zpl is bundled). Chunk-size config should map one librg cell
to the interest-cell size, not the voxel chunk.

### Open items for the integration pass

- librg cell coords are `int16`; clamp/verify world extent fits.
- `librg_world_write` needs a caller-sized buffer; size it from
  `max_entities_in_view * (record_size + librg_overhead)` and handle
  `LIBRG_WRITE_REJECT` (buffer full) by growing + retrying.
- librg is C with `-Wunused-parameter` noise — compile it as its own target
  without the project warning flags (same pattern as `vb_raygui_impl`).
