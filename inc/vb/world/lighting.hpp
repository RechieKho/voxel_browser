#pragma once

#include <array>
#include <cstdint>
#include <utility>

#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"

// Voxel lighting (spec §5.4). Flood-fill sky + block light. relight_chunk()
// computes one chunk at a time; sky light additionally needs to know whether
// there are loaded chunks around it -- vertically (`Neighbours::above`) and,
// as of this pass, horizontally too (`Neighbours::north/south/east/west`) --
// relight_column() below drives both across a whole loaded area. Cross-chunk
// block light and incremental relight-on-edit for block light remain a
// Phase 3/5 refinement.

namespace vb::world {

inline constexpr std::uint8_t kMaxLight = 15;

class LightEngine {
public:
	// Holds the registry by value (it is cheap and callers pass temporaries).
	explicit LightEngine(BlockRegistry registry) : registry_(std::move(registry)) {}

	// Loaded chunks adjacent to the one being relit, each seeding sky light
	// across the corresponding border instead of `relight_chunk` assuming
	// that border is closed off. `above` (the pre-existing, vertical case)
	// must be the chunk at `{coord.x, coord.y + 1, coord.z}`; `north`/
	// `south`/`east`/`west` (new) must be the chunks at `{coord.x, coord.y,
	// coord.z + 1}` / `{coord.x, coord.y, coord.z - 1}` / `{coord.x + 1,
	// coord.y, coord.z}` / `{coord.x - 1, coord.y, coord.z}` respectively.
	// Any field left null means "nothing loaded there" (or "closed off"),
	// same posture `above == nullptr` already had -- never "assume open".
	// The single-pointer constructor keeps every existing `above`-only call
	// site (including relight_column()'s own, pre this pass) compiling
	// unchanged.
	struct Neighbours {
		const Chunk *above = nullptr;
		const Chunk *north = nullptr; // {coord.z + 1}
		const Chunk *south = nullptr; // {coord.z - 1}
		const Chunk *east = nullptr; // {coord.x + 1}
		const Chunk *west = nullptr; // {coord.x - 1}

		Neighbours() {}
		Neighbours(const Chunk *above_chunk) : above(above_chunk) {}
	};

	// Recompute both light channels for a whole chunk from scratch. Clears the
	// light dirty flag. See `Neighbours` above for what each field seeds.
	void relight_chunk(Chunk &chunk, const Neighbours &neighbours = {}) const;

private:
	// How much light a block passes through (kMaxLight for air/transparent,
	// reduced for liquids, 0 for opaque).
	std::uint8_t transmittance(core::BlockId block) const;

