# Voxel Browser — Wire Protocol Reference

> **Status: stub.** Normative reference for the wire format. Every message
> struct in `inc/vb/protocol/`, every lane, and the handshake are documented
> here, and this file MUST be updated in the same commit as any wire change
> (which also bumps `kEngineProtocolVersion` in `vb/core/version.hpp`).

Current `ENGINE_PROTOCOL_VERSION`: **1** (nothing on the wire yet).

## Envelope

Every message: `{ uint16 type, uint16 flags, uint32 payload_len }`, little-endian,
followed by `payload_len` bytes. (Implemented in Phase 1.2.)

## Lanes (GameNetworkingSockets)

| Lane | Name       | Reliability            | Carries                                        |
| ---- | ---------- | ---------------------- | --------------------------------------------- |
| 0    | `control`  | reliable ordered       | handshake, auth, registry, chat, RPC, errors   |
| 1    | `world`    | reliable ordered       | chunk add/update/remove, block edits, light    |
| 2    | `snapshot` | unreliable (seq-gated)  | entity snapshots, local-player reconciliation  |
| 3    | `assets`   | reliable ordered       | asset manifest + file chunk transfer           |
| 4    | `input`    | unreliable (seq)        | client → server `InputCmd` batches             |

## Handshake

See `ARCHITECTURE_SPEC.md` §8.3. Message-by-message breakdown lands with Phase 1.3.

## Dependency pins (record resolved commits when bumped)

| Dependency            | Pinned tag | Notes                                    |
| --------------------- | ---------- | --------------------------------------- |
| raylib                | `5.5`      | window/GL/input                          |
| raygui                | `4.0`      | version-matched to raylib               |
| EnTT                  | `v3.13.2`  | ECS                                      |
| doctest               | `v2.4.11`  | tests                                    |
| GameNetworkingSockets | `v1.4.1`   | Phase 1; pulls protobuf, needs OpenSSL   |
| zpl / librg           | `v18.1.4` / `v7.2.2` | Phase 1.4 spike — confirm API |
| FastNoise2            | `v0.10.0`  | Phase 2                                  |
| lz4 / xxHash          | `v1.9.4` / `v0.8.2` | Phase 2/4 codecs                |
| Lua                   | `v5.4.6`   | wrapped by `cmake/lua/CMakeLists.txt`    |
| sol2                  | `v3.3.0`   | binding layer (§19 Q1 resolved)          |
| Cellulose             | `main`     | Phase 2.5 spike — pin a commit then      |
