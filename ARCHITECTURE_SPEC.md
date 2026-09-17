# Voxel Browser — Architecture Specification

> Status: **Draft v0.1** — derived from `README.md`. This document describes the
> target architecture. The repository currently contains only a raylib bootstrap
> (`src/main.cpp`) and the CMake skeleton; see `REMAINING_TASKS.md` for the gap.

---

## 1. Purpose & Scope

Voxel Browser is a multiplayer-first voxel engine with an authoritative server
and a thin, content-agnostic client (the "browser"). The base engine ships
almost no game content. Blocks, items, world generation rules, entities, and UI
are defined by **Lua content packs** that live on the server and are streamed to
clients on connect.

This spec covers:

- Process topology and module boundaries
- The wire protocol (transport, channels, message types)
- Core subsystems: world model, world generation, ECS, networking/replication,
  asset sync, scripting, rendering, UI, physics
- Data-flow walkthroughs for the critical paths
- Threading, configuration, security, and testing strategy

Non-goals for the first playable base: persistence to disk beyond a simple
region format, server-side anti-cheat beyond input validation, dedicated
matchmaking/lobby services, and audio.

---

## 2. Design Principles

1. **Server authoritative.** The server owns all mutable game state. The client
   renders a replicated, interpolated view and predicts only its own player.
2. **Content is data, shipped over the wire.** The client trusts no local
   content. Everything gameplay-facing (block registry, textures, UI layouts,
   scripts) is synchronized from the server and content-addressed by hash.
3. **Thin client.** The client contains rendering, input, interpolation,
   prediction, and a sandboxed Lua VM for *client-side UI callbacks only*. No
   authoritative game logic runs on the client.
4. **Deterministic-ish core, tolerant netcode.** World generation is
   deterministic from `(seed, chunk coord, pack version)`. Movement uses
   client prediction + server reconciliation; other entities are interpolated.
5. **Small hard-coded surface.** The C++ engine exposes stable primitives
   (voxels, chunks, entities, events, network); gameplay is assembled in Lua.
6. **Modular build.** Engine code is a library; `client` and `server` are
   separate executables linking a shared `core` library.

---

## 3. Process Topology

```
                 ┌─────────────────────────────────────────────┐
                 │                  SERVER                      │
                 │  (authoritative, headless)                   │
                 │                                             │
   Lua content   │  ┌───────────┐   ┌──────────────┐            │
   pack (disk) ─▶│  │ Lua VM +  │   │  World Gen   │            │
                 │  │ bindings  │   │ (FastNoise2) │            │
                 │  └─────┬─────┘   └──────┬───────┘            │
                 │        │                │                    │
                 │  ┌─────▼────────────────▼──────┐             │
                 │  │  Game State (EnTT ECS)      │             │
                 │  │  + Voxel World (chunks)     │             │
                 │  └─────────────┬──────────────┘              │
                 │                │                             │
                 │  ┌─────────────▼──────────────┐              │
                 │  │ Replication (librg)        │              │
                 │  │ Asset Sync service         │              │
                 │  │ Transport (GNS)            │              │
                 │  └─────────────┬──────────────┘              │
                 └────────────────┼────────────────────────────-┘
                                  │  UDP (reliable + unreliable channels)
                 ┌────────────────┼────────────────────────────-┐
                 │                │           CLIENT ("browser") │
                 │  ┌─────────────▼──────────────┐               │
                 │  │ Transport (GNS)            │               │
                 │  │ Asset cache (hash-indexed) │               │
                 │  │ Replication receiver       │               │
                 │  └─────────────┬──────────────┘               │
                 │                │                              │
                 │  ┌─────────────▼──────────────┐               │
                 │  │ Client World (chunk store) │               │
                 │  │ Entity view + interpolation│               │
                 │  │ Local player prediction    │               │
                 │  └─────────────┬──────────────┘               │
                 │                │                              │
                 │  ┌─────────────▼──────────────┐  ┌──────────┐ │
                 │  │ Hand-rolled mesher + render│  │ Sandboxed│ │
                 │  │ raygui HUD / menus         │◀─│ Lua VM   │ │
                 │  │ Input capture              │  │ (UI only)│ │
                 │  └────────────────────────────┘  └──────────┘ │
                 └─────────────────────────────────────────────-─┘
```

The same binary may host an **integrated server** for singleplayer: the client
spawns an in-process server on `localhost` and connects to it through the normal
socket path. There is no separate singleplayer code path.

---

## 4. Repository & Module Layout (target)

```
voxel_browser/
├── CMakeLists.txt              # top-level: options, dependency fetch, add_subdirectory
├── PROJECT_NAME
├── cmake/                      # helper modules (Dependencies.cmake, Warnings.cmake)
├── inc/                        # public headers, mirrors src/ tree
│   └── vb/
│       ├── core/               # math, ids, result types, logging, config
│       ├── world/              # block registry, chunk, world, region io
│       ├── worldgen/           # noise pipeline, biome interface
│       ├── ecs/                # component definitions, system runner
│       ├── net/                # transport, channels, message codec, snapshots
│       ├── replication/        # librg glue, interest management
│       ├── assetsync/          # manifest, hashing, transfer state machine
│       ├── script/             # Lua VM wrapper, binding registration, event bus
│       ├── protocol/           # generated/handwritten wire structs + versions
│       └── render/             # (client) mesher glue, camera, interpolation
├── src/
│   ├── core/  world/  worldgen/  ecs/  net/  replication/  assetsync/  script/
│   ├── server/                 # server executable: main.cpp, tick loop, CLI
│   └── client/                 # client executable: main.cpp, render loop, UI, input
├── content/
│   └── base/                   # the shipped minimal content pack (Lua + textures)
│       ├── pack.toml           # pack manifest: name, version, entry script
│       ├── init.lua
│       ├── blocks/  entities/  ui/
│       └── textures/
├── tests/                      # unit + integration tests (Catch2 or doctest)
│   ├── unit/
│   └── integration/            # headless client<->server harness
└── docs/
    ├── ARCHITECTURE_SPEC.md    # this file (or repo root)
    ├── lua-api.md              # generated + hand-written Lua API reference
    └── protocol.md             # wire format reference
```

