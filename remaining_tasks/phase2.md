## Phase 2 — World State & Terrain Generation

> Full history for this phase; linked from `REMAINING_TASKS.md`. Ground truth for [x] items — do not duplicate here.

Goal: server generates terrain, streams chunks, client meshes and renders them.

### 2.1 Voxel data model (`vb_core/world`)  ✅

- [x] `PalettedChunkStore` — 0/1/2/4/8/16 bpv, auto width growth, homogeneous
      collapse, `compact()`; randomized round-trip test.
- [x] `Chunk` — block store + light volume (sky/block nibbles) + `DirtyFlags` +
      `GenState` + monotonic `revision`.
- [x] `World` — `ChunkCoord → Chunk` map, world-voxel get/set across boundaries,
      load/unload; implements `BlockSolidQuery`.
- [x] `BlockSolidQuery` interface. `BlockRegistry` (hardcoded base set; Phase 4
      → Lua). `kChunkDim = 32` + `index_of` / `address_of` coord math.

### 2.2 World generation (`vb_core/worldgen`)  ✅ (base pipeline)

- [x] Deterministic hand-rolled coherent noise (`vb/core/noise.hpp`) — integer
      hash + polynomial interpolation, no trig; `-ffp-contract=off` project-wide
      for cross-compiler reproducibility. (FastNoise2 remains the Phase 4
      Lua-pipeline backend behind `VB_WITH_WORLDGEN`.)
- [x] Fixed base pipeline (`WorldGenerator`): fBm heightmap → stone / dirt /
      grass, sand + water near sea level. Deterministic from (seed, coord).
- [x] `WorldGenWorkerPool` — N threads, dedup, `poll_completed()` handoff to the
      tick thread (mutex+CV queue; "lock-free" is aspirational, correctness first).
- [x] Determinism gate: `tests/unit/worldgen_test.cpp` hashes a fixed 4-chunk
      region (FNV-1a) against a committed golden — CI runs it on all 3 platforms.
- [x] Biome selection (2.2 step 2) + carvers + vein/scatter + decoration pass
      — deferred to the Lua pipeline (Phase 4); base pipeline is
      heightmap-only for now. Biome selection design updated 2026-09-17 to
      Voronoi-cell partitioning with adjacency-weighted probability
      (WFC-flavored, non-backtracking — see `ARCHITECTURE_SPEC.md` §6 stage
      2), not the originally-sketched continuous temperature/humidity noise.
      Landed 2026-09-18 as Phase 6.14 (`vb.worldgen.set_pipeline` +
      `vb.register_biome` + `vb/worldgen/biome_selector.hpp`) — see that
      item for the full writeup. The fixed base pipeline itself stays
      heightmap-only exactly as this bullet always said; the Lua pipeline is
      the opt-in layer on top, per 6.14's own "no call, no pipeline" posture.

### 2.3 Lighting  ✅ (per-chunk)

- [x] `LightEngine::relight_chunk` (`vb/world/lighting.{hpp,cpp}`): BFS sky-light
      flood (full-strength straight down, −1/step sideways) + block-light flood
      from emitters; `transmittance()` from the registry (opaque = 0, water dims
      by 2). Clears the light dirty flag, marks mesh dirty.
- [x] Cross-chunk **vertical** sky occlusion + relight-on-edit cascade
      (`relight_column` in `lighting.hpp`, 2026-09-15): a chunk's relight now
      uses its real neighbour above (or cascades down through a whole loaded
      column) instead of always assuming open sky. Fixed the false-bright
      band at chunk boundaries reported while mining. See `STATE.md` §8.
- [ ] Horizontal cross-chunk light propagation (sideways-only spill, e.g.
      under a horizontal overhang spanning a chunk border) is still
      per-chunk-only — smaller-magnitude follow-up, not attempted.

### 2.4 World replication (§8.5)  ✅

- [x] Messages: `S2C_ChunkAdd` (coord + revision + opaque payload),
      `S2C_ChunkDelta` (block + light change lists), `S2C_ChunkRemove` —
      `vb/protocol/world.{hpp,cpp}`, round-trip tested.
