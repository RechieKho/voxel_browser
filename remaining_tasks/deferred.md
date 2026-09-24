## Deferred (post first-playable)

> Full reasoning for deferred (post-first-playable) items; linked from `REMAINING_TASKS.md`.

- ~~World persistence: region file format, save/load, chunk eviction to
  disk.~~ **Resolved 2026-09-25 (Phase 7.6)** for `voxel_browser_server`;
  see `REMAINING_TASKS.md`'s Phase 7.6 entry. Still deferred:
  `--singleplayer`'s integrated server has no `RegionStore` wired in, and
  region files carry no LZ4/zstd framing yet (RLE only).
- Account/auth token verification service (`auth_mode = token`).
- Audio subsystem + Lua sfx/music API.
- Server-side plugin hot-reload.
- Entity–entity physics, mounts, projectiles beyond basics.
- Client-side particle system beyond block-break puffs.
- Compression tuning (zstd), snapshot delta compression, bit-packed inputs.
- Dedicated server browser / master server list.
- Modding: multiple stacked content packs, dependency resolution.
- Rule-based decorative structure placement (trees, ruins, rock formations)
  for the worldgen decoration pass (`ARCHITECTURE_SPEC.md` §6 stage 6):
  structures authored in a dedicated external tool and imported into the
  content pack as a schematic, placed by declarative rules (neighbor-block
  constraints — e.g. "must be on dirt", clustering tendency, biome/density
  weighting) rather than every structure needing a hand-written procedural
  callback. Explicitly post-first-playable — depends on the Lua-driven
  worldgen pipeline itself (Phase 4.2/6) landing and settling first; noted
  now so the decoration-pass design leaves room for it. **Phase 6.14 landed
  the dependency** (`vb.worldgen.set_pipeline` + `vb.register_biome`'s
  `decoration` schematic entries, `vb/worldgen/pipeline.hpp`'s
  `DecorationEntry`) — this item itself is still not attempted. Two
  narrower gaps 6.14 left inside what it *did* ship, worth folding into
  whichever future session tackles this:
  - **Procedural/callback decoration.** 6.14's decoration is schematic-only
    (a fixed block-offset list) — a per-site Lua callback (e.g. "grow a
    randomized tree shape") can't run on a `WorldGenWorkerPool` worker
    thread (Lua/sol2 is strictly single-threaded; see `set_pipeline`'s own
    entry in Phase 6 above for the same constraint). Would need either a
    main-thread deferred-apply pass after a chunk comes back from a worker,
    or a per-worker `sol::state`, neither attempted.
  - **Cross-chunk decoration.** 6.14's decoration offsets landing outside
    the originating chunk are silently skipped (`WorldGenerator::generate`,
    `src/worldgen/generator.cpp`) — no structure can straddle a chunk
    boundary yet, unlike the spec's stage-6 framing ("runs once neighbors
    are generated so trees/structures may cross chunk borders").
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