### CMake targets

| Target        | Type        | Links                                                        |
| ------------- | ----------- | ----------------------------------------------------------- |
| `vb_core`     | STATIC lib  | fastnoise2, entt, lua, gamenetworkingsockets, librg, xxhash |
| `vb_render`   | STATIC lib  | `vb_core`, raylib                                            |
| `voxel_browser_server` | EXE | `vb_core`                                                    |
| `voxel_browser` (client) | EXE | `vb_core`, `vb_render`, raygui                            |
| `vb_tests`    | EXE         | `vb_core`, test framework                                    |

Dependencies are pulled via `FetchContent` (pinned tags) with `find_package`
fallback, matching the existing raylib pattern in `CMakeLists.txt`.

---

## 5. Core Data Model

### 5.1 Identifiers

- `BlockId` — `uint16_t`, index into the per-session **block registry** sent by
  the server. `0` is always `air`. IDs are assigned by the server at pack load
  and are stable for the session's lifetime.
- `EntityId` — EnTT `entt::entity` server-side; a compact `uint32_t` network id
  on the wire (assigned by librg / the replication layer).
- `ChunkCoord` — `ivec3` in chunk units.
- `AssetHash` — 128-bit xxHash3 of file contents, hex-encoded in manifests.

### 5.2 Blocks

```cpp
struct BlockType {
    std::string   name;          // "base:stone" (namespaced)
    BlockId       id;            // session-assigned
    bool          solid;         // AABB collision participation
    bool          opaque;        // meshing / face-culling + light occlusion
    uint8_t       light_emission;// 0..15
    AABBSet       collision;     // full cube by default; Lua may override
    RenderModel   model;         // CUBE | CROSS | CUSTOM(mesh hash)
    TextureRef    faces[6];      // atlas coords resolved client-side
    LuaRef        on_interact;   // server-side callback (optional)
    LuaRef        on_break;
    LuaRef        on_place;
    float         max_damage;    // 0 = instant break (default); >0 = shared
                                  // damage-pool breaking, see §10.7
    TextureRef    crack_texture; // optional; unset falls back to the engine's
                                  // default generic crack atlas, see §10.7
};
```

The **BlockRegistry** is authored in Lua (`vb.register_block{...}`), frozen at
server start, serialized into the join handshake, and reconstructed read-only on
the client.

### 5.3 Chunks

- Chunk size: **32×32×32** voxels (`CHUNK_DIM = 32`). Rationale: good SIMD/mesh
  batch size, keeps a full uncompressed chunk at 2 KB (`uint16` × 32³ = 64 KiB
  — see compression below).
- Storage: `std::array<BlockId, 32*32*32>` behind a **palette-compressed**
  container:
  - `PalettedChunkStore`: per-chunk palette + bit-packed indices
    (1/2/4/8/16 bits per voxel). Homogeneous chunks (all air, all stone) collapse
    to a single palette entry and zero index storage.
- Per-chunk metadata: dirty flags (`terrain`, `light`, `mesh`), generation
  state (`Ungenerated → Generating → Generated → Populated`), light volume
  (`uint8` per voxel: 4 bits sky, 4 bits block), and a monotonically increasing
  `revision` used for delta replication.
- **World** = hash map `ChunkCoord → Chunk`, plus a load/unload manager driven by
  player interest spheres.

### 5.4 Lighting

Flood-fill light propagation (sky + block light), recomputed on chunk generation
and incrementally on block edits. Runs on the server world-gen/edit worker; the
resulting light volume is replicated alongside block data so the client does not
recompute it. Client meshing consumes it directly for per-vertex AO/light.

---

## 6. World Generation

Pipeline, executed on a pool of **worldgen worker threads**, deterministic from
`(world_seed, chunk_coord, pack_version)`:

1. **Base density / heightmap** — FastNoise2 node trees (FBM, ridged, domain
   warp) configured by the pack. C++ provides the noise evaluation; Lua provides
   the node-tree description and parameters via `vb.worldgen.set_pipeline{...}`.
