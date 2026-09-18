# Core Data Model — Full Reference

> Full detail for this topic; linked from `ARCHITECTURE_SPEC.md`. Ground truth — do not duplicate here.

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

