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
                 │  │ Cellulose meshing + render │  │ Sandboxed│ │
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
│       └── render/             # (client) Cellulose glue, camera, interpolation
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
| `vb_render`   | STATIC lib  | `vb_core`, raylib, cellulose                                 |
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
};
```

The **BlockRegistry** is authored in Lua (`vb.register_block{...}`), frozen at
server start, serialized into the join handshake, and reconstructed read-only on
the client.

### 5.3 Chunks

- Chunk size: **32×32×32** voxels (`CHUNK_DIM = 32`). Rationale: good SIMD/mesh
  batch size, aligns with Cellulose's expected chunk granularity, keeps a full
  uncompressed chunk at 2 KB (`uint16` × 32³ = 64 KiB — see compression below).
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
2. **Biome selection** — 2D temperature/humidity noise → biome id. Biomes are
   registered in Lua (`vb.register_biome{...}`) with surface/filler/stone block
   refs and a decoration list.
3. **Surface pass** — replace top solid voxels with biome surface/filler
   (grass over dirt, sand near water level, etc.).
4. **Carvers** — caves/ravines via 3D noise threshold (optional in base pack).
5. **Decoration / population pass** — runs once *neighbors are generated* so
   trees/structures may cross chunk borders. Deterministic per-chunk RNG seeded
   from `(seed, coord)`. Trees etc. are defined in Lua as schematics or
   procedural callbacks.
6. **Lighting** — initial sky/block light flood fill.

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
| `EntityKind`     | Lua-registered type id                            | drives client-side model selection     |
| `ScriptState`    | `LuaRef` table                                    | per-entity Lua data                    |
| `Health`, `Inventory`, `ItemStack` | ...                             | base-provided, Lua-extensible          |

Lua may attach arbitrary named data via `ScriptState`; it cannot define new C++
components but can register **entity kinds** with tick callbacks.

### 7.2 Systems (ordered per tick)

1. `IngestInputSystem` — drain per-player `InputCmd` queues, clamp/validate.
2. `ScriptPreTickSystem` — dispatch `on_tick` for entity kinds + global timers.
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
`EntityKind`, `RenderHandle`. The local player additionally has
`PredictedState` and an unacknowledged `InputCmd` history for reconciliation.

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
  authoritative `S2C_ChunkDelta`. On rejection the client rolls back.

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

- `vb.register_block(def) -> BlockId`
- `vb.register_item(def)`
- `vb.register_entity(kind_def)` — `on_spawn`, `on_tick`, `on_hit`, `on_death`
- `vb.register_biome(def)`
- `vb.worldgen.set_pipeline(node_tree_def)`
- `vb.register_craft(recipe)`

Runtime:

- World: `vb.world.get_block(x,y,z)`, `vb.world.set_block(x,y,z,id)`,
  `vb.world.raycast(origin, dir, max)`, `vb.world.spawn(kind, pos)`.
- Entities: `entity:get_pos()`, `entity:set_velocity()`, `entity:remove()`,
  `entity:get_inventory()`, component-ish accessors for base components.
- Players: `player:send_message(text)`, `player:open_ui(name, ctx)`,
  `player:give(itemstack)`, `player:get_name()`.
- Events (subscribe): `vb.on("player_join" | "player_leave" | "block_break" |
  "block_place" | "player_interact" | "chat" | "tick", handler)`. Handlers may
  return `false` to veto vetoable events.
- Scheduling: `vb.after(seconds, fn)`, `vb.every(seconds, fn)`.
- Storage: `vb.storage` — a persisted key/value table (JSON-backed) for pack
  world data.

### 10.4 UI API surface (client VM)

- `ui.define(name, layout_fn)` — layout described declaratively (panels, labels,
  buttons, lists, item grids, text inputs). The C++ side renders it with raygui.
- Callbacks: `on_click`, `on_change`, `on_close` — these send a `C2S_UiEvent`
  RPC to the server VM (`player:open_ui` context round-trips), so UI logic that
  matters is still server-authoritative. Purely cosmetic state stays local.

### 10.5 Event flow example (block break)

```
client click ─▶ C2S_BlockEdit ─▶ server: reach/tool check
  ─▶ Lua "block_break" event (veto?) ─▶ BlockEditSystem applies
  ─▶ chunk.revision++, light dirty, mesh dirty
  ─▶ Lua on_break callback (drops, sfx trigger, ...)
  ─▶ S2C_BlockEditResult + S2C_ChunkDelta to all interested players