2. **Biome selection — Voronoi cells, adjacency-weighted (WFC-flavored, not
   WFC-proper)**. The world is partitioned into Voronoi cells (a noise-driven
   cellular partition, independent of chunk boundaries — a cell is typically
   many chunks across). Each cell's biome is a weighted random draw from
   every `vb.register_biome{...}`'d biome, where the weight is that biome's
   base spawn probability **multiplied by adjacency-compatibility factors**
   against whichever neighboring cells are already resolved (e.g. desert
   next to tundra gets a near-zero multiplier, desert next to plains a
   normal one) — Lua supplies both the base probabilities and the adjacency
   table, the engine only does the weighted draw.
   - **Resolution order is coordinate-pure, not exploration-order.** Cells
     resolve in a canonical order derived from a hash of `(world_seed,
     cell_id)`, never from which direction a player approached — otherwise
     two players exploring the same seed from opposite directions could see
     different biome layouts at the same coordinates, breaking the
     `(world_seed, chunk_coord, pack_version)`-determinism every other stage
     in this pipeline relies on. Resolving a cell recursively resolves any
     not-yet-resolved neighbors that precede it in that order first
     (memoized per cell — cheap; only a handful of neighbors ever need it).
   - **Adjacency weights are soft multipliers, never hard exclusions** — a
     "very unlikely" pairing is a small weight (e.g. `0.02`), never a hard
     `0`. This guarantees at least one biome is always pickable, so cell
     resolution can never hit a contradiction and never needs backtracking
     or a restart — unlike textbook WFC, which allows genuine dead ends,
     this variant is deliberately built to always succeed in one pass. That
     trade (no global constraint solving, no backtracking) is what makes it
     safe for an infinite, lazily-generated world instead of a bounded,
     solved-all-at-once grid.
   - Biomes carry surface/filler/stone block refs and a decoration list, as
     before.
   - Optional, purely cosmetic: a short terrain/surface-block blend near
     remaining cell boundaries, since adjacency weighting controls *which*
     biomes can neighbor each other, not how abruptly the terrain itself
     transitions at the seam.
3. **Surface pass** — replace top solid voxels with biome surface/filler
   (grass over dirt, sand near water level, etc.).
4. **Carvers** — caves/ravines via 3D noise threshold (optional in base pack).
5. **Vein / scatter pass** — underground ore and other valuable-block
   placement. Distinct from decoration below (that's chunk-surface
   population like trees; this is subsurface scatter). Each biome (or the
   pack globally) registers entries shaped like `{block, target_rock,
   height_range, vein_size, spawn_rate}`; the engine deterministically
   scatters them per chunk (same `(seed, coord)`-seeded RNG as decoration)
   — Lua supplies the table, engine does the placement.
