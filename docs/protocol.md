# Voxel Browser — Wire Protocol Reference

> Normative reference for the wire format. Update this file in the **same commit**
> as any change to a struct in `inc/vb/protocol/`, and bump
> `kEngineProtocolVersion` in `cmake/version.hpp.in`.

Current `ENGINE_PROTOCOL_VERSION`: **11**.

- **11** — `S2C_Inventory` (107) payload defined (Phase 5.1, real inventory
  sync): `varint n`, `n × {u16 item, u16 count}`. Sent to one player whenever
  their inventory changes (currently: after `player:give()`); always a full
  snapshot, not a delta. `ClientSession::inventory()` keeps the latest copy.
  Closes the "no wire message syncing inventory contents to the client at
  all" gap `REMAINING_TASKS.md` 5.1 tracked — `player:get_inventory()`/
  `player:give()` (Phase 4.2) were server-Lua-only state until now.
- **10** — `S2C_TimeOfDay` (46) payload defined (Phase 5.4, day/night cycle):
  `u32 time_of_day`. The server advances a `time_of_day` clock every tick
  (`vb::world::advance_time_of_day`, `ServerSession::set_day_length_seconds`,
  default 1200s/day) and broadcasts it to every playing connection about once
  a second; `S2C_JoinAccept::time_of_day` already carried the initial value
  (since version 1) but nothing advanced it server-side or kept an
  already-connected client in sync until now.
- **9** — `S2C_PlayerJoin` (104), `S2C_PlayerLeave` (105), `S2C_PlayerList`
  (106) payloads defined (Phase 5.4, player list / join-leave messages).
  Broadcast to already-playing connections when a new player finishes the
  join handshake / disconnects; `S2C_PlayerList` is sent once to a newcomer
  listing everyone else already playing.
- **8** — `C2S_Chat` (100) payload defined (Phase 5.4, HUD chat box):
  `string text`, sent by a playing client. Routed server-side through
  `vb.on("chat", handler)` (veto); if allowed, broadcast to every playing
  connection as `S2C_Chat{"<name>: <text>"}`.
- **7** — `C2S_UiEvent` (102) payload defined (Phase 4.5, client UI VM):
  `ui_name`, `widget_id`, `event_kind`, `value_json`. Sent when a widget's
  `on_click`/`on_change`/`on_close` Lua callback calls
  `ui.send_event(...)`/`ui.close()`; routed server-side to
  `vb.on("ui_event", handler)`.
- **6** — Asset sync (Phase 4.4, spec §9): `C2S_AssetManifestRequest` (20),
  `S2C_AssetManifest` (21), `C2S_AssetRequest` (22), `S2C_AssetData` (23)
  payloads defined, AND the handshake sequence itself changes — three new
  states (`kAwaitingAssetManifestRequest`/`kAwaitingAssetRequest`/
  `kStreamingAssets` server-side; `kAwaitingAssetManifest`/`kSyncingAssets`
  client-side) sit between `S2C_AuthResult` and `C2S_Ready` unconditionally,
  not just an optional extra message — a structural, breaking change to the
  handshake, hence the version bump (not merely additive like 4/5).
- **5** — `S2C_BlockRegistry` (40) payload defined (Phase 4.3): sent between
  `C2S_Ready` and `S2C_JoinAccept` when the host opts in
  (`HandshakeServerHost::block_registry`); the dedicated server always opts
  in with its live (possibly Lua-extended) registry, `nullopt` (no frame)
  otherwise.
- **4** — `S2C_Chat` (101) + `S2C_OpenUi` (103) payloads defined (Phase 4.2,
  `player:send_message`/`player:open_ui`); no client handles them yet, but the
  wire format is real.
