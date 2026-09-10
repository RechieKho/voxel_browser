# Voxel Browser — Wire Protocol Reference

> Normative reference for the wire format. Update this file in the **same commit**
> as any change to a struct in `inc/vb/protocol/`, and bump
> `kEngineProtocolVersion` in `cmake/version.hpp.in`.

Current `ENGINE_PROTOCOL_VERSION`: **1**.

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

### Not yet implemented

Asset sync (20–23), world (40–45), snapshot (60), input (80), chat/UI (100–103)
— types are reserved in `MessageType`; payloads land in Phases 2–5.

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
