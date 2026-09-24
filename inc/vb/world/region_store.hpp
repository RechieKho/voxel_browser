#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/world/chunk.hpp"

// On-disk world persistence (ARCHITECTURE_SPEC.md §18's "world persistence /
// region file format", deferred at first-playable, picked up 2026-09-25).
// Groups chunks into flat "region" files on the X/Z plane only (Minecraft's
// scheme) -- Y isn't grouped, since this engine's chunk coordinates are fully
// 3D but a generated world is typically only a few chunks tall; every chunk
// column-neighbour on X/Z shares one file regardless of Y. Reuses chunk_codec's
// existing palette+RLE payload format as-is -- a region file is just a
// container for it, not a new wire concept. No LZ4 framing yet: RLE is the
// only compression here for now (ARCHITECTURE_SPEC §18's still-open "chunk
// compression" item covers adding that uniformly, not scoped to this class).
//
// Only *edited* chunks are ever persisted: Chunk::revision() == 0 means "still
// exactly what worldgen produced", so regenerating on next load is equivalent
// and cheaper than reading+writing it. save_if_dirty() also skips a chunk
// whose in-memory revision already matches what's cached/on-disk, so a chunk
// that's loaded but never edited again is never re-encoded.
//
// Writes are batched: save_if_dirty() only updates an in-memory per-region
// cache; flush() is what actually rewrites a region file, once per dirty
// region regardless of how many of its chunks changed. Not thread-safe --
// call only from the tick thread, same as everything else ChunkLifecycleSystem
// touches.

namespace vb::world {

class RegionStore {
public:
	explicit RegionStore(std::filesystem::path world_dir);

	// Returns the persisted chunk at `coord`, or nullptr if it was never saved
	// (or its region file failed to decode -- corruption is logged and treated
	// as "not found", never fatal: the caller just regenerates it).
	std::unique_ptr<Chunk> load(core::ChunkCoord coord);

	// Updates the in-memory region cache if `chunk` has edits not yet cached
	// (chunk.revision() > 0 and different from what's cached). Never touches
	// disk on its own -- call flush() to persist.
	void save_if_dirty(const Chunk &chunk);

	// Writes every region with unsaved changes to disk.
	void flush();

private:
	struct Entry {
		std::vector<std::byte> payload;
		std::uint64_t revision = 0;
	};
	struct Region {
		std::unordered_map<core::ChunkCoord, Entry> entries;
		bool dirty = false;
	};

	// Packs a region's (x, z) into one key -- region grouping never involves Y.
	static std::int64_t region_key(core::ChunkCoord coord);
	std::filesystem::path region_path(std::int64_t key) const;
	Region &region_for(core::ChunkCoord coord);

	std::filesystem::path world_dir_;
	std::unordered_map<std::int64_t, Region> regions_;
};

} // namespace vb::world
