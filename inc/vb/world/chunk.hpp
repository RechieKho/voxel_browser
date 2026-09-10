#pragma once

#include <array>
#include <cstdint>

#include "vb/core/ids.hpp"
#include "vb/world/paletted_chunk_store.hpp"

// One 32^3 chunk: block store + light volume + bookkeeping (spec §5.3).

namespace vb::world {

enum class GenState : std::uint8_t {
	kUngenerated = 0,
	kGenerating,
	kGenerated, // terrain + initial light done
	kPopulated, // decoration pass done (needs neighbours)
};

struct DirtyFlags {
	bool terrain = false;
	bool light = false;
	bool mesh = false;

	void mark_edited() {
		terrain = true;
		light = true;
		mesh = true;
	}
	bool any() const { return terrain || light || mesh; }
};

// Light volume: one byte per voxel, 4 bits sky + 4 bits block light (0..15).
struct Light {
	std::uint8_t packed = 0;

	std::uint8_t sky() const { return static_cast<std::uint8_t>(packed >> 4); }
	std::uint8_t block() const { return static_cast<std::uint8_t>(packed & 0x0F); }
	void set_sky(std::uint8_t v) {
		packed = static_cast<std::uint8_t>((packed & 0x0F) | ((v & 0x0F) << 4));
	}
	void set_block(std::uint8_t v) {
		packed = static_cast<std::uint8_t>((packed & 0xF0) | (v & 0x0F));
	}
	std::uint8_t max() const {
		const std::uint8_t s = sky();
		const std::uint8_t b = block();
		return s > b ? s : b;
	}
};

class Chunk {
public:
	Chunk() = default;
	explicit Chunk(core::ChunkCoord coord) : coord_(coord) {}

	core::ChunkCoord coord() const { return coord_; }
	void set_coord(core::ChunkCoord c) { coord_ = c; }

	PalettedChunkStore &blocks() { return blocks_; }
	const PalettedChunkStore &blocks() const { return blocks_; }

	core::BlockId get(int x, int y, int z) const { return blocks_.get(x, y, z); }
	bool set(int x, int y, int z, core::BlockId b) {
		const bool changed = blocks_.set(x, y, z, b);
		if (changed) {
			dirty_.mark_edited();
			++revision_;
		}
		return changed;
	}

	Light &light(int x, int y, int z) { return light_[index_of(x, y, z)]; }
	const Light &light(int x, int y, int z) const {
		return light_[index_of(x, y, z)];
	}
	std::array<Light, kChunkVolume> &light_volume() { return light_; }
	const std::array<Light, kChunkVolume> &light_volume() const {
		return light_;
	}

	GenState gen_state() const { return gen_state_; }
	void set_gen_state(GenState s) { gen_state_ = s; }

	DirtyFlags &dirty() { return dirty_; }
	const DirtyFlags &dirty() const { return dirty_; }

	std::uint64_t revision() const { return revision_; }
	void bump_revision() { ++revision_; }
	void set_revision(std::uint64_t r) { revision_ = r; }

private:
	core::ChunkCoord coord_{};
	PalettedChunkStore blocks_{ core::BlockId::kAir };
	std::array<Light, kChunkVolume> light_{};
	GenState gen_state_ = GenState::kUngenerated;
	DirtyFlags dirty_{};
	std::uint64_t revision_ = 0;
};

} // namespace vb::world
