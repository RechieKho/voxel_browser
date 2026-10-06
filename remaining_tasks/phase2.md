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
- [x] Horizontal cross-chunk light propagation — landed 2026-09-28, closing
      this phase's last remaining item (`relight_chunk`/`relight_column` were
      previously vertical-only: a chunk always assumed a closed border on
      every side except straight up). `LightEngine::Neighbours`
      (`inc/vb/world/lighting.hpp`) replaces the old bare `const Chunk *above`
      parameter with 5 optional fields (`above` plus new `north`/`south`/
      `east`/`west`) — a single-pointer constructor keeps every pre-existing
      `above`-only call site (including `relight_column`'s own) compiling
      unchanged. `relight_chunk` (`src/world/lighting.cpp`) seeds each of the
      4 new vertical *faces* from whichever horizontal neighbour is loaded,
      the same "attenuate by 1 step, `if (seeded > sky[i])` relax" shape the
      interior BFS already used, not the top face's special "straight down,
      no falloff" case (horizontal light always decays by 1 per step, matching
      how a purely-interior sideways step already behaved before this pass).
      `relight_column` now builds the full `Neighbours` set (via `find()`) at
      every level of its vertical cascade, so *any* relight — edit, initial
      load, or cascade — picks up whatever horizontal neighbours happen to be
      loaded at that moment, the same passive "use what's there" posture
      `above` already had.
      **Real reactive gap closed on top of that:** the passive form above
      only helps when a chunk happens to relight *after* its neighbour is
      already lit right; it does nothing for the common live-edit case (break
      one block near a border, and the chunk on the other side — already
      stably lit, with no other reason to ever relight again — never finds
      out). `relight_column_impl`'s new `push` parameter closes that: when a
      chunk in the cascade actually changes and a horizontal neighbour is
      loaded, it recursively relights that neighbour's whole column too
      (deferred until the whole triggering column finishes, so the pushed
      neighbour never reads a half-updated column back), reported through the
      same `on_relit` callback so `WorldReplicator`'s per-edit code sends the
      neighbour's own delta immediately, not on the next tick's separate
      revision-diff sweep. Bounded to exactly one hop, provably: every
      propagation step costs at least 1 of light's 0-15 range and a chunk is
      `kChunkDim` (32) blocks wide, so light that has just crossed one border
      has at most 14 of budget left — nowhere near enough to cross a second
      full-width chunk and reach a third one, so a pushed neighbour's own
      relight never tries to push again. Diagonal neighbours are still never
      touched directly (unchanged from before this pass) — any effect on one
      only ever arrives indirectly through whichever shared orthogonal
      neighbour pushes into it.
      Verified: full `vb_tests` 382/382 green (2 new `lighting_test.cpp`
      cases — a direct `relight_chunk` case proving sideways spill/falloff
      from a single `west` neighbour under an otherwise-sealed roof, and a
      `relight_column`-based end-to-end case proving an edit that opens a
      gap in one already-loaded chunk's ceiling automatically relights an
      already-stable neighbour on the other side of the border, without ever
      calling relight on that neighbour directly, and reports it via
      `on_relit`), clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server` (temporarily reconfigured `build-net-lua` with
      `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to
      this dir's OFF default afterward). **Deliberately out of scope, per the
      pre-existing vertical asymmetry this pass didn't reopen:** block light
      still doesn't cross chunk borders at all (vertically or horizontally) —
      only sky light does, matching what the vertical-only implementation
      already covered before this pass. The actual rendered result (a human
      mining sideways near a chunk border and watching light spill in
      correctly instead of a dark band) was **not** manually eyeballed — no
      GUI in this agent environment, same still-open caveat as every other
      rendering-adjacent pass in this file.

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

