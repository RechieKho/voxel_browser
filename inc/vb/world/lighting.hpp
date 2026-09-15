#pragma once

#include <array>
#include <cstdint>
#include <utility>

#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"

// Voxel lighting (spec §5.4). Flood-fill sky + block light. relight_chunk()
// computes one chunk at a time; sky light additionally needs to know whether
// there's a loaded chunk directly above it (relight_column() below handles
// that across a whole vertical stack). Cross-chunk sky occlusion is otherwise
// still per-column-only (no horizontal light travel) and incremental
// relight-on-edit for block light is a Phase 3/5 refinement.

namespace vb::world {

inline constexpr std::uint8_t kMaxLight = 15;

class LightEngine {
public:
	// Holds the registry by value (it is cheap and callers pass temporaries).
	explicit LightEngine(BlockRegistry registry) : registry_(std::move(registry)) {}

	// Recompute both light channels for a whole chunk from scratch. Clears the
	// light dirty flag.
	//
	// `above`, if non-null, must be the chunk directly above `chunk` in the
	// same (x, z) column (i.e. at `{chunk.coord().x, chunk.coord().y + 1,
	// chunk.coord().z}`). Its bottom (local y = 0) sky-light row seeds this
	// chunk's top layer instead of assuming open sky above it -- omitting it
	// (or passing nullptr because nothing is loaded there) means "open sky",
	// which is only actually correct for the topmost loaded chunk in a
	// column. Every other chunk in a stack needs its real neighbour passed in
	// -- see relight_column(), which does that plus cascades downward.
	void relight_chunk(Chunk &chunk, const Chunk *above = nullptr) const;

private:
	// How much light a block passes through (kMaxLight for air/transparent,
	// reduced for liquids, 0 for opaque).
	std::uint8_t transmittance(core::BlockId block) const;

	BlockRegistry registry_;
};

// Relight the chunk at `coord` (found via `find`) with its real neighbour
// above, then cascade the same relight downward through every consecutively
// loaded chunk below it, unconditionally, down to the bottom of the loaded
// stack. This is what fixes the "false-bright chunk boundary" bug:
// relight_chunk() alone, called per chunk in isolation, always assumes open
// sky at every chunk's own top layer regardless of what's actually above it.
//
// Deliberately unconditional rather than stopping early once a chunk's
// output "looks unchanged": a chunk that has never been lit before starts
// with an all-zero light volume by construction, which is indistinguishable
// from "correctly recomputed to all zero" (e.g. under a solid roof) using a
// before/after comparison alone -- an early-exit built on that comparison
// stops the cascade before it ever reaches chunks that still needed it. Loaded
// columns are shallow in practice (a handful of chunks per the server's
// vertical view distance), so the extra relight_chunk() calls this costs
// when nothing below actually needed updating are cheap.
//
// `find(ChunkCoord) -> Chunk*` looks up a loaded chunk (nullptr if none).
// `on_relit(ChunkCoord, const std::array<Light, kChunkVolume>& light_before,
// const Chunk& chunk_after)` fires once per chunk actually processed, in
// top-down order, for `coord` itself and every consecutively loaded chunk
// below it. A chunk's revision is bumped whenever its light output actually
// changed (chunk_lifecycle.cpp relies on this to know a chunk needs
// re-sending to already-connected players; client-side callers can rely on
// it the same way ChunkRenderer already relies on revision changes to know
// when to re-mesh).
//
// Does not touch horizontal/diagonal neighbours -- if a cascaded chunk's new
// light affects a sideways neighbour's border AO, that neighbour is not
// re-flagged here (a much rarer, smaller-magnitude case than the whole-layer
// bright band this fixes; left as a known follow-up, see STATE.md).
template <typename ChunkLookup, typename OnRelit>
void relight_column(const LightEngine &engine, core::ChunkCoord coord,
		ChunkLookup &&find, OnRelit &&on_relit) {
	Chunk *chunk = find(coord);
	if (chunk == nullptr) {
		return;
	}
	Chunk *above = find(core::ChunkCoord{ coord.x, coord.y + 1, coord.z });
	core::ChunkCoord cur = coord;
	for (;;) {
		const std::array<Light, kChunkVolume> before = chunk->light_volume();
		engine.relight_chunk(*chunk, above);
		if (chunk->light_volume() != before) {
			chunk->bump_revision();
		}
		on_relit(cur, before, *chunk);

		const core::ChunkCoord below_coord{ cur.x, cur.y - 1, cur.z };
		Chunk *below = find(below_coord);
		if (below == nullptr) {
			return;
		}
		above = chunk;
		chunk = below;
		cur = below_coord;
	}
}

} // namespace vb::world