	BlockRegistry registry_;
};

// Relight the chunk at `coord` (found via `find`) with its real neighbours --
// above, and (as of this pass) whichever of north/south/east/west are
// loaded -- then cascade the same relight downward through every
// consecutively loaded chunk below it, unconditionally, down to the bottom of
// the loaded stack. This is what fixes the "false-bright chunk boundary" bug:
// relight_chunk() alone, called per chunk in isolation, always assumes every
// border is closed off regardless of what's actually loaded there.
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
// below it, plus (see below) any horizontal neighbour this call ends up
// pushing into. A chunk's revision is bumped whenever its light output
// actually changed (chunk_lifecycle.cpp relies on this to know a chunk needs
// re-sending to already-connected players; client-side callers can rely on
// it the same way ChunkRenderer already relies on revision changes to know
// when to re-mesh).
//
// Horizontal push: if relighting a chunk in this column with a neighbour's
// input actually changes that chunk's own light, and a horizontal neighbour
// in that direction is loaded, this also relights that neighbour's whole
// column (recursively, via the same cascade) so an *already-stable* sideways
// neighbour picks up the change immediately -- the case that matters for a
// live edit near a chunk border (e.g. breaking a block that opens a gap
// under an overhang whose far side is a chunk that finished lighting long
// ago and has no other reason to ever relight again). This only ever goes
// one hop out from the originally-requested column, never further: every
// propagation step costs at least 1 of light's 0-15 range, and a chunk is
// kChunkDim (32) blocks wide, so light that has just crossed one border has
// at most 14 of budget left -- nowhere near enough to cross a second
// full-width chunk and matter to a third one. A pushed neighbour's own
// relight can therefore never usefully push back out any further, so it
// doesn't try. (Diagonal neighbours are still never touched directly, same
// as before this pass -- an edit's effect on a diagonal chunk, if any, only
// ever arrives indirectly through whichever of the two shared orthogonal
// neighbours it pushes into, one hop at a time, exactly like everything else
// here.)
template <typename ChunkLookup, typename OnRelit>
void relight_column_impl(const LightEngine &engine, core::ChunkCoord coord,
		ChunkLookup &&find, OnRelit &&on_relit, bool push);

template <typename ChunkLookup, typename OnRelit>
void relight_column(const LightEngine &engine, core::ChunkCoord coord,
		ChunkLookup &&find, OnRelit &&on_relit) {
	relight_column_impl(engine, coord, find, on_relit, /*push=*/true);
}

template <typename ChunkLookup, typename OnRelit>
void relight_column_impl(const LightEngine &engine, core::ChunkCoord coord,
		ChunkLookup &&find, OnRelit &&on_relit, bool push) {
	Chunk *chunk = find(coord);
	if (chunk == nullptr) {
		return;
	}
	bool changed_east = false;
	bool changed_west = false;
	bool changed_north = false;
	bool changed_south = false;

	Chunk *above = find(core::ChunkCoord{ coord.x, coord.y + 1, coord.z });
	core::ChunkCoord cur = coord;
	for (;;) {
		typename LightEngine::Neighbours n;
		n.above = above;
		n.east = find(core::ChunkCoord{ cur.x + 1, cur.y, cur.z });
		n.west = find(core::ChunkCoord{ cur.x - 1, cur.y, cur.z });
		n.north = find(core::ChunkCoord{ cur.x, cur.y, cur.z + 1 });
		n.south = find(core::ChunkCoord{ cur.x, cur.y, cur.z - 1 });

		const std::array<Light, kChunkVolume> before = chunk->light_volume();
		engine.relight_chunk(*chunk, n);
		const bool changed = chunk->light_volume() != before;
		if (changed) {
			chunk->bump_revision();
			changed_east = changed_east || n.east != nullptr;
			changed_west = changed_west || n.west != nullptr;
			changed_north = changed_north || n.north != nullptr;
			changed_south = changed_south || n.south != nullptr;
		}
		on_relit(cur, before, *chunk);

		const core::ChunkCoord below_coord{ cur.x, cur.y - 1, cur.z };
		Chunk *below = find(below_coord);
		if (below == nullptr) {
			break;
		}
		above = chunk;
		chunk = below;
		cur = below_coord;
	}

	// Deferred until the whole column above is fully relit, not fired
	// per-level as each change is found: a pushed neighbour's own relight
	// reads *this* column's chunks back via `find`, and would see a
	// half-updated column (levels below the one that just changed still
	// holding pre-relight data) if pushed too early.
	if (!push) {
		return;
	}
	if (changed_east) {
		relight_column_impl(engine, core::ChunkCoord{ coord.x + 1, coord.y, coord.z }, find, on_relit, false);
	}
	if (changed_west) {
		relight_column_impl(engine, core::ChunkCoord{ coord.x - 1, coord.y, coord.z }, find, on_relit, false);
	}
	if (changed_north) {
		relight_column_impl(engine, core::ChunkCoord{ coord.x, coord.y, coord.z + 1 }, find, on_relit, false);
	}
	if (changed_south) {
		relight_column_impl(engine, core::ChunkCoord{ coord.x, coord.y, coord.z - 1 }, find, on_relit, false);
	}
}

} // namespace vb::world
