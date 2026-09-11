# Voxel Browser — Wire Protocol Reference

> Normative reference for the wire format. Update this file in the **same commit**
> as any change to a struct in `inc/vb/protocol/`, and bump
> `kEngineProtocolVersion` in `cmake/version.hpp.in`.

Current `ENGINE_PROTOCOL_VERSION`: **3**.

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

### Not yet implemented

Asset sync (20–23), block registry (40), chat/UI (100–103) — types are reserved
in `MessageType`; payloads land in Phases 4–5.

## Handshake sequence

See `ARCHITECTURE_SPEC.md` §8.3 for the full diagram. Order:
`Hello → ServerInfo → Auth → AuthResult → (asset manifest/data) → Ready →
BlockRegistry → JoinAccept → initial ChunkAdd + EntitySnapshot`.

Implemented, transport-agnostic, in `inc/vb/net/handshake.hpp`:

- `ServerHandshake` — per-connection FSM: `AwaitingHello → AwaitingAuth →
  AwaitingReady → Playing` (or `Closed`). Rejects out-of-order messages
  (`kBadHandshake`), protocol-version mismatch (`kProtocolMismatch`), a full
  server (`kServerFull`), and failed auth (`kAuthFailed`); `on_timeout()` →
  `kTimeout`.
- `ClientHandshake` — drives `Hello → Auth → Ready` and exposes
  `ClientHandshakeStatus { Connecting, Authenticating, Syncing, Joined, Failed }`
  for the connect UI. Any `S2C_Disconnect` fails the handshake with the
  server-provided message.

The `Transport` interface (`inc/vb/net/transport.hpp`) delivers whole framed
messages per lane. Backends: `LoopbackTransport` (in-process, done) and
`GnsTransport` (GameNetworkingSockets, `VB_WITH_NET`, pending).

## Dependency pins

| Dependency            | Pinned tag | Notes                                    |
| --------------------- | ---------- | --------------------------------------- |
| raylib / raygui       | `5.5` / `4.0` | window/GL/input; version-matched      |
| EnTT                  | `v3.13.2`  | ECS                                     |
| tomlplusplus          | `v3.4.0`   | server.toml / client.toml loader        |
| doctest               | `v2.4.11`  | tests                                   |
| GameNetworkingSockets | `v1.4.1`   | Phase 1; pulls protobuf, needs OpenSSL  |
| zpl / librg           | `v18.1.4` / `v7.2.2` | Phase 1.4 spike — confirm API |
| FastNoise2            | `v0.10.0`  | Phase 2                                 |
| lz4 / xxHash          | `v1.9.4` / `v0.8.2` | Phase 2/4 codecs                |
| Lua / sol2            | `v5.4.6` / `v3.3.0` | Phase 4                          |
| Cellulose             | `main`     | Phase 2.5 spike — pin a commit then     |