```

---

## 11. Rendering (Client)

### 11.1 Stack

`raylib` owns the window, GL context, input, and 2D/UI draw. `Cellulose` owns
voxel meshing and chunk mesh management. `raygui` draws menus/HUD as an
immediate-mode overlay.

### 11.2 Chunk meshing

- Client keeps a `ClientChunkStore` mirroring replicated chunk data.
- On `ChunkAdd`/`ChunkDelta`, mark mesh dirty; a **mesh worker thread pool**
  builds greedy-meshed vertex buffers via Cellulose from `(block registry,
  block data, light volume, neighbor faces)`. Per-vertex light + AO baked in.
- Completed meshes are uploaded to GPU on the main thread (GL calls are
  single-threaded); a budget caps uploads per frame to avoid hitches.
- Frustum culling + distance culling per chunk. Optional simple occlusion via
  chunk visibility bitset from generation.
- Transparent blocks (water, leaves-as-cutout) drawn in a second pass, back to
  front.

### 11.3 Frame loop

```
poll input ─▶ sample InputCmd, push to history, send on lane 4
           ─▶ predict local player (integrate + voxel collision, shared code)
recv network ─▶ apply snapshots, reconcile local player, apply chunk deltas
interpolate remote entities at (server_time_est - interp_delay)
update dirty meshes (bounded)
render: sky ─▶ opaque chunks ─▶ entities ─▶ transparent chunks ─▶ particles
        ─▶ raygui HUD ─▶ open Lua-defined UI ─▶ debug overlay
present
```

Target 60 FPS decoupled from the 20 Hz server tick.

### 11.4 Camera & input

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
| Mesh pool (N)          | Cellulose greedy meshing from chunk snapshots              |
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
init.lua             requires blocks/*, entities/*, registers worldgen + biomes
blocks/*.lua         vb.register_block{...}  (dirt, grass, stone, sand, wood, leaves)
entities/*.lua       player defaults, dropped-item entity
ui/*.lua             inventory screen, pause menu content
textures/*.png       16×16 block textures; packed into an atlas client-side
```

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
| Connection flood        | resource exhaustion                 | GNS connection limits, handshake timeout, per-IP cap                 |

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
2. **Cellulose API fit**: **Resolved (2026-09-11).** Cellulose is a mature
   header-only lib (same maintainer). It **emits vertex data, not GPU buffers**:
   `ChunkMesh { vector<MeshVertex{position, normal, u, v, brightness, block_id,
   texture_id, occlusion}>, vector<u32> indices }`. The reusable seam is
   `greedy_mesh(vector<MeshSample>, size, block_scale, ao, weld) -> ChunkMesh`
   where `MeshSample { visible[6], block_id, brightness[6], texture[6],
   face_occlusion[6] }` — a flat grid we can fill from `ClientChunkStore`
   *without* adopting Cellulose's `World`. `cellulose/raylib.hpp` has
   `to_raylib_mesh`. **Plan: adopt `greedy_mesh` under `VB_WITH_MESHING`.**
   Blocker for turning it on now: Cellulose vendors `unordered_dense` as a git
   submodule (empty on a shallow clone) and its default `CELLULOSE_BUILD_DEMO`
   fetches raylib again — `Dependencies.cmake` needs `GIT_SUBMODULES` +
   `CELLULOSE_BUILD_DEMO=OFF`. Phase 2 ships a hand-rolled mesher
   (`vb/render/chunk_mesher`) with the same inputs/outputs so the swap is local.
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
   out of scope for v0 but the handshake reserves the field.
