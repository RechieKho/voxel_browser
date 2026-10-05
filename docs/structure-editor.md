# Structure editor — design and phased plan

> Status: **Planned, not started.** Plans the "dedicated external structure
> tool" that `architecture_spec/worldgen.md` §6 stage 6 and
> `remaining_tasks/deferred.md` ("Rule-based decorative structure
> placement") left for after the Lua worldgen pipeline. Phase 6.14 shipped
> that pipeline. Same layout as `docs/content-base-ui.md`: findings first,
> then the design, then phases (S0–S6) with checkbox tasks.

The goal is an interactive voxel tool for building biome decorations (trees,
bushes, boulders, ruins, and so on), saving them into a content pack, and
seeing them placed on real generated terrain. The engine also needs the
placement side so these structures actually show up in worlds.

## What was decided earlier

These points come from `architecture_spec/worldgen.md` §6 and
`remaining_tasks/deferred.md`. This plan follows them:

1. Structures are **authored in a dedicated external tool** and imported into
   the content pack **as schematics**.
2. They are placed by **declarative rules**, not hand-written per-structure
   callbacks. Examples given: neighbor-block constraints ("must be on dirt"),
   clustering tendency, and biome/density weighting.
3. Placement must stay deterministic from `(world_seed, chunk_coord,
   pack_version)`, the same as every other worldgen stage.
4. Two gaps from Phase 6.14 should be fixed in the same effort:
   - **Cross-chunk decoration.** Today, offsets that land outside the
     originating chunk are dropped.
   - **Procedural decoration.** A per-site Lua callback can't run on a
     `WorldGenWorkerPool` thread.

## Findings (current code)

### 1. Decoration today is an inline offset list

- `vb.register_biome{decoration = {{spawn_rate=, blocks={{x=,y=,z=,block=},
  ...}}, ...}}` is parsed in `PackRuntime::build_worldgen_pipeline`
  (`src/script/pack_runtime.cpp`, around the "Decoration:" comment). The
  result is `worldgen::DecorationEntry` (`inc/vb/worldgen/pipeline.hpp`),
  which holds a `std::vector<BlockOffset>` and a `spawn_rate`.
- A plain string (`content/base/biomes/forest.lua`'s `decoration = "trees"`)
  is captured and ignored. `content/base` never calls
  `vb.worldgen.set_pipeline`, so the base world has no decoration at all.
  Only `content/examples/kitchen_sink` uses the pipeline.
- There is no structure name, no reuse across biomes, no file of its own, no
  rotation, and no placement constraint. A tree gets written by hand as
  dozens of `{x=,y=,z=,block=}` lines.

### 2. The decoration pass (`WorldGenerator::generate`, `src/worldgen/generator.cpp`)

- The biome is resolved **once per chunk, at the chunk center**, and not per
  anchor. A chunk that straddles a biome border uses one biome's list for
  the whole chunk.
- The RNG is seeded from `hash3(seed, cx, cy, cz)`, so it is **per 3D
  chunk**. Every vertical chunk in a column rolls its own placements, and
  each anchor sits at `surface_height + 1`. Only the chunk that contains
  that height keeps any blocks. Effective density therefore depends on
  terrain height in a non-obvious way.
- Any block outside `[0, kChunkDim)` (32) is skipped, so trees get cut off at
  chunk borders.
- Blocks overwrite whatever is there. There is no "only into air" policy and
  no check of the block under the anchor (it can grow out of water, sand, or
  a carved cave mouth).
- Good news for compatibility: the pack-driven golden test
  (`tests/unit/worldgen_test.cpp`, `make_test_pipeline`) uses **empty**
  decoration, so changing decoration semantics doesn't move that golden.

### 3. Reusable pieces for a standalone tool

- `vb_render` already has everything needed to draw voxels:
  `ChunkRenderer` (background meshing plus budgeted upload),
  `TextureAtlas` (block textures from the pack, with a flat-color fallback),
  `camera.hpp`, `input.hpp`, `ui_renderer.hpp`, and raygui
  (`src/render/raygui_impl.c`).
- `vb::world::ClientChunkStore` together with `mesh_chunk` gives face-culled,
  AO-lit meshes from any voxel data placed in a store. A structure can be a
  few chunks in a private store.
- `vb::world::raycast` handles block picking.
- `vb::script::load_content_pack` and `PackRuntime` give the pack's real
  `BlockRegistry` (names, textures) and its biome/pipeline definitions.
  Because `build_worldgen_pipeline` and `WorldGenerator` are plain C++, the
  tool can **generate real terrain locally** to preview placement, with no
  server involved.
- CMake already has per-executable switches (`VB_BUILD_CLIENT/SERVER/CLI`),
  so an editor target fits the same pattern.

## Design

### A. Shape of the solution

```
 ┌───────────────────── vb_structure_editor (new exe) ─────────────────────┐
 │ raylib/raygui app: orbit camera, palette, tools, generators, preview    │
 │        │ uses                                                           │
 │ vb_editor_model (new lib, headless, unit-tested)                        │
 │   Volume · Selection · Command/undo stack · Generators · Writer         │
 └────────┬───────────────────────────────────────────────┬────────────────┘
          │ writes structures/*.lua                       │ loads pack via
          ▼                                               ▼ load_content_pack
   content/<pack>/structures/oak.lua  ──► PackRuntime (vb.register_structure)
                                            │ build_worldgen_pipeline
                                            ▼
                         worldgen::StructureDef + PlacementRule (pure data)
                                            │
                         WorldGenerator decoration pass (worker threads)
```

The tool and the engine share one format and one parser: the pack's own Lua
loader. The tool never has a separate reader that could drift from what the
server accepts.

### B. Why a standalone executable rather than an in-game creative mode

- **Offline and instant.** No server, handshake, or replication, and no world
  to keep clean. Open the pack, edit, save.
- **Easy to iterate on worldgen.** The tool runs `WorldGenerator` itself, so
  re-rolling a seed or tweaking a rule redraws a terrain patch in under a
  second. That would be awkward through a live server.
- **This is the earlier decision.** The spec and deferred notes both say
  "dedicated external tool".
- **Cost:** some camera and input code gets duplicated. It's limited because
  `vb_render` is already a library.

Rejected for now: an in-game `/structure` capture command. It could be
added later on top of the same writer, for example "select a region in
singleplayer and export". It's listed as optional in S6.

### C. File format: `structures/*.lua`

A structure is a Lua file that calls a new `vb.register_structure`. It is
loaded in the pack's existing fixed walk, after `entities/` and **before
`biomes/`**, so biomes can refer to structures by name. Lua is chosen over a
binary or JSON file because:

- the pack loader, asset sync, and `require` already handle `.lua`
- diffs read well in git, and small fixes can be made by hand
- the editor's reader is just `PackRuntime`, as described in A

```lua
-- Generated by vb_structure_editor. Hand edits are kept as long as the
-- layout below stays valid.
vb.register_structure({
	name = "base:oak_tree",
	size = { x = 5, y = 7, z = 5 },
	anchor = { x = 2, y = 0, z = 2 },   -- the cell that sits on the ground block
	palette = {
		["."] = false,                 -- keep: leave existing terrain untouched
		["_"] = "base:air",            -- explicit air: carves
		["W"] = "base:wood",
		["L"] = "base:leaves",
	},
	-- One string per row (z), one table per layer (y), bottom layer first.
	-- Each row is size.x characters long.
	variants = {
		{ weight = 1, layers = {
			{ ".....", ".....", "..W..", ".....", "....." },
			-- ...
		} },
		-- more baked variants (S4); one is chosen per placement
	},
	placement = {                       -- defaults; a biome entry can override them
		on = { "base:grass", "base:dirt" }, -- block directly under the anchor
		replace = "air",                -- "air" | "air_and_plants" | "all"
		rotate = true,                  -- random 0/90/180/270 degrees around y
		mirror = false,
		min_spacing = 4,                -- blocks between anchors (jittered grid)
		cluster = 0.0,                  -- 0 = even; toward 1 = clumped (noise-gated)
		max_slope = 2,                  -- largest ground height difference under the footprint
		y_min = 0, y_max = 255,
	},
})
```

Rules:
- Palette keys are single characters. Values are a block **name**, never an
  id, because ids depend on registration order. `false` means "keep". Names
  are resolved at `build_worldgen_pipeline` time, and an unknown name is a
  pack load error naming the file.
- The size is capped at 64×64×64 (a constant, `kMaxStructureDim`). That keeps
  the cross-chunk search radius in E bounded.
- Biomes refer to structures by name:
  `decoration = { { structure = "base:oak_tree", spawn_rate = 0.6, on = {...}? }, ... }`.
  Any `placement` field can be overridden per biome entry. The existing
  inline `{blocks = {...}}` form keeps working and becomes an anonymous
  one-variant structure with `replace = "all"` and no rotation, so its output
  matches today's.

### D. Engine data model (pure C++, no sol2, safe on worker threads)

```cpp
// inc/vb/worldgen/structure.hpp
struct StructureVariant {
	core::IVec3 size;
	std::vector<core::BlockId> cells; // size.x*size.y*size.z, kKeep = sentinel
	double weight = 1.0;
};
struct StructureDef {
	std::string name;
	core::IVec3 anchor;
	std::vector<StructureVariant> variants;
	int radius_xz; // precomputed max horizontal reach from anchor, any rotation
};
struct PlacementRule {
	std::uint32_t structure; // index into PackWorldGenPipeline::structures
	double spawn_rate;       // expected placements per 32x32 column
	std::vector<core::BlockId> on;
	enum class Replace { kAir, kAirAndPlants, kAll } replace;
	bool rotate, mirror;
	int min_spacing, max_slope, y_min, y_max;
	double cluster;
};
```

`PackWorldGenPipeline` gains `std::vector<StructureDef> structures`, and
each biome's decoration list becomes `std::vector<PlacementRule>`.
`DecorationEntry` is removed after its uses are migrated in S0.

### E. Cross-chunk placement without waiting on neighbors ("pull" stamping)

The spec's wording ("runs once neighbors are generated") would add an
ordering dependency between worker jobs. The plan avoids that dependency,
keeping workers independent and output deterministic:

1. **Anchors belong to columns, not chunks.** For column `(cx, cz)`, the
   candidate anchors for rule `r` come from
   `DetRng{hash3(seed, cx, cz, salt_r)}` only. They're laid out on a
   jittered grid with cell size `min_spacing`, then kept with probability
   derived from `spawn_rate`. With `cluster > 0`, that probability is
   multiplied by a low-frequency noise value. Each anchor's biome is
   resolved **at the anchor itself**.
2. **Anchor validity uses only pre-decoration terrain, computed as pure
   functions:** `surface_height`, the biome's surface and filler blocks,
   carver density, and the sea level. That covers `on`, `max_slope` (from
   heights under the rotated footprint), and the y range. None of these
   reads a neighbor chunk's voxels, so every chunk that asks gets the same
   answer.
3. **Each chunk pulls.** When chunk `(cx, cy, cz)` reaches the decoration
   pass, it enumerates anchors for every column within
   `ceil(max radius_xz / 32)` (one ring for structures under 32 blocks wide)
   and stamps only the cells inside its own bounds. Vertical chunks in a
   column all see the same anchors, so trees no longer depend on which
   vertical chunk the surface falls in.
4. **Fixed overlap order.** Stamps are applied in a canonical order:
   `(anchor column x, z, rule index, anchor index)`. When two structures
   overlap, the later stamp wins under the `replace` policy, and the result
   is the same in every chunk.
5. Cost: about 9 column enumerations per chunk, each with cheap
   hash-and-height checks, so roughly O(anchors) per chunk. `perf_budget_test`
   gets a decoration-heavy case to keep this bounded.

`Replace::kAirAndPlants` needs to know which blocks count as plants. It
reuses a `replaceable` flag on block registration (new and optional, default
false; leaves set it).

### F. "Procedural" without callbacks: generators in the tool

The callback gap from 6.14 is handled by moving the randomness to authoring
time. The editor has **parametric generators** (tree, bush, boulder,
fallen log, and more later), written in C++ and seeded. They fill the volume
and can **bake N variants** into one structure. At world generation, the
engine only picks a weighted variant, plus a rotation and mirror. Ten baked
oak variants times four rotations times two mirrors gives 80 distinct shapes,
which is enough variety for decoration, and it stays pure data with no Lua on
worker threads. A true runtime callback, using a per-worker `sol::state` or a
main-thread apply pass, stays deferred. Its entry in `deferred.md` is
narrowed to cases bakes can't cover, such as structures that react to their
surroundings.

### G. Editor UX

- **Viewport:** orbit, pan, and zoom camera around the structure bounds.
  Ground-grid plane at the anchor. Bounding box drawn as a wireframe, anchor
  cell marked. "Keep" cells are invisible and explicit-air cells show as a
  faint ghost cube. A layer-slice slider hides everything above y.
- **Palette panel:** every registered block from the loaded pack, with the
  atlas icon and name, plus a search box. The selected block is the brush.
  "Keep" and "Air" are pseudo-blocks.
- **Tools:** place or remove (raycast to face or cell), paint (replace in
  place), box fill, line, flood replace, eyedropper, select/move/copy/paste,
  and symmetry (mirror X/Z live, plus 4-way rotational). Unlimited undo and
  redo (`Ctrl+Z` / `Ctrl+Y`).
- **Structure panel:** name, size (resize keeps content, with an anchor-side
  choice), anchor picker, variant list (add, duplicate, delete, weight), and
  generator panel (parameters, seed, "Generate", "Bake ×N").
- **Placement panel:** fields for the `placement` defaults and a biome
  dropdown to preview with.
- **Terrain preview mode:** generates a patch of N×N columns (default 8×8,
  256×256 blocks) with the pack's real pipeline and this structure's rule,
  and switches to fly-camera view. Re-roll seed. Toggle "only this
  structure" or "all decorations". It reuses `WorldGenerator` and
  `ChunkRenderer` as they are.
- **Files:** open a pack (`--pack content/base`), open or create
  `structures/<name>.lua`, save, and save-as. A dirty marker in the title
  and a confirm prompt before closing unsaved work.

### H. Code layout

```
inc/vb/worldgen/structure.hpp          StructureDef, PlacementRule (engine)
src/worldgen/structure_placement.cpp   anchor enumeration + pull stamping
inc/vb/editor/*.hpp, src/editor/model/ vb_editor_model (headless lib)
    volume.{hpp,cpp}        dense cells + keep sentinel, resize, rotate
    commands.{hpp,cpp}      Command interface, UndoStack, SetCells/Fill/Paste
    generators.{hpp,cpp}    tree / bush / boulder (deterministic DetRng)
    structure_writer.{hpp,cpp}  StructureDef -> canonical Lua text
src/editor/app/                       vb_structure_editor executable
    main.cpp, editor_app.{hpp,cpp}, viewport, panels, preview
```

The CMake option is `VB_BUILD_EDITOR` (default ON when `VB_BUILD_CLIENT` and
`VB_WITH_LUA` are both on). It's off for `VB_HEADLESS`. The model library
builds whenever tests do, so CI covers it everywhere.

## Phases

Each phase can ship on its own. S0–S1 are engine-only and give the base pack
real trees even before any UI exists. S2–S5 build the tool. S6 integrates
and documents.

### S0 — Structure format and loading (engine)

- [ ] `inc/vb/worldgen/structure.hpp`: `StructureDef`, `StructureVariant`,
      `PlacementRule`, `kMaxStructureDim`, and the keep sentinel.
- [ ] `vb.register_structure{...}` in `PackRuntime`: capture it, check that
      size, layers, row lengths, and palette keys are valid, and report
      errors that name the file and the structure.
- [ ] Pack loader walk adds `structures/*.lua` (sorted) between
      `entities/` and `biomes/`. Update `pack_loader.hpp`'s header comment
      and `architecture_spec/content-pack-format.md`.
- [ ] `build_worldgen_pipeline`: resolve palette names to ids, build
      `structures`, and parse biome `decoration` entries of the form
      `{structure=..., ...overrides}`. Migrate the inline `{blocks=...}` form
      to an anonymous structure (same output as today: no rotation,
      `replace="all"`).
- [ ] Tests (`pack_runtime_test.cpp`): parse round trip; malformed files
      (bad row length, unknown key, unknown block, size over the cap) give
      clear errors; inline-form compatibility.

### S1 — Rule-based, cross-chunk placement (engine)

- [ ] `structure_placement.cpp`: per-column anchor enumeration (jittered
      grid, spawn-rate thinning, cluster noise) and per-anchor biome lookup.
- [ ] Anchor validity from pure terrain functions: `on`, `max_slope`, y
      range, and not underwater. Factor out the carver/surface check
      `generate()` already does into a reusable `block_at_pregen(x, y, z)`.
- [ ] Pull stamping across neighbor columns in the canonical order. Rotation
      and mirror applied to variant cells. `replace` policy. Optional
      `replaceable` block flag.
- [ ] Replace the decoration block in `WorldGenerator::generate` with the
      above.
- [ ] Tests (`worldgen_test.cpp`): a structure that straddles a chunk border
      is identical whichever chunk is generated first, and when chunks are
      generated on different threads; all vertical chunks agree; same seed
      gives the same result; rotation and mirror are correct on an
      asymmetric fixture; `on` and slope rejection; new decoration golden
      hash. The existing pack-driven golden must stay unchanged because its
      decoration list is empty.
- [ ] `perf_budget_test`: a decoration-heavy chunk stays within budget.
- [ ] Content: a hand-written `structures/oak_tree.lua` in
      `content/examples/kitchen_sink`, used by its forest-like biome.

### S2 — Editor shell (viewing only)

- [ ] `VB_BUILD_EDITOR` option, `vb_editor_model` library,
      `vb_structure_editor` executable, CI build in the existing matrix.
- [ ] `Volume` model with a conversion to and from a private
      `ClientChunkStore`, so `ChunkRenderer` meshes it unchanged.
- [ ] Load a pack with `--pack <dir>` through `load_content_pack`. Build the
      `TextureAtlas` from the pack's registry.
- [ ] Open a structure (`--open structures/x.lua`, or a list of structures
      from the loaded pack). Orbit camera, ground grid, bounds box, anchor
      marker, variant switcher.
- [ ] `StructureWriter` producing canonical Lua with stable key order, so a
      save of an unchanged file is byte-identical. Test: write, then load
      through `PackRuntime`, gives an equal `StructureDef` for every
      fixture.

### S3 — Interactive editing

- [ ] `Command` interface plus `UndoStack` in the model. Commands for
      setting cells, box fill, line, flood replace, paste, resize, and
      re-anchor. Unit tests for apply, undo, and redo of each.
- [ ] Viewport raycast to a cell or face, with hover highlight. Place,
      remove, paint, and eyedropper on mouse buttons and modifiers.
- [ ] Palette panel: icons from the atlas, search, and the Keep/Air
      pseudo-blocks.
- [ ] Box, line, and flood tools. Selection with copy, paste, and move.
      Live mirror symmetry on X and Z.
- [ ] Layer-slice slider, keep/air ghost rendering, and a dirty flag with a
      save prompt on exit.
- [ ] New structure dialog (name, size, anchor). Save and save-as into
      `<pack>/structures/`.

### S4 — Generators and variants

- [ ] `Generator` interface (parameters, then fill the volume) in the model,
      seeded by `DetRng`, so the same seed always gives the same shape.
- [ ] Generators: **tree** (trunk height range, trunk block, canopy shape
      (sphere, cone, or layered blob), canopy radius, leaf density, branch
      count), **bush**, **boulder** (noise-deformed ellipsoid with block
      mix), and **fallen log**.
- [ ] Generator panel: parameter widgets driven by each generator's
      parameter descriptor list. "Generate" replaces the current variant;
      "Bake ×N" appends N variants with seeds derived from one base seed.
- [ ] Variant list editing (weights, duplicate, delete, reorder).
- [ ] Tests: generator output is deterministic per seed (hash golden) and
      stays inside the volume bounds.

### S5 — Placement authoring and live terrain preview

- [ ] Placement panel bound to the structure's `placement` defaults, plus a
      biome dropdown taken from the pack's `vb.register_biome` entries.
- [ ] Preview mode: build the pack's pipeline with this in-memory structure
      and rule swapped in (not saved yet), generate an N×N column patch on
      a `WorldGenWorkerPool`, mesh it, and use a fly camera.
- [ ] Re-roll seed, toggle "only this structure" or "all decorations",
      re-generate on any rule change (debounced), and show a placements
      counter.
- [ ] Warnings shown in the panel, such as "no valid anchor in the preview
      patch" or "`on` block never appears in this biome's surface".
- [ ] A pack without `set_pipeline` (today's `content/base`) can't be
      previewed. Show a clear message, and fall back to a flat grass test
      patch so rules can still be tried.

### S6 — Integration, docs, base content

- [ ] `vb structure` CLI subcommands (`architecture_spec/dev-cli.md`):
      `new`, `edit` (launches the editor on the current pack), and
      `validate` (headless load and report, for CI on packs).
- [ ] Docs: `docs/lua-api.md` (`vb.register_structure`, the new
      decoration entry form, `replaceable`),
      `architecture_spec/worldgen.md` stage 6 rewritten to describe pull
      stamping, a new "Structure editor" section in `README.md`, and
      `remaining_tasks/deferred.md` updated: this item marked done and the
      runtime-callback item narrowed as in F.
- [ ] Base content: oak and birch trees, a bush, and a boulder made in the
      editor, used by `base:forest` and `base:plains`. **This depends on the
      open question below** about moving `content/base` onto
      `set_pipeline`.
- [ ] Optional: an in-game export command (singleplayer, automation build
      only) that captures a selected region with the same `StructureWriter`.

## Open questions

1. **Should `content/base` move onto `vb.worldgen.set_pipeline`?** Base
   trees need it, but it changes the default world's terrain (a new golden
   and new spawn behavior). The recommendation is yes, as its own task
   before the base-content part of S6, so the terrain change and the tree
   change can be reviewed separately.
2. **Size cap.** 64³ is plenty for trees and rocks. Ruins or villages would
   need bigger pieces or a jigsaw/assembly system. Treat that as a separate
   feature and don't raise the cap.
3. **Structure-to-structure avoidance** ("no tree within 3 of a boulder").
   `min_spacing` only applies within one rule. Cross-rule exclusion needs
   rules to know about each other's anchors. It's possible with the column
   enumeration, but leave it out until there's real content that needs it.
4. **Hot reload into a running singleplayer world.** Useful, but server-side
   hot reload is deferred as a whole. Preview mode (S5) covers the need for
   now.