- [x] Palette container serialization (`vb/world/chunk_codec`): palette + RLE of
      (run, palette-index) + RLE'd light. LZ4 is the `MessageFlag::kCompressed`
      framing layer, wired when `VB_WITH_COMPRESSION` lands (not required for
      correctness; RLE already shrinks it well).
- [x] Per-player visible chunk set (`vb/world/chunk_interest.hpp`:
      `chunks_in_view` + `diff_chunk_sets`) → add/remove diff in
      `net/world_replicator.{hpp,cpp}`.
- [x] `ChunkLifecycleSystem` (`vb/world/chunk_lifecycle.{hpp,cpp}`):
      generate (worldgen pool) / light / load around players, unload when no
      player wants a chunk. Worker pool gained a `kSynchronous` mode for
      deterministic tests + the integrated server.
- [x] `ClientChunkStore` (`vb/world/client_chunk_store.{hpp,cpp}`): applies
      add/delta/remove, implements `BlockSolidQuery` for meshing + prediction.
- [x] Wired into `ServerSession` (`set_world_replicator`) + `ClientSession`
      (auto-mirrors chunk messages post-join). Integration test: a joined client
      mirrors the 27-chunk box around its spawn and reclaims it on move.

### 2.5 Client meshing  ✅ (hand-rolled, permanent — Cellulose evaluated and reverted)

- [x] **(spike)** Cellulose API — resolved in `ARCHITECTURE_SPEC.md §19 Q2`.
      Emits vertex data (`ChunkMesh`); reusable seam is `greedy_mesh(vector<
      MeshSample>, …)`. Wired in behind `VB_WITH_MESHING` (2026-09-16), but
      reverted after its more volatile greedy-merged vertex/index counts
      reproduced the NVIDIA VAO/VBO-churn crash documented in `STATE.md`
      §1/§8 — see §19 Q2's updated resolution note and `STATE.md` §8's 15th
      entry. The `VB_WITH_MESHING` flag and Cellulose `FetchContent` block
      were later removed outright (2026-09-17); not a planned swap-in
      anymore.
- [x] `vb/world/chunk_mesher.{hpp,cpp}`: face-culled cube mesh from a
      `ClientChunkStore` (reads neighbours across chunk borders), per-vertex
      light + ambient occlusion. `MeshData { MeshVertex[], u32 indices[] }` —
      renderer-neutral. Same I/O as `greedy_mesh` so the swap is local.
- [x] `vb/render/chunk_renderer.{hpp,cpp}`: main-thread meshing with a per-frame
      budget, raylib GPU upload (vertex colours from light × per-block tint),
      drop-on-unload, re-mesh on `revision` change.
- [x] Wired into the client: `--singleplayer` keeps an in-process
      `IntegratedGame` + `World` + worldgen pool + `WorldReplicator` alive; the
      render loop feeds player position back and draws the streamed, meshed
      terrain.
- [x] Mesh worker pool (`vb/world/chunk_mesh_worker_pool.{hpp,cpp}` +
      `chunk_mesh_snapshot.{hpp,cpp}`, 2026-09-15): CPU face-culling/AO now
      runs on background threads from a per-chunk+1-voxel-border snapshot;
      `ChunkRenderer::sync()` only does the GPU upload on the main thread.
      Fixes framerate drops while chunks stream in. See `STATE.md` §8.
- [ ] Frustum culling, transparent second pass, texture atlas (Phase 4) —
      follow-ups.

**Phase 2 exit:** connect to a server and fly around streamed, meshed terrain
(dirt/stone/grass/air) with correct chunk load/unload; determinism CI gate green.

**Phase 2 status (2026-09-11): met.** `voxel_browser --singleplayer` generates
terrain, streams it as chunks, meshes and renders it, and loads/unloads chunks
as the player moves. Determinism gate is green on all 3 platforms. Deferred to
later phases: biomes/carvers/decoration (Lua pipeline, Phase 4), cross-chunk sky
occlusion + relight-on-edit (Phase 3/5), texture atlas (Phase 4), LZ4 chunk
compression (`VB_WITH_COMPRESSION`). Greedy merge (Cellulose,
`VB_WITH_MESHING`) is no longer deferred-but-planned — it was tried, reverted
(2026-09-16, see 2.5 above), and the dependency removed outright
(2026-09-17); the mesh worker pool itself shipped (2026-09-15) with the
hand-rolled per-face mesher.