- **3** — `C2S_BlockEdit` (44) + `S2C_BlockEditResult` (45) payloads defined.
- **2** — `S2C_EntitySnapshot` gains `bool has_local` + trailing `EntityRecord local`
  (the recipient's own authoritative state, for client reconciliation);
  `C2S_InputBatch` (80) payload defined.
- **1** — initial shipped set.

## Conventions

- Little-endian for all fixed-width integers and IEEE-754 floats.
- `varint` = unsigned LEB128 (≤ 10 bytes). `svarint` = zig-zag + LEB128.
- `string` = `varint` length prefix + raw UTF-8 bytes (length capped at 64 MiB).
- `bool` = one byte, `0` or `1` (any other value is a decode error).
- Decoders are bounds-checked and return `Result<T, ProtocolError>`; a message
  that decodes but leaves trailing bytes is rejected (`kTrailingBytes`).

Primitives live in `inc/vb/protocol/byte_buffer.hpp` (`ByteWriter` / `ByteReader`).

## Envelope

`inc/vb/protocol/message.hpp` — every logical message is framed as:

| Field         | Type     | Notes                                        |
| ------------- | -------- | -------------------------------------------- |
| `type`        | `uint16` | `MessageType`                                |
| `flags`       | `uint16` | bitfield; `0x01` = payload is LZ4-compressed |
| `payload_len` | `uint32` | ≤ 16 MiB, else `kLengthExceeded`             |
| `payload`     | bytes    | `payload_len` bytes                          |

`read_frame()` reports `kShortBuffer` until the whole envelope+payload is
buffered, and yields `consumed` so a stream reader can advance.

## Lanes (GameNetworkingSockets)

`lane_for(MessageType)` maps each type to its lane:

| Lane | Name       | Reliability            | Message types                                  |
| ---- | ---------- | ---------------------- | --------------------------------------------- |
| 0    | `control`  | reliable ordered       | handshake, auth, chat, RPC, disconnect         |
| 1    | `world`    | reliable ordered       | block registry, chunk add/delta/remove, edits  |
| 2    | `snapshot` | unreliable (seq-gated)  | entity snapshots                               |
| 3    | `assets`   | reliable ordered       | asset manifest + file chunk transfer           |
| 4    | `input`    | unreliable (seq)        | `C2S_InputBatch`                               |

## Messages

### Handshake / control — `inc/vb/protocol/handshake.hpp` (implemented)

| Type (id)                | Fields                                                                 |
| ------------------------ | -------------------------------------------------------------------- |
| `C2S_Hello` (1)          | `u16 engine_protocol_version`, `u64 client_nonce`, `string client_version` |
| `S2C_ServerInfo` (2)     | `string pack_name`, `string pack_version`, `u16 engine_protocol_version`, `u16 tick_rate`, `string motd`, `u8 auth_mode` |
| `C2S_Auth` (3)           | `string player_name`, `string token` (empty when `auth_mode = none`)   |
| `S2C_AuthResult` (4)     | `bool ok`, `string reason`                                             |
| `C2S_Ready` (5)          | *(empty)*                                                              |
| `S2C_JoinAccept` (6)     | `u32 net_id`, `f64×3 spawn_pos`, `u64 world_seed`, `u32 time_of_day`   |
| `S2C_Disconnect` (7)     | `u8 reason`, `string message`                                          |

`auth_mode`: `0 = none`, `1 = token`. `reason`: see `DisconnectReason` (0 unknown,
1 server full, 2 protocol mismatch, 3 auth failed, 4 shutdown, 5 kicked,
6 timeout, 7 protocol error, 8 bad handshake).

### Snapshot — `inc/vb/protocol/snapshot.hpp` (implemented)

| Type (id)               | Fields                                                                 |
| ----------------------- | --------------------------------------------------------------------- |
| `S2C_EntitySnapshot` (60) | `u32 server_tick`, `u32 last_acked_input_seq`, `varint n` + `n×EntityRecord entered`, `varint n` + `n×EntityRecord updated`, `varint n` + `n×u32 removed`, `bool has_local`, `EntityRecord local` (only if `has_local`) |

`EntityRecord` = `u32 net_id`, `u16 kind`, `f64×3 pos`, `f32×2 rot` (yaw,pitch deg),
`f32×3 vel`, `u8 flags` (bit 0 = `on_ground`). Interest culling excludes the
recipient, so their own authoritative state rides in `local` for
prediction/reconciliation (spec §8.4).

### Input — `inc/vb/protocol/input.hpp` (implemented)

| Type (id)            | Fields                                                        |
| -------------------- | ------------------------------------------------------------ |
| `C2S_InputBatch` (80) | `varint n` (≤64) + `n×InputCmd cmds` (ascending `seq`)       |

`InputCmd` = `u32 seq`, `f32 dt`, `f32×3 move` (x=strafe, y=up/fly, z=forward, [-1,1]),
`f32 yaw`, `f32 pitch`, `u8 buttons` (bit0 jump, bit1 sprint, bit2 primary,
bit3 secondary, bit4 fly-up, bit5 fly-down). Sent every client frame; each batch
resends recent unacked commands. The server simulates any `seq` above the last it
has run and acks the highest via `S2C_EntitySnapshot.last_acked_input_seq`.

### Asset sync — `inc/vb/protocol/assetsync.hpp` (implemented)

| Type (id)                     | Fields                                                        |
| ------------------------------ | ------------------------------------------------------------ |
| `C2S_AssetManifestRequest` (20) | `hash known_manifest_hash` ({0,0} = nothing cached)          |
| `S2C_AssetManifest` (21)        | `hash manifest_hash`, `u64 total_bytes`, `varint n` + `n×AssetEntryRecord entries` (empty if `known_manifest_hash` matched) |
| `C2S_AssetRequest` (22)         | `varint n` + `n×hash missing` (empty = "I have it all")     |
| `S2C_AssetData` (23)            | `hash hash`, `u32 seq`, `u32 total_chunks`, `varint len` + `len` bytes (≤ ~48 KiB target, decode caps at 1 MiB) |

`AssetEntryRecord` = `string path`, `hash hash`, `u64 size`, `u8 kind` (0
script / 1 texture / 2 model / 3 ui / 4 sound / 5 data). `hash` = two raw
`u64`s (`lo`, `hi` — xxHash3-128 of the file's bytes).

Sent between `S2C_AuthResult` and `C2S_Ready` (spec §9, see the handshake
sequence below): the server always sends exactly one `S2C_AssetManifest`
reply; if it has no manifest at all (opted out, or built without
`VB_WITH_COMPRESSION`), `manifest_hash` is `{0,0}` and `entries` is empty.
The client always replies with exactly one `C2S_AssetRequest`; an empty
`missing` list (nothing to fetch) skips straight past `kSyncingAssets`.
`S2C_AssetData` chunks are paced by the server (a small per-tick send budget,
not real flow-control windowing) and verified by hash on the client before
being committed to its content-addressed cache — a mismatch aborts the
connection (spec §9.4). No model/texture/asset-kind-specific handling exists
downstream of the cache yet (no Lua `require`, no texture loader) — the
assembled virtual pack filesystem (`path -> bytes`) is exposed but unread.

### Block registry — `inc/vb/protocol/world.hpp` (implemented)

| Type (id)              | Fields                                                        |
| ----------------------- | ----------------------------------------------------------- |
| `S2C_BlockRegistry` (40) | `varint n` + `n × {string name, bool solid, bool opaque, bool liquid, u8 light_emission}` (index == `BlockId`) |

Sent between `C2S_Ready` and `S2C_JoinAccept` (Phase 4.3) only if
`HandshakeServerHost::block_registry` returns a value; `nullopt` (default)
sends nothing, so a host/test that never opts in is unaffected. The client
rebuilds a `world::BlockRegistry` from the records (in order, so ids match)
and swaps it into its `ClientChunkStore`. No model/texture/collision-shape
fields exist yet — those wait on asset sync (4.4) + the base pack (5.1).

### World editing — `inc/vb/protocol/world.hpp` (implemented)

| Type (id)               | Fields                                                        |
| ----------------------- | ----------------------------------------------------------- |
| `C2S_BlockEdit` (44)      | `u32 predicted_seq`, `u8 action` (0 break / 1 place), `svarint×3 pos` (world voxel), `u16 block` (place only) |
| `S2C_BlockEditResult` (45) | `u32 predicted_seq`, `bool accepted`, `svarint×3 pos`        |

Client applies the edit optimistically to its chunk mirror keyed by
`predicted_seq`, then rolls back if `accepted` is false. The authoritative block +
light change fans out to every interested player as an `S2C_ChunkDelta`; the
server validates reach (≤ 5.5 blocks from the eye), target validity, and
non-floating placement (a Lua `block_break`/`block_place` veto slots in at
Phase 4.2).

### Day/night — `inc/vb/protocol/world.hpp` (implemented)

| Type (id)            | Fields                |
| --------------------- | --------------------- |
| `S2C_TimeOfDay` (46)  | `u32 time_of_day`     |

Periodic update of the clock `S2C_JoinAccept::time_of_day` already seeds at
join (spec §5.4). `ServerSession` advances its own `time_of_day` once per
tick (`vb::world::advance_time_of_day`, day length configurable via
`ServerSession::set_day_length_seconds`, default 1200s/day) and broadcasts
`S2C_TimeOfDay` to every playing connection roughly once a second — coarser
than snapshots since the clock only needs to look smooth, not be exact every
tick. The client folds it into `ClientSession::time_of_day()`, which returns
the join-time value until the first update lands. Tick convention: 0 =
sunrise, `kTicksPerDay/4` = noon, `kTicksPerDay/2` = sunset,
`3*kTicksPerDay/4` = midnight, wrapping at `kTicksPerDay` (24000) —
see `vb::world::daynight.hpp`. `src/client/main.cpp` derives a simple
4-keyframe sky gradient color from it (`sky_color_for_time`) for the
`ClearBackground` behind the 3D view, plus an "HH:MM" readout in the debug
overlay.

### Chat / UI RPC — `inc/vb/protocol/chat.hpp` (implemented)

| Type (id)           | Fields                                                        |
| -------------------- | ------------------------------------------------------------ |
| `C2S_Chat` (100)     | `string text`                                                 |
| `S2C_Chat` (101)     | `string text`                                                 |
| `C2S_UiEvent` (102)  | `string ui_name`, `string widget_id`, `string event_kind` ("click"\|"change"\|"close"), `string value_json` |
| `S2C_OpenUi` (103)   | `string ui_name`, `string ctx_json`                           |
| `S2C_PlayerJoin` (104) | `u32 net_id`, `string name`                                 |
| `S2C_PlayerLeave` (105) | `u32 net_id`                                               |
| `S2C_PlayerList` (106) | `varint n`, `n × {u32 net_id, string name}`                 |

### Inventory sync — `inc/vb/protocol/inventory.hpp` (implemented)

| Type (id)             | Fields                                                     |
| ---------------------- | ---------------------------------------------------------- |
| `S2C_Inventory` (107)  | `varint n`, `n × {u16 item, u16 count}`                     |

Sent to one player whenever their inventory changes (currently only
`player:give()`, Phase 4.2). Always a full snapshot of every slot, not a
delta — mirrors `S2C_PlayerList`'s "just resend the whole thing" posture.
`ClientSession::inventory()` holds the latest copy client-side; the HUD
hotbar (`src/client/main.cpp`) reads it directly.

`S2C_Chat`/`S2C_OpenUi` sent by the server Lua runtime
(`player:send_message`/`player:open_ui`, Phase 4.2); `ctx_json` is the
pre-serialized JSON of the Lua `ctx` table. `C2S_UiEvent` (Phase 4.5) is sent
by the client's separate UI VM (`vb::script::UiRuntime`) when a widget's
`on_click`/`on_change`/`on_close` callback calls
`ui.send_event(...)`/`ui.close()`; the server routes it to
`vb.on("ui_event", handler)` (non-vetoable). `C2S_Chat` (Phase 5.4) is sent by
the client's HUD chat box; the server runs `vb.on("chat", handler)` as a veto
(default-allow when no pack/handler is attached — e.g. `--singleplayer`,
which has no `PackRuntime`) and, if not vetoed, broadcasts
`S2C_Chat{"<name>: <text>"}` (server-formatted, not the raw client `text`) to
every playing connection, sender included.

`S2C_PlayerJoin`/`S2C_PlayerLeave`/`S2C_PlayerList` (Phase 5.4) are
`ServerSession`-generated (no Lua involvement, same posture as chat's
server-side formatting): `S2C_PlayerList` is sent once to a client right when
it finishes joining, listing every other already-playing connection;
`S2C_PlayerJoin`/`S2C_PlayerLeave` are then broadcast to every other playing
connection as players come and go. The client folds join/leave into its chat
log as `"* <name> joined/left the game"` lines and keeps a live
`net_id -> name` map (`ClientSession::players()`) for a HUD player list.

## Handshake sequence

See `ARCHITECTURE_SPEC.md` §8.3 for the full diagram. Order:
`Hello → ServerInfo → Auth → AuthResult → AssetManifestRequest →
AssetManifest → AssetRequest → AssetData×N → Ready → BlockRegistry →
JoinAccept → initial ChunkAdd + EntitySnapshot`.

Implemented, transport-agnostic, in `inc/vb/net/handshake.hpp`:

- `ServerHandshake` — per-connection FSM: `AwaitingHello → AwaitingAuth →
  AwaitingAssetManifestRequest → AwaitingAssetRequest → StreamingAssets →
  AwaitingReady → Playing` (or `Closed`). `StreamingAssets` is unusual: it
  expects no incoming frame at all, paced instead by `pump_assets()` called
  once per tick from `ServerSession::tick()` (a small per-tick send budget,
  not literal byte-in-flight flow control) — every other state is purely
  reactive to `on_frame()`. Rejects out-of-order messages (`kBadHandshake`),
  protocol-version mismatch (`kProtocolMismatch`), a full server
  (`kServerFull`), and failed auth (`kAuthFailed`); `on_timeout()` →
  `kTimeout`.
- `ClientHandshake` — drives `Hello → Auth → AssetManifestRequest →
  AssetRequest → Ready` and exposes `ClientHandshakeStatus { Connecting,
  Authenticating, AwaitingAssetManifest, SyncingAssets, Syncing, Joined,
  Failed }` for the connect UI. Asset-sync mechanics (which hashes are
  missing, verifying + committing streamed chunks) are pushed out to a new
  `HandshakeClientHost` hook struct — `ClientHandshake` itself has no
  filesystem access; `ClientSession` wires it to a
  `vb::assetsync::ClientAssetCache` when one is supplied (optional 4th
  constructor parameter, `nullptr` by default — every pre-4.4 call site is
  unaffected and behaves as "already fully synced"). Any `S2C_Disconnect`
  fails the handshake with the server-provided message.

The `Transport` interface (`inc/vb/net/transport.hpp`) delivers whole framed
messages per lane. Backends: `LoopbackTransport` (in-process, tests +
integrated singleplayer) and `GnsTransport` (GameNetworkingSockets, real UDP,
`VB_WITH_NET`) — both implemented. `GnsTransport` maps each `Lane`'s
reliability (see `send_mode_for_lane`) straight onto GNS send flags; it does
not yet use GNS's own connection-lanes feature, so all reliable traffic shares
one ordered stream (a latency nuance, not a correctness issue — see
`REMAINING_TASKS.md` 1.2).

## Dependency pins

| Dependency            | Pinned tag | Notes                                    |
| --------------------- | ---------- | --------------------------------------- |
| raylib / raygui       | `5.5` / `4.0` | window/GL/input; version-matched      |
| EnTT                  | `v3.13.2`  | ECS                                     |
| tomlplusplus          | `v3.4.0`   | server.toml / client.toml loader        |
| doctest               | `v2.4.11`  | tests                                   |
| GameNetworkingSockets | `v1.6.0`   | Phase 1.2; needs a real protobuf install (vcpkg/apt/brew — not FetchContent-able, see `cmake/Dependencies.cmake`) + BCrypt (Windows) or OpenSSL (Linux/macOS) |
| zpl / librg           | `v18.1.4` / `v7.2.2` | Phase 1.4 spike — confirm API |
| FastNoise2            | `v0.10.0`  | Phase 2                                 |
| lz4 / xxHash          | `v1.9.4` / `v0.8.2` | Phase 4.4 manifest hashing (`XXH3_128bits`) is the first real consumer; both fetched under `VB_WITH_COMPRESSION`. xxHash is linked **before** lz4 in `src/core/CMakeLists.txt` on purpose — lz4 vendors its own private, older `xxhash.h` with no XXH3 API, and `#include <xxhash.h>` resolves against whichever `-I` entry comes first |
| Lua / sol2            | `v5.4.6` / `v3.3.0` | Phase 4                          |
| Cellulose             | `main`     | Phase 2.5 spike — pin a commit then     |