6. **Decoration / population pass** — runs once *neighbors are generated* so
   trees/structures may cross chunk borders. Deterministic per-chunk RNG seeded
   from `(seed, coord)`. Trees etc. are defined in Lua as schematics or
   procedural callbacks. **Further out (post this pipeline landing, see
   `REMAINING_TASKS.md`'s Deferred section):** a dedicated external structure
   tool exporting into the schematic format, plus declarative placement rules
   (neighbor-block constraints, clustering tendency, biome/density weighting)
   evaluated by this pass, instead of every structure needing a hand-written
   procedural callback.
7. **Lighting** — initial sky/block light flood fill.

Generated chunks are inserted into the world with `revision = 1` and flagged for
replication to any player whose interest sphere covers them.

---

## 7. Entity Component System (EnTT)

The server runs a fixed-tick simulation (default **20 Hz**, `TICK_DT = 50 ms`).

### 7.1 Components (base engine)

| Component        | Fields                                             | Notes                                  |
| ---------------- | ------------------------------------------------- | -------------------------------------- |
| `Position`       | `dvec3`                                           | world space, double precision          |
| `Velocity`       | `vec3`                                            | m/s                                    |
| `Rotation`       | `vec2` (yaw, pitch)                               |                                        |
| `AABB`           | `vec3 half_extents`, `vec3 offset`                | collision volume                       |
| `Collider`       | flags (gravity, step height, on_ground)           | voxel collision response               |
| `PlayerInput`    | ring buffer of `InputCmd` (seq, dt, move, look, buttons) | filled from client packets      |
| `PlayerTag`      | connection handle, account name, view distance    |                                        |
| `NetReplicated`  | network id, interest radius, last sent revision   | bridges to librg                       |
| `EntityKind`     | Lua-registered type id                            | drives client-side billboard sprite/animation selection (§11.3) |
| `ScriptState`    | `LuaRef` table                                    | per-entity Lua data                    |
| `Health`, `Inventory`, `ItemStack` | ...                             | base-provided, Lua-extensible          |

Lua may attach arbitrary named data via `ScriptState`; it cannot define new C++
components but can register **entity kinds** with tick callbacks.

### 7.2 Systems (ordered per tick)

1. `IngestInputSystem` — drain per-player `InputCmd` queues, clamp/validate.
   Fires `vb.on("player_input", handler)` here, before movement integration —
   a handler may veto (`return false`, drop this tick's input) or return a
   replacement input table (§10.6).
2. `ScriptPreTickSystem` — dispatch `on_tick` for entity kinds + global timers.
   `on_tick`/`on_spawn`/`on_hit`/`on_death` all receive the entity's
   `ScriptState` table as `self` (§10.3), so kind-local data survives between
   calls the way instance fields do on an object.
3. `MovementIntegrationSystem` — apply gravity, integrate velocity.
4. `VoxelCollisionSystem` — swept AABB vs. solid voxels, resolve penetration,
   set `on_ground`, apply step-up.
5. `BlockEditSystem` — apply queued authoritative block break/place ops, bump
   chunk revisions, enqueue light + mesh updates, fire Lua `on_break`/`on_place`.
6. `InterestManagementSystem` — update per-player visible chunk + entity sets.
7. `ReplicationSystem` — build per-player snapshots (see §8.4), hand to transport.
8. `ScriptPostTickSystem` — deferred Lua actions, entity spawn/despawn commits.
9. `ChunkLifecycleSystem` — load/generate/unload chunks around players.

### 7.3 Client-side ECS

The client uses a lightweight EnTT registry too, holding only:
`NetId`, `Position` (current + previous for interpolation), `Rotation`,
`EntityKind`, `RenderHandle` (a `SpriteVisual` def + the per-entity animation
state that picks a frame from it each draw — see §11.3). The local player
additionally has `PredictedState` and an unacknowledged `InputCmd` history for
reconciliation.

---

## 8. Networking

### 8.1 Layering

```
  Application messages (protocol structs, §8.3)
        │  serialize (little-endian, versioned)
        ▼
  Channel router  ── reliable / unreliable / chunk-stream / asset-stream
        │
        ▼
  GameNetworkingSockets  (ISteamNetworkingSockets, connection + lane mgmt)
        │
        ▼
  UDP
```

`librg` sits *beside* this, not under it: it computes interest/visibility and
produces entity create/update/destroy events. The replication system turns those
events into messages sent on the appropriate channel. librg's own serialization
is used for the compact entity component blobs; envelope + routing is ours.

### 8.2 Channels (GNS lanes)

| Lane | Name           | Reliability            | Carries                                            |
| ---- | -------------- | ---------------------- | ------------------------------------------------- |
| 0    | `control`      | reliable ordered       | handshake, auth, registry, chat, RPC, errors      |
| 1    | `world`        | reliable ordered       | chunk add/update/remove, block edits, light       |
| 2    | `snapshot`     | unreliable (seq-gated)  | entity snapshots, local-player reconciliation      |
| 3    | `assets`       | reliable ordered       | asset manifest + file chunk transfer              |
| 4    | `input`        | unreliable (seq)        | client → server `InputCmd` batches                 |

### 8.3 Connection Handshake

```
Client                                        Server
  │  Connect (GNS)                               │
  │ ───────────────────────────────────────────▶ │
  │  C2S_Hello{ engine_version, client_nonce }    │
  │ ───────────────────────────────────────────▶ │
  │           S2C_ServerInfo{ pack_name,          │
  │           pack_version, engine_version,       │
  │           tick_rate, motd, auth_mode }        │
  │ ◀─────────────────────────────────────────── │
  │  C2S_Auth{ player_name, token? }              │
  │ ───────────────────────────────────────────▶ │
  │           S2C_AuthResult{ ok, reason }        │
  │ ◀─────────────────────────────────────────── │
  │  C2S_AssetManifestRequest{}                   │
  │ ───────────────────────────────────────────▶ │
  │           S2C_AssetManifest{ [ {path,hash,   │
  │           size, kind} ... ], total_bytes }    │
  │ ◀─────────────────────────────────────────── │
  │  C2S_AssetRequest{ [hash...] (cache misses) } │
  │ ───────────────────────────────────────────▶ │
  │           S2C_AssetData{ hash, seq, bytes }   │  (streamed, lane 3)
  │ ◀───────────────────────────────────────────  │
  │  C2S_Ready{}                                  │
  │ ───────────────────────────────────────────▶ │
  │           S2C_BlockRegistry{ ... }            │
  │           S2C_JoinAccept{ your_net_id,        │
  │           spawn_pos, world_seed, time_of_day }│
  │ ◀─────────────────────────────────────────── │
  │           S2C_ChunkAdd × N  (initial area)    │
  │           S2C_EntitySnapshot (full)           │
  │ ◀─────────────────────────────────────────── │
  │  ── normal play loop ──                       │
```

### 8.4 Snapshot / replication model

- Every server tick, `ReplicationSystem` asks librg for the per-player delta of
  visible entities and emits `S2C_EntitySnapshot` on lane 2 containing:
  `server_tick`, `last_acked_input_seq` (for the local player), and a list of
  `{net_id, kind?, pos, rot, vel, flags, script_delta?}`.
- Snapshots are **unreliable**; each carries `server_tick`. The client keeps the
  two most recent and renders at `now - interpolation_delay` (default 100 ms,
  ≈2 ticks) by lerping position/rotation.
- Entity create/destroy is folded into the snapshot as explicit `spawn`/`despawn`
  records but re-sent until acked (tracked per player) to survive packet loss.
- **Local player**: server includes the authoritative state + the last processed
  input sequence. Client discards acked inputs, snaps to the authoritative
  state, and replays unacked inputs through the same movement+collision code
  (shared in `vb_core`) — client prediction with reconciliation.

### 8.5 World replication

- `S2C_ChunkAdd{ coord, palette, packed_blocks, light, revision }` — palette
  container serialized, then LZ4-compressed. ~0.5–3 KB typical.
- `S2C_ChunkDelta{ coord, base_revision, [ {index, block_id} ... ], light_patch }`
  — sent when a loaded chunk changes by a small number of voxels.
- `S2C_ChunkRemove{ coord }` — chunk left interest range.
- `C2S_BlockEdit{ predicted_seq, pos, action (break|place), block_id?, face }` —
  client request. Client may **optimistically** apply it locally; server
  validates (reach distance, tool, `on_break`/`on_place` Lua veto, protection)
  and replies with `S2C_BlockEditResult{ predicted_seq, accepted }` plus the
  authoritative `S2C_ChunkDelta`. On rejection the client rolls back. For a
  `max_damage > 0` block this is the *completion* of the shared-damage flow
  in §10.7, not the whole interaction — see that section for
  `C2S_BlockBreakBegin`/`...Stop` and how in-progress damage is replicated.

### 8.6 Time & tick sync

`JoinAccept` seeds `world_seed` and `time_of_day`. Server includes `server_tick`
in every snapshot; client maintains a smoothed estimate of server time for
interpolation. No lockstep.

---

## 9. Asset Sync Protocol

Goal: after connecting, the client has byte-identical copies of every file the
server's content pack needs, without re-downloading what it already cached from a
previous session or another server.

### 9.1 Manifest

Server, at startup, walks `content/<pack>/` and builds:

```
AssetEntry {
    string   path;      // pack-relative, forward-slashed, normalized
    AssetHash hash;     // xxHash3-128 of contents
    uint64   size;
    AssetKind kind;     // SCRIPT | TEXTURE | MODEL | UI | SOUND | DATA
}
```

The manifest is itself hashed (`manifest_hash`) so a client that reconnects can
skip the whole exchange with one comparison.

### 9.2 Client cache

Content-addressed store at `~/.cache/voxel_browser/assets/<hh>/<hash>` (OS-
appropriate dir). A small SQLite or flat index maps `hash → {size, last_used}`
for LRU eviction. The cache is shared across all servers — identical files are
downloaded once, ever.

### 9.3 Transfer

- Client sends `C2S_AssetRequest` with the list of hashes it is missing.
- Server streams `S2C_AssetData{ hash, seq, total_chunks, bytes }` on lane 3,
  round-robin across requested files, chunk size ~48 KB, with a bytes-in-flight
  window.
- Client verifies each completed file's hash before committing to cache; a
  mismatch aborts the connection with a protocol error.
- Progress is surfaced to the UI (`S2C` count + bytes vs. manifest totals) so the
  connect screen can show a progress bar.

### 9.4 Security

- Server refuses to include paths with `..`, absolute paths, or symlinks
  escaping the pack root.
- Client writes only into the content-addressed cache, never to arbitrary
  manifest paths; the `path` field is metadata used to build the virtual pack
  filesystem in memory, not a write target.
- Per-file and total size caps (configurable) reject hostile servers.
- Script assets are executed only inside the sandbox of §10.

---

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

---

## 11. Rendering (Client)

### 11.1 Stack

`raylib` owns the window, GL context, input, and 2D/UI draw. A hand-rolled
face-culled mesher (`vb::world::chunk_mesher` / `chunk_mesh_snapshot`) owns
voxel meshing and chunk mesh management — see §19 Q2 for why this is the
permanent choice rather than a placeholder for a greedy-mesh library.
`raygui` draws menus/HUD as an immediate-mode overlay.

### 11.2 Chunk meshing

- Client keeps a `ClientChunkStore` mirroring replicated chunk data.
- On `ChunkAdd`/`ChunkDelta`, mark mesh dirty; a **mesh worker thread pool**
  builds per-face-culled vertex buffers from `(block registry, block data,
  light volume, neighbor faces)`. Per-vertex light + AO baked in.
- Completed meshes are uploaded to GPU on the main thread (GL calls are
  single-threaded); a budget caps uploads per frame to avoid hitches.
- Frustum culling + distance culling per chunk. Optional simple occlusion via
  chunk visibility bitset from generation.
- Transparent blocks (water, leaves-as-cutout) drawn in a second pass, back to
  front.

### 11.3 Entity rendering — billboard sprites

**Decided (2026-09-11, §19 Q7): players and Lua entity kinds are 2D sprites, not
3D blocky models** — a *Don't Starve*-style presentation: a single flat billboard
per entity, with a handful of directional poses and per-state animation clips
standing in for full 3D animation, inside an otherwise fully 3D, free-look
first-person world (unlike Don't Starve's fixed isometric camera, so the
direction-selection math below has to do real work instead of being baked in at
authoring time).

**Billboarding.** Each entity draws as one `DrawBillboardPro(camera, atlas,
frame_rect, feet_position, up = {0,1,0}, size, origin, rotation = 0, tint)` call.
Passing a fixed world-up locks the quad vertical (it never leans with camera
pitch/roll); raylib still derives the quad's *right* edge from the camera's view
matrix, but that vector is always horizontal regardless of pitch (the cross
product of any forward vector with world-up has zero Y component) — so the quad
yaws to face the camera's bearing and nothing else. This is confirmed directly
from raylib 5.5's `rmodels.c`, not assumed; no custom quad math is needed. The
quad is anchored at the entity's feet (matching `Position`/`Collider`'s own
convention) and horizontally centered.

**Directional pose selection.** An entity kind declares `facings` (4 or 8,
default 8). Each frame, the client computes the bearing from the entity to the
camera in the world XZ plane, subtracts the entity's own facing yaw
(`Rotation.yaw`), normalizes to [0°, 360°), and buckets into `facings` equal
sectors to pick the pose — the classic "which side of you is the viewer standing
on" trick from isometric/2.5D games. A small hysteresis (the bucket must change
*and* persist a couple of frames, or a small angular deadzone at sector
boundaries) stops flicker when the viewer sits exactly between two sectors.
Packs only need to author the unique half of the poses — front, front-diagonal,
side, back-diagonal, back (5 sprites) for 8-way, or front/side/back (3) for
4-way — the engine mirrors the rest via `DrawBillboardPro`'s negative-`size.x`
horizontal flip, halving the art budget.

