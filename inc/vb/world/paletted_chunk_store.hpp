#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vb/core/ids.hpp"

// Palette-compressed voxel storage for one chunk (spec §5.3).
//
// - A per-chunk palette maps small indices -> BlockId.
// - Voxel indices are bit-packed at 0/1/2/4/8/16 bits per voxel; the width grows
//   automatically as the palette does. All widths divide 64, so a voxel never
//   straddles two words.
// - A homogeneous chunk (one palette entry) stores zero index data.
//
// Indexing is linear: `index = x + CHUNK_DIM*(z + CHUNK_DIM*y)` (see index_of).

namespace vb::world {

inline constexpr int kChunkDim = core::kChunkDim; // 32
inline constexpr std::size_t kChunkVolume =
		static_cast<std::size_t>(kChunkDim) * kChunkDim * kChunkDim;

constexpr std::size_t index_of(int x, int y, int z) {
	return static_cast<std::size_t>(x) +
			static_cast<std::size_t>(kChunkDim) *
			(static_cast<std::size_t>(z) +
					static_cast<std::size_t>(kChunkDim) *
							static_cast<std::size_t>(y));
}

class PalettedChunkStore {
public:
	// Starts homogeneous, all `fill`.
	explicit PalettedChunkStore(core::BlockId fill = core::BlockId::kAir);

	core::BlockId get(std::size_t index) const;
	core::BlockId get(int x, int y, int z) const {
		return get(index_of(x, y, z));
	}

	// Returns true if the voxel changed.
	bool set(std::size_t index, core::BlockId block);
	bool set(int x, int y, int z, core::BlockId block) {
		return set(index_of(x, y, z), block);
	}

	// Reset the whole chunk to one block, dropping all index storage.
	void fill(core::BlockId block);

	bool is_homogeneous() const { return bits_per_index_ == 0; }
	std::uint8_t bits_per_index() const { return bits_per_index_; }
	const std::vector<core::BlockId> &palette() const { return palette_; }

	// Bytes of packed index storage (0 when homogeneous).
	std::size_t packed_bytes() const { return data_.size() * sizeof(std::uint64_t); }

	// Drop palette entries no longer referenced and shrink the index width.
	void compact();

	// True when every voxel resolves to the same BlockId (may still be
	// non-homogeneous in storage until compact()).
	bool uniform_value(core::BlockId *out = nullptr) const;

private:
	std::size_t palette_index_of(core::BlockId block); // adds if absent
	std::uint64_t read_raw(std::size_t index) const;
	void write_raw(std::size_t index, std::uint64_t value);
	void set_width(std::uint8_t new_bits); // repacks

	static std::uint8_t width_for(std::size_t palette_size);

	std::vector<core::BlockId> palette_;
	std::vector<std::uint64_t> data_;
	std::uint8_t bits_per_index_ = 0;
};

} // namespace vb::world
