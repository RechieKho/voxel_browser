# World Generation Pipeline — Full Reference

> Full detail for this topic; linked from `ARCHITECTURE_SPEC.md`. Ground truth — do not duplicate here.

## 6. World Generation

Pipeline, executed on a pool of **worldgen worker threads**, deterministic from
`(world_seed, chunk_coord, pack_version)`:

1. **Base density / heightmap** — FastNoise2 node trees (FBM, ridged, domain
   warp) configured by the pack. C++ provides the noise evaluation; Lua provides
   the node-tree description and parameters via `vb.worldgen.set_pipeline{...}`.
2. **Biome selection — Voronoi cells, adjacency-weighted (WFC-flavored, not
   WFC-proper)**. The world is partitioned into Voronoi cells (a noise-driven
   cellular partition, independent of chunk boundaries — a cell is typically
   many chunks across). Each cell's biome is a weighted random draw from
   every `vb.register_biome{...}`'d biome, where the weight is that biome's
   base spawn probability **multiplied by adjacency-compatibility factors**
   against whichever neighboring cells are already resolved (e.g. desert
   next to tundra gets a near-zero multiplier, desert next to plains a
   normal one) — Lua supplies both the base probabilities and the adjacency
   table, the engine only does the weighted draw.
   - **Resolution order is coordinate-pure, not exploration-order.** Cells
     resolve in a canonical order derived from a hash of `(world_seed,
     cell_id)`, never from which direction a player approached — otherwise
     two players exploring the same seed from opposite directions could see
     different biome layouts at the same coordinates, breaking the
     `(world_seed, chunk_coord, pack_version)`-determinism every other stage
     in this pipeline relies on. Resolving a cell recursively resolves any
     not-yet-resolved neighbors that precede it in that order first
     (memoized per cell — cheap; only a handful of neighbors ever need it).
   - **Adjacency weights are soft multipliers, never hard exclusions** — a
     "very unlikely" pairing is a small weight (e.g. `0.02`), never a hard
     `0`. This guarantees at least one biome is always pickable, so cell
     resolution can never hit a contradiction and never needs backtracking
     or a restart — unlike textbook WFC, which allows genuine dead ends,
     this variant is deliberately built to always succeed in one pass. That
     trade (no global constraint solving, no backtracking) is what makes it
     safe for an infinite, lazily-generated world instead of a bounded,
     solved-all-at-once grid.
   - Biomes carry surface/filler/stone block refs and a decoration list, as
     before.
   - Optional, purely cosmetic: a short terrain/surface-block blend near
     remaining cell boundaries, since adjacency weighting controls *which*
     biomes can neighbor each other, not how abruptly the terrain itself
     transitions at the seam.
3. **Surface pass** — replace top solid voxels with biome surface/filler
   (grass over dirt, sand near water level, etc.).
4. **Carvers** — caves/ravines via 3D noise threshold (optional in base pack).
5. **Vein / scatter pass** — underground ore and other valuable-block
   placement. Distinct from decoration below (that's chunk-surface
   population like trees; this is subsurface scatter). Each biome (or the
   pack globally) registers entries shaped like `{block, target_rock,
   height_range, vein_size, spawn_rate}`; the engine deterministically
   scatters them per chunk (same `(seed, coord)`-seeded RNG as decoration)
   — Lua supplies the table, engine does the placement.
6. **Decoration / population pass** — runs once *neighbors are generated* so
   trees/structures may cross chunk borders. Deterministic per-chunk RNG seeded
   from `(seed, coord)`. Trees etc. are defined in Lua as schematics or
   procedural callbacks. **Further out (post this pipeline landing, see
   `REMAINING_TASKS.md`'s Deferred section):** a dedicated external structure
   tool exporting into the schematic format, plus declarative placement rules
   (neighbor-block constraints, clustering tendency, biome/density weighting)
   evaluated by this pass, instead of every structure needing a hand-written
   procedural callback.
7. **Lighting** — initial sky/block light flood fill.

Generated chunks are inserted into the world with `revision = 1` and flagged for
replication to any player whose interest sphere covers them.

---