**Animation state.** Driven entirely by data already on the wire (§8.4's
`EntityRecord`), no extra round trip: horizontal speed from `vel` buckets
walk/run, and `flags` (currently only bit 0, `on_ground`) grows three more bits
— `dead`, `hurt_pulse` (edge-triggered: the client plays it once then the sender
clears it), `acting` — giving a fixed client-side priority resolution `dead >
hurt_pulse > acting > jump/fall > run > walk > idle`. Each clip is an ordered
list of atlas rects with an fps and a loop-or-hold-last-frame flag, defined per
entity kind, not hardcoded per clip name beyond that small base set. Wiring
the new `flags` bits bumps `kEngineProtocolVersion` and `docs/protocol.md` when
it happens (Phase 3.5) — this section records the *plan*, not a shipped wire
change.

**Where it's defined.** `vb.register_entity{ visual = {...} }` (§10.3, Phase
4.2) is the single source of truth per entity *kind*; the atlas PNG travels
through Asset Sync (§9, Phase 4.4) like any other texture, with no separate
synced manifest. Phase 3 ships first against a **hardcoded single-frame
placeholder** (`facings = 1`, a flat tint) so remote players are visible
before Lua/asset-sync exist, the same way Phase 2 shipped a hand-rolled mesher
ahead of Cellulose — real art (5.1) is a local swap once 4.2/4.4 land.

