# Structure editor — design and phased plan

> Status: **Implemented: S0–S7 done** (the optional in-game export in S7 was left out). Plans the "dedicated external structure
> tool" that `architecture_spec/worldgen.md` §6 stage 6 and
> `remaining_tasks/deferred.md` ("Rule-based decorative structure
> placement") left for after the Lua worldgen pipeline. Phase 6.14 shipped
> that pipeline. Same layout as `docs/content-base-ui.md`: findings first,
> then the design, then phases (S0–S7) with checkbox tasks.

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
- `vb.register_block`'s table parsing (`src/script/pack_runtime.cpp`) is
  the one place that turns a Lua block table into a `world::BlockType`. Once
  it's factored out, the editor can use the same code on a plain data table
  without running any pack code.
- `WorldGenerator` and `PackWorldGenPipeline` are plain C++. A pipeline can
  be built directly in C++, as `tests/unit/worldgen_test.cpp`'s
  `make_test_pipeline` does, so the tool can **generate terrain locally** to
  preview placement, with no server involved.
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
 └───▲───────────────┬──────────────────────────────────▲─────────────────┘
     │ reads         │ reads/writes                     │ reads
 data/blocks.lua   structures/*.lua                   textures/*.png
 (returns a table) (each returns a table)
     │               │
     │  require      │ require                 pack code (init.lua or any file)
     └───────────────┴──────────────────────►  vb.register_block(def)
                                               vb.register_structure(def)
                                                 │ build_worldgen_pipeline
                                                 ▼
                         worldgen::StructureDef + PlacementRule (pure data)
                                                 │
                         WorldGenerator decoration pass (worker threads)
```

The editor only reads **data scripts**: Lua files that `return` a table and
make no `vb.*` calls (`require` of other pack files is allowed). The pack decides where and how to register that data with its
own code. Both sides use the same C++ parsing functions for block and
structure tables, so the editor can't accept something the server rejects.

### B. Why a standalone executable rather than an in-game creative mode

- **Offline and instant.** No server, handshake, or replication, and no world
  to keep clean. Point it at a block data script, edit, save.
- **Easy to iterate on worldgen.** The tool runs `WorldGenerator` itself, so
  re-rolling a seed or tweaking a rule redraws a terrain patch in under a
  second. That would be awkward through a live server.
- **This is the earlier decision.** The spec and deferred notes both say
  "dedicated external tool".
- **Cost:** some camera and input code gets duplicated. It's limited because
  `vb_render` is already a library.

Rejected for now: an in-game `/structure` capture command. It could be
added later on top of the same writer, for example "select a region in
singleplayer and export". It's listed as optional in S7.

### C. File format: `structures/*.lua`

A structure is a data script: a Lua file that returns one table. The pack
registers it with a new `vb.register_structure(def)`, the same pattern as
blocks in G. The pack loader's directory walk doesn't touch `structures/`,
so these files only run when something `require`s them. Lua is chosen over a
binary or JSON file because:

- `require` already resolves pack `.lua` files
- diffs read well in git, and small fixes can be made by hand
- the format is the same kind of data script as the block data in G

```lua
-- structures/oak_tree.lua. Generated by vb_structure_editor. Hand edits are
-- kept as long as the layout below stays valid.
return {
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
		-- more baked variants (S5); one is chosen per placement
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
}
```

Rules:
- Palette keys are single characters. Values are a block **name**, never an
  id, because ids depend on registration order. `false` means "keep". Names
  are resolved at `build_worldgen_pipeline` time, after the whole pack has
  loaded, so blocks may be registered in any file, `init.lua` included. An
  unknown name is a pack load error naming the structure.
- **Registering structures.** The editor also keeps
  `structures/all.lua` up to date on every save. It is a data script that
  `require`s and returns every structure in the folder, so the pack needs
  just one line, wherever it likes:
  `for _, s in ipairs(require("structures.all")) do vb.register_structure(s) end`.
  A pack can ignore `all.lua` and `require` individual structures instead.
  `all.lua` is a reserved name: the editor never lists it as a structure.
- The size is capped at 64×64×64 (a constant, `kMaxStructureDim`). That keeps
  the cross-chunk search radius in E bounded.
- Biomes refer to structures by name:
  `decoration = { { structure = "base:oak_tree", spawn_rate = 0.6, on = {...}? }, ... }`.
  Any `placement` field can be overridden per biome entry. The existing
  inline `{blocks = {...}}` form keeps working and becomes an anonymous
  one-variant structure with `replace = "all"` and no rotation. Its block
  shape is unchanged, but where it lands changes with the new anchor
  scheme in E (per column instead of per 3D chunk). No pack in this repo
  uses the inline form, so nothing in-repo moves.

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
`DecorationEntry` is removed after its uses are migrated in S1.

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

### G. Block data script: what the editor reads

The editor doesn't load the content pack or run any of its code. Its input is
a **block data script**: a Lua file that returns a list of block tables, the
same tables `vb.register_block` already takes. The pack registers those
blocks from its own code, in `init.lua` or anywhere else. Nothing about this
is enforced: a pack can register some blocks directly as it does today, and
the editor just won't show those blocks.

```lua
-- data/blocks.lua: pure data, no vb.* calls
return {
	{ name = "base:stone", solid = true, opaque = true, texture = "textures/stone.png" },
	{ name = "base:wood",  solid = true, opaque = true },
	{ name = "base:grass", solid = true, opaque = true, drops = "base:dirt" },
	{ name = "base:leaves", solid = true, opaque = false, replaceable = true },
	-- ...
}
```

```lua
-- blocks/register.lua: any file works, but see "Where to register" below
BLOCK_IDS = {}
for _, def in ipairs(require("data.blocks")) do
	-- The handler looks up the id when a block breaks, after every block is
	-- registered, so `drops` may name a block later in the list.
	def.on_break = def.on_break or function(ctx)
		vb.world.spawn_item_drop(
			{ x = ctx.pos.x + 0.5, y = ctx.pos.y + 0.5, z = ctx.pos.z + 0.5 },
			BLOCK_IDS[def.drops or def.name], 1)
	end
	BLOCK_IDS[def.name] = vb.register_block(def)
end
```

(`drops` is a field this pack's code reads, not an engine field. The engine
ignores keys it doesn't know.)

- **Behavior stays in pack code.** The data script describes what a block
  is: name, solidity, light, texture, `max_damage`, `max_stack`, and so on.
  The pack attaches handlers when it registers. A data table may contain
  functions too (an `on_break` in the entry); the editor ignores fields it
  doesn't use and never calls them.
- **Where to register.** For worldgen, any file works: biome and structure
  block names are only resolved when the pipeline is built, after the whole
  pack has loaded (`build_worldgen_pipeline`). Other pack code can still
  need block ids **at load time**. `content/base/crafting.lua`, a root
  module, reads `base_wood_id`, `base_planks_id`, and `base_sticks_id` when
  it loads, and root modules run before `init.lua`. So `content/base`
  registers from `blocks/register.lua`, which the loader runs first. A pack
  that registers from `init.lua` has to move any load-time id users after
  that point.
- **Keep the registration order.** Block ids are assigned in registration
  order, and saved worlds (region files) store ids. `data/blocks.lua` for
  `content/base` lists blocks in today's order (the sorted `blocks/*.lua`
  file order), so existing worlds load unchanged. The built-in
  `BlockRegistry::base()` blocks keep their fixed ids whatever the order.
- **How the editor evaluates the script.** It uses a bare Lua state: no
  config, no storage, no network, and only a stub `vb` table whose fields
  all raise the error below. `require` is limited to the
  pack's own `.lua` files, so a data script can pull in shared constants.
  Calling `vb.*` from a data script fails with a clear message: "block data
  scripts must only return data; register blocks from pack code". The
  editor starts from `BlockRegistry::base()` (the engine's built-in blocks),
  as the server does, then adds each entry through the shared
  `parse_block_type`.
- **Shared parsing.** `vb.register_block`'s table-to-`BlockType` code is
  factored into `vb::script::parse_block_type(sol::table)`, and structure
  tables get the same treatment with `parse_structure`. The engine's
  `vb.register_block` and the editor call the same functions.
- **Where paths come from.** The editor finds the pack root by walking up
  from the data script to the nearest `pack.toml`, or uses the script's
  folder if there is none. Texture paths and the `structures/` folder are
  resolved against that root. The `base:` name prefix for new structures
  defaults to the `name` in `pack.toml`.
- **The convention is recommended, not mandatory.** `content/base` moves to
  `data/blocks.lua` plus `blocks/register.lua` (S0), so its blocks are
  editable. Its existing `base_*_id` globals stay defined for
  `crafting.lua` and the other files that use them. Other packs can adopt it whenever they want editor support.

### H. Editor inputs and outputs

`vb_structure_editor <block-data-script>`, for example
`vb_structure_editor content/base/data/blocks.lua`. That is all it needs.

- **Reads:**
  - the block data script (G), which gives the palette: names, textures,
    and properties;
  - `textures/*.png` from the pack root, for icons and the viewport, through
    `TextureAtlas`. Blocks with no texture get the atlas's flat placeholder
    color, the same as in the client;
  - `structures/*.lua` from the pack root (except `all.lua`), each
    evaluated as a data script in the same bare Lua state. These populate
    the Open dialog.
- **Writes:** only `structures/<local-name>.lua` and `structures/all.lua`,
  on save. Nothing else in the pack is touched, and no pack code runs, so
  there are no side effects to guard against (such as `init.lua` writing
  `vb.storage`).
- **Reload** (`F5`) re-evaluates the block data script and textures, so
  blocks edited elsewhere show up without restarting. The open structure
  stays in memory and its block names are resolved again. Names that no
  longer exist turn into a visible "missing block" marker instead of being
  dropped. The same marker appears for blocks the pack registers outside
  the data script.
- **Errors are shown, not fatal.** A Lua error in the block data script or a
  structure file is listed with its file name in an errors panel. If the
  block data script can't load at all, the editor stays on its open screen.

### I. Editor UX

- **Viewport:** orbit, pan, and zoom camera around the structure bounds.
  Ground-grid plane at the anchor. Bounding box drawn as a wireframe, anchor
  cell marked. "Keep" cells are invisible and explicit-air cells show as a
  faint ghost cube. A layer-slice slider hides everything above y.
- **Palette panel:** every block from the block data script, with the
  atlas icon and name, plus a search box. The selected block is the brush.
  "Keep" and "Air" are pseudo-blocks.
- **Tools:** place or remove (raycast to face or cell), paint (replace in
  place), box fill, line, flood replace, eyedropper, select/move/copy/paste,
  and symmetry (mirror X/Z live, plus 4-way rotational). Unlimited undo and
  redo (`Ctrl+Z` / `Ctrl+Y`).
- **Structure panel:** name, size (resize keeps content, with an anchor-side
  choice), anchor picker, variant list (add, duplicate, delete, weight), and
  generator panel (parameters, seed, "Generate", "Bake ×N").
- **Placement panel:** fields for the `placement` defaults.
- **Terrain preview mode:** generates a patch of N×N columns (default 8×8,
  256×256 blocks) of **test terrain** and runs this structure's rule on it
  with the engine's real placement code, then switches to fly-camera view.
  The test terrain is configured in the panel: height noise (flat to hilly),
  sea level, and surface, filler, and stone blocks picked from the palette.
  It is a one-biome `PackWorldGenPipeline` built in C++, so no pack worldgen
  code is needed. Re-roll seed. Toggle "only this structure" or "all
  structures in the folder". It reuses `WorldGenerator` and `ChunkRenderer`
  as they are.
- **Files:** open one of the structures in `structures/` or create a new
  one, save, and save-as (see H). A dirty marker in the title
  and a confirm prompt before closing unsaved work.

### J. Code layout

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

Each phase can ship on its own. S0–S2 are engine and content work, and S1–S2 give
packs real trees even before any UI exists. S3–S6 build the tool. S7
integrates and documents.

### S0 — Block data script (engine + content/base)

- [x] Factor `vb.register_block`'s table parsing into
      `vb::script::parse_block_type(sol::table)`
      (`inc/vb/script/block_def.hpp`). `vb.register_block` calls it, so its
      behavior doesn't change.
- [x] `vb::script::eval_data_script(path, pack_root)`: evaluates a Lua file
      in a bare state (a stub `vb` whose every field raises "data scripts
      must only return data", `require` limited to the pack's `.lua` files)
      and returns its table or an error naming the file. It needs no
      `PackRuntime`, transport, or storage. The editor uses it for both
      block data and structure files.
- [x] Optional `replaceable` block field in `BlockType`, read by
      `parse_block_type` (used by `replace = "air_and_plants"` in E).
- [x] `content/base`: move the block tables from `blocks/*.lua` into
      `data/blocks.lua` (same order as today's sorted file walk) and
      register them from `blocks/register.lua`. Keep today's `on_break`
      drops, including grass dropping dirt (`drops` field), and keep the
      `base_*_id` globals that `crafting.lua` and others read. Mark
      `base:leaves` `replaceable`. `kitchen_sink` stays as it is, to show the direct style still works.
- [x] Docs: `docs/lua-api.md` and `architecture_spec/content-pack-format.md`
      describe data scripts as a recommended convention, with the example
      in G.
- [x] Tests: `parse_block_type` gives identical `BlockType`s through
      `vb.register_block` and through `eval_data_script`; `content/base`'s
      block registry (ids, names, textures, drops) is unchanged by the move,
      checked against the existing `content_base_*` suites, and a world
      saved before the move loads with the same blocks; a data script that
      calls `vb.*` fails with the expected message.

### S1 — Structure format and loading (engine)

- [x] `inc/vb/worldgen/structure.hpp`: `StructureDef`, `StructureVariant`,
      `PlacementRule`, `kMaxStructureDim`, and the keep sentinel.
- [x] `vb::script::parse_structure(sol::table)`, shared by the engine and
      the editor: check that size, layers, row lengths, and palette keys are
      valid, and report errors that name the structure.
- [x] `vb.register_structure(def)` in `PackRuntime`, built on
      `parse_structure`. Structure files are data scripts that the pack
      `require`s. The loader walk doesn't change.
- [x] `build_worldgen_pipeline`: resolve palette names to ids, build
      `structures`, and parse biome `decoration` entries of the form
      `{structure=..., ...overrides}`. Migrate the inline `{blocks=...}` form
      to an anonymous one-variant structure (no rotation, `replace="all"`;
      the shape is kept, positions follow S2's anchor scheme).
- [x] Tests (`pack_runtime_test.cpp`): parse round trip; the same
      structure file gives an equal `StructureDef` through
      `vb.register_structure(require(...))` and through `eval_data_script`;
      malformed files (bad row length, unknown key, unknown block, size over
      the cap) give clear errors; the inline form converts to the expected
      anonymous structure.

### S2 — Rule-based, cross-chunk placement (engine)

- [x] `structure_placement.cpp`: per-column anchor enumeration (jittered
      grid, spawn-rate thinning, cluster noise) and per-anchor biome lookup.
- [x] Anchor validity from pure terrain functions: `on`, `max_slope`, y
      range, and not underwater. Factor out the carver/surface check
      `generate()` already does into a reusable `block_at_pregen(x, y, z)`.
- [x] Pull stamping across neighbor columns in the canonical order. Rotation
      and mirror applied to variant cells. `replace` policy, using the
      `replaceable` flag from S0.
- [x] Replace the decoration block in `WorldGenerator::generate` with the
      above.
- [x] Tests (`worldgen_test.cpp`): a structure that straddles a chunk border
      is identical whichever chunk is generated first, and when chunks are
      generated on different threads; all vertical chunks agree; same seed
      gives the same result; rotation and mirror are correct on an
      asymmetric fixture; `on` and slope rejection; new decoration golden
      hash. The existing pack-driven golden must stay unchanged because its
      decoration list is empty.
- [x] `perf_budget_test`: a decoration-heavy chunk stays within budget.
- [x] Content: a hand-written `structures/acacia_tree.lua` in
      `content/examples/kitchen_sink`, registered from its `worldgen.lua`
      with `vb.register_structure(require("structures.acacia_tree"))` and
      used by `biomes/savanna.lua`.

### S3 — Editor shell (viewing only)

- [x] `VB_BUILD_EDITOR` option, `vb_editor_model` library,
      `vb_structure_editor` executable, CI build in the existing matrix.
- [x] `Volume` model with a conversion to and from a private
      `ClientChunkStore`, so `ChunkRenderer` meshes it unchanged.
- [x] `vb_structure_editor <block-data-script>`: load blocks with
      `eval_data_script` and `parse_block_type` (S0), find the pack root,
      build the `TextureAtlas`, show an errors panel, and support Reload
      (`F5`), as described in H. Read the `pack.toml` `name` for the
      default name prefix.
- [x] Test: the editor's palette for `content/base/data/blocks.lua` matches
      the block registry the server builds from the full pack.
- [x] Open a structure from `structures/*.lua`, skipping the reserved
      `all.lua` (optionally preselected with `--open <name>`). Orbit camera,
      ground grid, bounds box, anchor marker, variant switcher.
- [x] Block names a structure uses that aren't in the block data script
      show as a "missing block" marker and are kept on save, never dropped.
      Test: load, then save, a structure with an unknown name keeps it.
- [x] `StructureWriter` producing canonical Lua with stable key order, so a
      save of an unchanged file is byte-identical, plus regeneration of
      `structures/all.lua`. Test: write, then read back through
      `eval_data_script` and `parse_structure`, gives an equal
      `StructureDef` for every fixture.

### S4 — Interactive editing

- [x] `Command` interface plus `UndoStack` in the model. Commands for
      setting cells, box fill, line, flood replace, paste, resize, and
      re-anchor. Unit tests for apply, undo, and redo of each.
- [x] Viewport raycast to a cell or face, with hover highlight. Place,
      remove, paint, and eyedropper on mouse buttons and modifiers.
- [x] Palette panel: icons from the atlas, search, and the Keep/Air
      pseudo-blocks.
- [x] Box, line, and flood tools. Selection with copy, paste, and move.
      Live mirror symmetry on X and Z.
- [x] Layer-slice slider, keep/air ghost rendering, and a dirty flag with a
      save prompt on exit.
- [x] New structure dialog (name with the pack-name prefix prefilled, size,
      anchor). Save and save-as into `<pack>/structures/`, which also
      regenerates `structures/all.lua`.

### S5 — Generators and variants

- [x] `Generator` interface (parameters, then fill the volume) in the model,
      seeded by `DetRng`, so the same seed always gives the same shape.
- [x] Generators: **tree** (trunk height range, trunk block, canopy shape
      (sphere, cone, or layered blob), canopy radius, leaf density, branch
      count), **bush**, **boulder** (noise-deformed ellipsoid with block
      mix), and **fallen log**.
- [x] Generator panel: parameter widgets driven by each generator's
      parameter descriptor list. "Generate" replaces the current variant;
      "Bake ×N" appends N variants with seeds derived from one base seed.
- [x] Variant list editing (weights, duplicate, delete, reorder).
- [x] Tests: generator output is deterministic per seed (hash golden) and
      stays inside the volume bounds.

### S6 — Placement authoring and live terrain preview

- [x] Placement panel bound to the structure's `placement` defaults.
- [x] Test-terrain panel: height noise, sea level, and surface, filler, and
      stone blocks from the palette, built into a one-biome
      `PackWorldGenPipeline` in C++.
- [x] Preview mode: run the in-memory structure and rule (not saved yet)
      on that pipeline, generate an N×N column patch on a
      `WorldGenWorkerPool`, mesh it, and use a fly camera.
- [x] Re-roll seed, toggle "only this structure" or "all structures in the
      folder", re-generate on any rule change (debounced), and show a
      placements counter.
- [x] Warnings shown in the panel, such as "no valid anchor in the preview
      patch" or "`on` block is not the test terrain's surface block".

### S7 — Integration, docs, base content

- [x] `vb structure` CLI subcommands (`architecture_spec/dev-cli.md`):
      `new`, `edit [block-data-script]` (launches the editor; the script
      defaults to `data/blocks.lua` in the current pack), and `validate`
      (headless: evaluate the block data and every structure file with
      `eval_data_script`, then report parse errors, block names missing
      from the block data, and structure files left out of `all.lua`).
      `new` writes an empty structure file and updates `all.lua`.
- [x] Docs: `docs/lua-api.md` (`vb.register_structure`, the new
      decoration entry form, `replaceable`),
      `architecture_spec/worldgen.md` stage 6 rewritten to describe pull
      stamping, a new "Structure editor" section in `README.md`, and
      `remaining_tasks/deferred.md` updated: this item marked done and the
      runtime-callback item narrowed as in F.
- [x] Base content: oak and birch trees, a bush, and a boulder made in the
      editor, registered with one `structures.all` loop in
      `blocks/register.lua` (or a new root module), and used by
      `base:forest` and `base:plains`. **This depends on the
      open question below** about moving `content/base` onto
      `set_pipeline`.
- [ ] Optional (not done): an in-game export command (singleplayer, automation build
      only) that captures a selected region with the same `StructureWriter`.

## Open questions

1. **Should `content/base` move onto `vb.worldgen.set_pipeline`?** Base
   trees need it, but it changes the default world's terrain (a new golden
   and new spawn behavior). **Resolved: yes**, in its own commit before the
   trees (`content/base/worldgen.lua`): same height range, sea level and soil
   depth, different noise salting, and an optional `beach = "base:sand"`
   pipeline field added so beaches survive the move.
2. **Size cap.** 64³ is plenty for trees and rocks. Ruins or villages would
   need bigger pieces or a jigsaw/assembly system. Treat that as a separate
   feature and don't raise the cap.
3. **Structure-to-structure avoidance** ("no tree within 3 of a boulder").
   `min_spacing` only applies within one rule. Cross-rule exclusion needs
   rules to know about each other's anchors. It's possible with the column
   enumeration, but leave it out until there's real content that needs it.
4. **Preview on real pack terrain.** Test terrain covers rule tuning. Seeing
   a structure in the pack's actual biomes would need the pack's worldgen
   settings as data too. That could be a second, optional data script
   (`data/worldgen.lua`, read the same way as block data), added only if
   test terrain turns out not to be enough.
5. **Hot reload into a running singleplayer world.** Useful, but server-side
   hot reload is deferred as a whole. Preview mode (S6) covers the need for
   now.

## What was built, and where it differs from the plan

Everything in S0–S7 above is implemented, with these notes:

- **Where things live.** The headless model is `vb_editor_model`
  (`src/editor/model/`, `inc/vb/editor/`): `Volume`, `StructureDoc` (cells are
  indices into a name table, with 0 = keep), `StructureWriter`, `BlockCatalog`,
  `Workspace`, `EditSession` (every tool, undo/redo, variants, placement),
  `generators`, `TerrainPreview` and `validate_pack`. `vb_structure_editor`
  (`src/editor/app/`) is drawing and input. The structure *format* types and
  placement live in the engine (`inc/vb/worldgen/structure.hpp`,
  `src/worldgen/structure*.cpp`), shared with the editor.
- **Unknown block names.** `PackRuntime::validate_worldgen()` (called after
  `freeze()` by the server and `--singleplayer`) reports unknown block and
  structure names and an unknown pipeline `beach`; `build_worldgen_pipeline`
  logs the same and builds without decoration instead of crashing.
- **`min_spacing`** is implemented as a jittered grid with cell side
  `2 * min_spacing`, which guarantees anchors are at least `min_spacing` apart
  on some axis (see `architecture_spec/worldgen.md` §6).
- **Spawn rate in the editor** is a preview-only setting; real density lives in
  the biome entry that uses the structure.
- **Rotational symmetry** from the editing design (§I) was not built; mirror X
  and Z (the S4 task list) were.
- **Dev hooks.** `vb_structure_editor --check` prints a headless summary (a
  ctest runs it on `content/base`), and `--script "cmd;cmd"` with
  `--screenshot <png>` drives the editor for screenshot tests; the structures
  shipped in `content/base` were made that way (seeded generators, `Bake ×N`,
  `save`). See the comment above `EditorApp::run_script`.
- **Packaging.** `vb_structure_editor` is added to release archives when it was
  built, `vb which editor` finds it, and `vb structure edit` launches it.
