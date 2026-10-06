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
6. **Decoration / population pass** — structures (trees, bushes, boulders,
   ruins) placed by declarative rules, crossing chunk borders without ordering
   dependencies between worker jobs ("pull" stamping; `src/worldgen/
   structure_placement.cpp`, design in `docs/structure-editor.md` §E).
   - **Data.** A structure is a data file (`vb.register_structure`,
     `structures/*.lua`, written by the structure editor): size, anchor, a
     palette of block *names*, one or more weighted variants, and default
     placement fields (`on`, `replace`, `rotate`, `mirror`, `min_spacing`,
     `cluster`, `max_slope`, `y_min`/`y_max`). A biome's `decoration` list
     references structures by name with a `spawn_rate` and optional field
     overrides. The resolved form (`worldgen::StructureDef` /
     `PlacementRule`) is plain data with no Lua, safe on worker threads.
   - **Anchors belong to columns, not chunks.** Each rule has a global jittered
     grid keyed by `(world seed, grid cell, rule)`: cell side `2 * min_spacing`,
     anchor jittered over `[0, min_spacing]` inside it (so anchors are at
     least `min_spacing` apart on some axis), kept with a probability derived
     from `spawn_rate` (expected placements per 32×32 column), optionally
     multiplied by low-frequency noise when `cluster > 0` (the mean is
     preserved). The weighted variant, rotation (0–3 quarter turns about +y)
     and mirror come from the same per-anchor RNG.
   - **Validity reads only pre-decoration terrain.** The biome *at the anchor*
     must be the rule's biome; the ground block (`WorldGenerator::
     block_at_pregen`: surface, filler, stone, carvers, water — never a
     neighbor chunk's voxels) must be in `on` (any solid block if empty); the
     cell above it must be air and the ground at or above sea level; the
     ground height must lie in `y_min..y_max`; and the heights under the
     rotated footprint (sampled) must differ by at most `max_slope`.
   - **Each chunk pulls.** A chunk enumerates the placements within the
     largest structure radius of its footprint and stamps only its own cells,
     in the canonical order `(anchor x, z, biome, rule)`, so overlaps resolve
     identically from every chunk. `replace` is `"air"`, `"air_and_plants"`
     (air or a block registered `replaceable`) or `"all"`; an explicit
     `base:air` cell always carves. Every vertical chunk of a column sees the
     same placements.
   - **Procedural variety without callbacks.** The structure editor's seeded
     generators (tree, bush, boulder, fallen log) fill a volume at authoring
     time and "Bake ×N" stores N variants; the engine only picks one. A true
     runtime callback that reacts to its surroundings stays deferred
     (`REMAINING_TASKS.md`, Deferred section).
   - The inline `{blocks = {{x=,y=,z=,block=}}}` form still works as an
     anonymous one-variant structure (`replace = "all"`, no rotation).
7. **Lighting** — initial sky/block light flood fill.

Generated chunks are inserted into the world with `revision = 1` and flagged for
replication to any player whose interest sphere covers them.

---
