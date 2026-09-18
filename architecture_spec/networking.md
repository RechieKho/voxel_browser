# Networking — Full Reference

> Full detail for this topic; linked from `ARCHITECTURE_SPEC.md`. Ground truth — do not duplicate here.

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