**Decided (2026-09-17): frame-size variants, spritesheet grid schema, and
per-instance override.** The atlas is a spritesheet, not one loose sprite per
pose, so the engine needs a declared frame size to slice it into a grid —
rather than accept arbitrary pixel dimensions, packs pick from a small closed
set of named variants (multiples of 128px, matching typical small/medium/large
entity silhouettes at a fixed pixels-per-metre ratio):

| variant | frame size (px) |
|---|---|
| `small` | 128×128 |
| `tall` | 128×256 |
| `flat` | 256×128 |
| `medium` | 256×256 |
| `medium_tall` | 256×512 |
| `medium_flat` | 512×256 |
| `large` | 512×512 |
| `large_tall` | 512×1024 |
| `large_flat` | 1024×512 |

These are an authoring/validation convenience, not an engine type — the
variant name just looks up a `{frame_width, frame_height}` pair used below.

**Kind-level default**, set via `vb.register_entity{ visual = {...} }`:

```lua
visual = {
  variant = "tall",              -- looked up -> frame_width/frame_height
  texture = "textures/entities/player.png",
  facings = 8,                   -- rows = floor(facings/2)+1 unique poses
                                  -- (§11.3 pose mirroring), engine mirrors rest
  origin = { x = 0.5, y = 1.0 }, -- normalized anchor within a frame (feet point)
  clips = {                      -- column layout, shared across every pose row
    { clip = "idle",   frames = 4, fps = 6  },
    { clip = "walk",   frames = 6, fps = 10 },
    { clip = "run",    frames = 6, fps = 14 },
    { clip = "jump",   frames = 1, fps = 1  },
    { clip = "fall",   frames = 1, fps = 1  },
    { clip = "acting", frames = 4, fps = 8  },
    { clip = "hurt",   frames = 2, fps = 10 },
    { clip = "dead",   frames = 1, fps = 1  },
  },
}
```

Row = pose index (the same index `select_pose()` already produces). Column =
a pose-row-local pixel offset, computed by the engine as a running sum over
`clips` in declared order — so `clip` names and per-clip frame counts/fps are
entirely pack-defined (no positional row/column convention to memorize), while
built-in kinds (`base:player`, `base:dropped_item`) simply ship their own
`visual` table as sensible defaults in `content/base`, the same mechanism a
third-party pack would use, not a special-cased engine path. The required
sheet size is derived and validated exactly at load time: `width ==
frame_width * sum(frames)`, `height == frame_height * (floor(facings/2)+1)` —
a mismatch is a pack load error, not a silent misdraw. A kind that omits a
clip `resolve_anim_clip()` can still resolve to (e.g. `base:dropped_item`
never running or jumping) falls back to the first declared clip (conventionally
`idle`) rather than erroring at runtime.

**Per-instance override.** Each spawned instance may carry its own partial
`visual` table in its `ScriptState` under a reserved key (`entity.
visual_override = {...}`), merged field-by-field over its kind's default —
this is the *only* per-instance case (e.g. a player skin overriding just
`texture` while inheriting `variant`/`clips`/`facings`/`origin` unchanged);
every other entity kind is expected to look the same across all its
instances and only needs the kind-level default. No new storage mechanism:
`ScriptState` already exists as arbitrary per-entity Lua data (§4's entity
component table above).

**Client-side.** `vb/render/entity_renderer` (sibling to `vb/render/chunk_renderer`)
keeps one `EntityRenderState` (clip, elapsed time, direction bucket + hysteresis
counter) per replicated `NetId`, updated from `ClientSession::remote_entities()`
+ `interpolated_pos()` (already built for §8.4). The local player is not drawn
as a billboard in first person (no viewmodel in scope).

**Explicitly out of scope for v1:** skeletal/vertex animation, per-limb
equipment layering, blob shadows, and dynamic per-entity lighting sampled from
the block underfoot (chunks already compute the light value that would need —
cheap follow-up, not core).

### 11.4 Frame loop

```
poll input ─▶ sample InputCmd, push to history, send on lane 4
           ─▶ predict local player (integrate + voxel collision, shared code)
recv network ─▶ apply snapshots, reconcile local player, apply chunk deltas
interpolate remote entities at (server_time_est - interp_delay)
update dirty meshes (bounded)
render: sky ─▶ opaque chunks ─▶ billboard entities (§11.3) ─▶ transparent chunks
        ─▶ particles ─▶ raygui HUD ─▶ open Lua-defined UI ─▶ debug overlay
present
```

Target 60 FPS decoupled from the 20 Hz server tick.

