#pragma once

#include <cstdint>
#include <functional>

#include "vb/core/math.hpp"

// Strongly-typed identifiers (spec §5.1). Thin wrappers so a BlockId can't be
// passed where a NetId is expected.

namespace vb::core {

// Index into the per-session block registry sent by the server. 0 == air.
enum class BlockId : std::uint16_t { kAir = 0 };

// Compact network entity id assigned by the replication layer / librg.
enum class NetId : std::uint32_t { kInvalid = 0 };

// Lua-registered entity type id (drives client model selection).
enum class EntityKindId : std::uint16_t { kInvalid = 0 };

// Chunk position in chunk units (spec: CHUNK_DIM = 32).
struct ChunkCoord {
	std::int32_t x{};
	std::int32_t y{};
	std::int32_t z{};

	constexpr bool operator==(const ChunkCoord &) const = default;
};

inline constexpr std::int32_t kChunkDim = 32;

constexpr ChunkCoord chunk_of(IVec3 world_block) {
	return { floor_div(world_block.x, kChunkDim), floor_div(world_block.y, kChunkDim),
		floor_div(world_block.z, kChunkDim) };
}
constexpr IVec3 local_of(IVec3 world_block) {
	return { floor_mod(world_block.x, kChunkDim), floor_mod(world_block.y, kChunkDim),
		floor_mod(world_block.z, kChunkDim) };
}
constexpr IVec3 chunk_origin(ChunkCoord c) {
	return { c.x * kChunkDim, c.y * kChunkDim, c.z * kChunkDim };
}

// 128-bit content hash (xxHash3-128) of an asset's bytes; hex in manifests.
struct AssetHash {
	std::uint64_t lo{};
	std::uint64_t hi{};

	constexpr bool operator==(const AssetHash &) const = default;
};

} // namespace vb::core

template <>
struct std::hash<vb::core::ChunkCoord> {
	std::size_t operator()(const vb::core::ChunkCoord &c) const noexcept {
		// 21 bits per axis is plenty for any reachable world extent.
		const std::uint64_t h = (static_cast<std::uint64_t>(c.x & 0x1FFFFF)) |
				(static_cast<std::uint64_t>(c.y & 0x1FFFFF) << 21) |
				(static_cast<std::uint64_t>(c.z & 0x1FFFFF) << 42);
		return std::hash<std::uint64_t>{}(h);
	}
};

template <>
struct std::hash<vb::core::AssetHash> {
	std::size_t operator()(const vb::core::AssetHash &a) const noexcept {
		return std::hash<std::uint64_t>{}(a.lo) ^ (std::hash<std::uint64_t>{}(a.hi) << 1);
	}
};
