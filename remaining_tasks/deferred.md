## Deferred (post first-playable)

> Full reasoning for deferred (post-first-playable) items; linked from `REMAINING_TASKS.md`.

- ~~World persistence: region file format, save/load, chunk eviction to
  disk.~~ **Resolved 2026-09-25 (Phase 7.6)** for `voxel_browser_server`;
  see `REMAINING_TASKS.md`'s Phase 7.6 entry. ~~Region files carry no
  LZ4/zstd framing yet (RLE only).~~ **Resolved 2026-09-28** — see
  `ARCHITECTURE_SPEC.md` §18 row 5. ~~`--singleplayer`'s integrated server
  has no `RegionStore` wired in.~~ **Resolved 2026-09-28** — see
  `STATE.md`\'s history (`state/changelog-*.md`) for the full writeup: `Singleplayer`
  (`src/client/main.cpp`) now owns its own `RegionStore` under a fixed
  `world_singleplayer/` directory, wired into its `WorldReplicator` exactly
  like the dedicated server's own `region_store`, with a matching 60s
  periodic autosave sweep plus an unconditional final save in its
  destructor. No `client.toml` surface to disable it yet (the dedicated
  server's `persist_world` toggle has no singleplayer equivalent) — a real
  follow-up, not a cut corner of this pass.
- Account/auth token verification service (`auth_mode = token`).
- Audio subsystem + Lua sfx/music API.
- Server-side plugin hot-reload.
- Entity–entity physics, mounts, projectiles beyond basics.
- Client-side particle system beyond block-break puffs.
- Compression tuning (zstd), snapshot delta compression, bit-packed inputs.
- Dedicated server browser / master server list.
- Modding: multiple stacked content packs, dependency resolution.
- ~~Rule-based decorative structure placement (trees, ruins, rock formations)~~
  **Done** (`docs/structure-editor.md`, phases S0–S7): structures are data
  files (`vb.register_structure`, `structures/*.lua`) authored in the
  standalone `vb_structure_editor`, placed by declarative per-biome rules with
  cross-chunk "pull" stamping (`architecture_spec/worldgen.md` §6 stage 6);
  `content/base` now ships oak and birch trees, a bush and a boulder made with
  it. Left out of that work: the optional in-game export command (capture a
  selected region from singleplayer into a structure file), structure-to-
  structure avoidance across rules, preview on a pack's real biomes, and
  structures bigger than 64 blocks per side (a jigsaw/assembly system).
- Runtime/callback decoration (narrowed from the item above): the structure
  editor covers procedural *variety* by baking seeded variants at authoring
  time, so what stays deferred is decoration that has to react to its
  surroundings at generation time (e.g. a vine that follows the cliff face).
  A per-site Lua callback can't run on a `WorldGenWorkerPool` worker thread
  (Lua/sol2 is strictly single-threaded; see `set_pipeline`'s own entry in
  Phase 6 for the same constraint), so it would need a main-thread
  deferred-apply pass after a chunk comes back from a worker, or a per-worker
  `sol::state`; neither is attempted.
- Voronoi biome-cell resolution result caching (`vb/worldgen/
  biome_selector.hpp`'s `BiomeSelector::resolve`, Phase 6.14): deliberately
  recomputes its bounded neighbor-adjacency recursion from scratch on every
  call instead of memoizing across calls, trading cache-hit-rate for zero
  shared mutable state across `WorldGenWorkerPool` worker threads (no lock
  needed). If profiling ever shows this matters (repeated nearby-column
  queries within the same cell redo the same cheap recursion every time),
  a per-pipeline, mutex- or shard-guarded cache is the natural follow-up —
  not attempted here since the recursion is "only a handful of neighbors"
  per the design note and no perf problem has actually been observed.
- CSS-like declarative layout for `vb::script::UiRuntime` widgets
  (user-suggested, 2026-09-18): today every widget is placed with absolute
  pixel `x`/`y`/`w`/`h` (`content/base/ui/*.lua`, `docs/lua-api.md`) — a pack
  author does all positioning/responsiveness math by hand, including reading
  `client.screen_size()` (Phase 6.16) themselves to center anything. A
  flexbox/grid-flavored layout model (parent/child nesting, percentage or
  `flex`-style sizing, anchors) would let Lua describe *intent* ("centered",
  "fill remaining space", "bottom-right corner") instead of arithmetic,
  and would resize correctly with the window without every screen
  reimplementing that math. Explicitly post-first-playable — the current
  absolute-position model is sufficient for the existing modal screens/HUD;
  this is a bigger `UiRuntime`/`UiRenderer` redesign (a layout pass computing
  final `x`/`y`/`w`/`h` before widgets reach the renderer, most likely) worth
  doing once there's enough real UI content to justify it, not before.