### 11.5 Camera & input

First-person camera driven by predicted local player transform. Mouse-look with
capture toggle. Keybindings configurable via a local (non-synced) settings file.

---

## 12. Physics

- **Broadphase:** entity AABB expanded by motion, intersected with the voxel
  grid to get candidate solid cells.
- **Narrowphase / response:** swept AABB against axis-aligned voxel faces,
  resolved per axis (X, then Z, then Y or a variant) to give clean wall-slide and
  ground behavior. Step-up height for players (0.6 m). Gravity from config.
- Runs identically on server (`VoxelCollisionSystem`) and client (prediction) —
  the routine lives in `vb_core` and takes a `BlockSolidQuery` interface so both
  sides feed it their chunk store.
- No entity–entity physics in the first playable base beyond simple push-out for
  players (optional).

---

## 13. Threading Model

### Server

| Thread(s)              | Work                                                        |
| ---------------------- | --------------------------------------------------------- |
| Main / tick            | ECS systems, Lua VM, block edits, replication build        |
| Network I/O            | GNS poll, (de)serialization, per-connection send queues    |
| Worldgen pool (N)      | chunk generation + initial lighting (pure, deterministic)  |
| Asset stream           | reading pack files, chunking, hashing at startup           |

Generated chunks and incremental light results are handed back to the tick
thread via lock-free queues; the Lua VM is touched only from the tick thread.

### Client

| Thread(s)              | Work                                                        |
| ---------------------- | --------------------------------------------------------- |
| Main / render          | input, prediction, GL, raygui, mesh upload, client Lua VM  |
| Network I/O            | GNS poll, decode, apply to double-buffered state           |
| Mesh pool (N)          | hand-rolled face-culled meshing from chunk snapshots       |
| Asset writer           | verify hash + write cache files                            |

---

## 14. Serialization & Protocol Versioning

- Hand-written little-endian codecs in `vb/protocol/`, one `read`/`write` per
  message struct, plus varint helpers. No RTTI, no exceptions on the hot path;
  decode returns `Result<T, ProtocolError>`.
- Every message envelope: `{ uint16 type, uint16 flags, uint32 payload_len }`.
- `ENGINE_PROTOCOL_VERSION` bumped on any wire change; handshake rejects
  mismatches with a human-readable reason.
- Content-pack compatibility is separate: `pack_version` string is informational;
  the block registry is always sent in full, so old clients cannot desync on
  content.
- `docs/protocol.md` is the normative reference and must be updated with each
  message change.

---

## 15. Configuration

### Server (`server.toml`, CLI overrides)

```toml
bind_address   = "0.0.0.0"
port           = 27015
content_pack   = "content/base"
max_players    = 16
view_distance  = 8          # chunks
tick_rate      = 20
world_seed     = 0          # 0 = random
gravity        = 24.0
asset_max_file_mb   = 32
asset_max_total_mb  = 512
```

### Client (`client.toml`, local only, never synced)

Window size, vsync, FOV, render distance (clamped to server), mouse sensitivity,
keybindings, asset cache size cap, last-connected servers list.

---

## 16. Content Pack Format (`content/base`)

```
pack.toml            name, version, engine_version_req, entry = "init.lua"
init.lua             pack-wide setup; always loaded last (host-driven, see below)
*.lua (pack root)    any other top-level module (e.g. crafting.lua) — loaded
                     sorted, after blocks/entities/biomes, before init.lua
blocks/*.lua         vb.register_block{...}  (dirt, grass, stone, sand, wood, leaves)
entities/*.lua       player defaults, dropped-item entity, visual = {...} (§11.3)
ui/*.lua             inventory screen, pause menu content
textures/*.png       16×16 block textures; packed into an atlas client-side
textures/entities/*.png  billboard sprite atlases (§11.3), directional × animation frames
```

Real `require` doesn't exist yet (§10.2), so a pack can't pull its own files in
from `init.lua` the way this layout might imply — the host
(`vb::script::load_content_pack`, `src/script/pack_loader.cpp`) walks the
directories and any other root-level `*.lua` file itself, in a fixed order,
loading each into the same Lua state as one shared chunk sequence. The engine
has no idea what any of these files are *for* beyond that fixed load order —
`content/base/crafting.lua` (an ordinary root-level module, not a special
case) is the reference example of a pack building a real gameplay system
(crafting recipes) entirely in content, with no engine-side game logic at
all.

The base pack is the reference implementation of the Lua API and the smoke-test
content for CI.

---

## 17. Security Considerations

| Surface                 | Threat                              | Mitigation                                                            |
| ----------------------- | ---------------------------------- | ------------------------------------------------------------------- |
| Asset manifest paths    | path traversal, symlink escape      | normalize + reject `..`/abs/symlink; client writes only to CAS       |
| Asset sizes             | disk-fill DoS                       | per-file + total caps; progress abort                                |
| Lua (server pack)       | host compromise via `os`/`io`       | sandbox env, reimplemented `require`, no bytecode load               |
| Lua (client UI)         | malicious server script on client   | separate restricted VM, no world/net/fs, instruction + time budget   |
| Packet decode           | malformed input crash               | bounds-checked `Result` codecs, fuzz targets, size caps              |
| Block edits             | reach hacks, protected-area grief   | server reach check, tool check, Lua veto, per-region protection API  |
| Input flood             | CPU DoS                             | per-connection input rate limit, `InputCmd` count clamp per tick     |
| Custom keybind flood    | wire bandwidth / dispatch DoS       | closed schema (bounded bitset, registered set only, §10.6), not a post-receipt filter; per-connection rate limit on top |
| Connection flood        | resource exhaustion                 | GNS connection limits, handshake timeout, per-IP cap                 |
| Pack-implemented auth   | weak/pure-Lua credential hashing    | engine offers `vb.crypto.hash` (§10.6) so packs aren't rolling their own; engine itself takes no position on auth |
| Block-break begin/stop spam | CPU DoS via many concurrent damage-pool entries | `C2S_BlockBreakBegin` still gated by the same reach/tool/protection checks as `C2S_BlockEdit`; sparse map size is bounded by actual concurrent contributors, not attacker-controlled growth (§10.7) |