- [x] **(spike)** Cellulose API — resolved in `ARCHITECTURE_SPEC.md §18 Q2`.
      Emits vertex data (`ChunkMesh`); reusable seam is `greedy_mesh(vector<
      MeshSample>, …)`. Wired in behind `VB_WITH_MESHING` (2026-09-16), but
      reverted after its more volatile greedy-merged vertex/index counts
      reproduced the NVIDIA VAO/VBO-churn crash documented in `STATE.md`
      §1/§8 — see §18 Q2's updated resolution note and `STATE.md` §8's 15th
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
- [x] Frustum culling, transparent second pass — landed 2026-09-27 (texture
      atlas itself landed separately 2026-09-23, see Phase 4's own entry).
      New `inc/vb/render/frustum.hpp` (header-only, no raylib dependency,
      same "pure math, unit-tested without a GL context" posture as
      `entity_visual_layout.hpp`): `build_frustum()` derives the 6 view-frustum
      planes straight from camera basis vectors (position/forward/up/fovy/
      aspect/near/far) via the standard "cross product of the far-plane
      corner vectors" construction — deliberately *not* extracted from a
      combined view-projection matrix, which would tie this pure header to
      raylib/rlgl's internal row/column matrix convention; `aabb_in_frustum()`
      is the standard conservative "positive vertex" AABB-vs-plane test.
      `ChunkRenderer::draw()` (`src/render/chunk_renderer.cpp`) now takes the
      `Camera3D` being rendered with (signature change, one call site in
      `src/client/main.cpp`), builds a frustum from it every call (near/far
      hardcoded to 0.01/1000.0 to match `BeginMode3D`'s own un-overridden
      `RL_CULL_DISTANCE_NEAR/FAR` defaults — nothing in this codebase calls
      `rlSetClipPlanes`), and skips any chunk whose 32-block AABB is provably
      entirely outside it — no draw call at all for a culled chunk, not just
      an early depth-reject.
      Transparent second pass: `ChunkRenderer` now uploads **two** GPU models
      per chunk instead of one — `split_transparent()` (new, `chunk_renderer.
      cpp`) partitions a chunk's meshed quads by the same flat fallback-color
      alpha `fill_mesh_arrays` already used for vertex-color alpha (today:
      only `base:leaves`, `a=220` — see `fallback_color_for()`), since every
      quad's 4 vertices already share one `block_id` and are contiguous by
      construction (`chunk_mesh_snapshot.cpp`'s own per-face `first` numbering)
      -- not a per-texel alpha check, and not a new `BlockType` field. `draw()`
      renders every visible chunk's **opaque** model first (any order, the
      depth buffer alone sorts it out), then every chunk with transparent
      geometry a second time with `rlDisableDepthMask()` set and sorted
      back-to-front by chunk-center distance from the camera (`rlDrawRenderBatchActive()`
      flushes around the depth-mask toggle so it doesn't retroactively apply
      to already-batched pass-1 draws) — chunk granularity only, not
      per-triangle, matches this engine's block scale. `GpuChunk` is now two
      `GpuMesh` slots (`opaque`/`transparent`) instead of one `Model`+capacity
      pair; `upload_part()` (new) is the old single-mesh reuse-if-it-fits/
      recreate-if-it-doesn't logic, now run once per slot.
      Verified: full `vb_tests` 370/370 green (7 new `frustum_test.cpp` cases:
      ahead/behind/beside/beyond-far/nearer-than-near/straddling-the-boundary
      AABB cases plus one proving a non-normalized non-orthogonal `up` still
      works), clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server` (temporarily reconfigured `build-net-lua` with
      `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to this
      dir's OFF default afterward). The actual rendered result (a human
      walking around and watching off-screen chunks stop being drawn, and
      leaves/water blend correctly over terrain behind them) was **not**
      manually eyeballed — no GUI in this agent environment, same still-open
      caveat as every other rendering-adjacent pass in this file.

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

---

### Moved from `REMAINING_TASKS.md`'s core (dream, 2026-10-06)

> Verbatim text of the core file's "Remaining" list for this section at the
> time of the move; the core now keeps a one-line summary.

- [x] Horizontal cross-chunk light propagation — landed 2026-09-28, closing
      this phase's last remaining item. `LightEngine::Neighbours`
      (`inc/vb/world/lighting.hpp`) extends `relight_chunk`/`relight_column`
      from vertical-only to all 4 horizontal neighbours, plus a `push`
      parameter that reactively relights an already-stable neighbour when a
      live edit opens a gap near a border (bounded to one hop by light's
      0-15 range vs. 32-block chunk width). Block light still doesn't cross
      chunk borders (only sky light does) — deliberately out of scope.
- [x] Frustum culling + transparent second pass — landed 2026-09-27. New
      header-only `inc/vb/render/frustum.hpp` (`build_frustum`/
      `aabb_in_frustum`) culls chunks provably outside the camera's 6-plane
      frustum before any draw call; `ChunkRenderer` now uploads separate
      opaque/transparent `GpuMesh` slots per chunk (`split_transparent()`)
      and draws transparent geometry in a back-to-front sorted second pass
      with depth-mask disabled.