The engine assumes a **trusted server operator** but an **untrusted network and
untrusted clients**. Client sandboxing protects players from malicious servers
to the extent practical (no code exec, no arbitrary FS writes).

---

## 18. Testing Strategy

- **Unit** (`tests/unit/`): palette chunk store round-trips, protocol codec
  round-trips + fuzz, voxel collision cases, worldgen determinism
  (same seed → same chunk hash), asset hashing, Lua sandbox escape attempts.
- **Integration** (`tests/integration/`): headless server + headless client
  (`--headless` renders nothing) in one process; assert join handshake, asset
  sync of the base pack, chunk streaming, a scripted block break propagates to a
  second client, prediction reconciliation converges.
- **Determinism gate in CI**: generate a fixed region, hash it, compare against a
  committed golden value across Linux/macOS/Windows (existing build matrix).
- **Soak**: N simulated clients doing random walks + edits for M minutes,
  watch for leaks (ASan/LSan) and unbounded queue growth.
- Frameworks: doctest or Catch2 for unit; a small custom harness for integration.
  Sanitizer builds wired into the debug CI matrix.

---

## 19. Open Questions

1. **Binding layer**: raw Lua C API vs. `sol2`. **Resolved (2026-09-10): sol2**
   (v3.3.0), for ergonomics; compile-time cost accepted.
2. **Cellulose API fit**: **Resolved (2026-09-11), then reversed
   (2026-09-16), and the dependency fully removed (2026-09-17) — hand-rolled
   meshing is the permanent backend.** Cellulose (a header-only greedy
   mesher) was spiked behind a build flag: it emitted vertex data, not GPU
   buffers, via a reusable `greedy_mesh(vector<
   MeshSample>, size, block_scale, ao, weld) -> ChunkMesh` entry point, fed
   from a flat grid filled from `ClientChunkStore`. It was wired in and
   passed the full test suite, but crashed in real play: greedy-merged
   output has far more volatile per-edit vertex/index counts than the
   hand-rolled per-face mesher (merging means one edit near a merge boundary
   can swing a whole face's quad count a lot), which defeated
   `chunk_renderer.cpp`'s GPU-buffer-headroom mitigation for the NVIDIA
   VAO/VBO-churn heap-corruption bug (see `STATE.md` §1/§8) and reproduced
   it far more often. Reverted, and Cellulose's build-flag/FetchContent
   scaffolding was later removed outright rather than kept dormant —
   `vb/world/
   chunk_mesher` + `chunk_mesh_snapshot`'s hand-rolled per-face mesher
   (fixed 4 vertices/6 indices per visible face, so an edit's effect on GPU
   buffer size stays small and local) is the permanent choice, with no
   planned swap-in. Full story: `STATE.md` §8's 15th entry (2026-09-16) and
   the 2026-09-17 removal entry.
3. **librg version / API**: **Resolved (2026-09-10): librg v7.4.0** (single
   self-contained header, zpl bundled). Interest is chunk-radius based (cells
   independent of voxel chunks). **Use librg for interest culling + its
   create/update/remove framing; keep our own per-entity payload codec and
   envelope/lane routing.** Not yet wired — Phase 1 ships a hand-rolled
   `InterestGrid` with the same diff semantics behind a narrow interface. Full
   write-up in `docs/replication.md`.
4. **Chunk compression**: LZ4 vs. zstd vs. palette-only. Start LZ4, measure.
5. **Persistence**: region file format for world save — deferred past first
   playable, but the chunk store should not assume in-memory-forever.
6. **Account/auth**: `auth_mode = none | token` — token verification service is
   out of scope for v0 but the handshake reserves the field. **Direction set
   (2026-09-17, not yet implemented):** the engine will not own an auth
   concept at all — `vb.db` (§10.6) gives packs a generic per-key store, and
   any login flow (recognizing a returning player, credential checks) is
   entirely pack-implemented on top of it plus the UI/input APIs. This
   `auth_mode` field stays reserved for a future *transport-level* token
   check, which is a different, lower-level concern than pack-level identity.
7. **Entity visual presentation**: 3D blocky models vs. 2D sprites.
   **Resolved (2026-09-11): Don't Starve-style Y-axis-billboarded sprites**, not
   blocky models — full design in §11.3. Key parameters locked in: raylib
   `DrawBillboardPro` with a fixed world-up (verified against `rmodels.c`, no
   custom quad math); default 8 discrete facings authored as 5 unique poses +
   engine-side mirroring; animation state resolved client-side from
   `EntityRecord.vel` + `flags` bits (`on_ground` existing, `dead`/`hurt_pulse`/
   `acting` new — not yet wired on the wire, see `REMAINING_TASKS.md` 3.5).
   Deferred to implementation time: exact clip-name base set beyond the priority
   list itself, and whether a blob-shadow decal ships alongside v1 or later.
