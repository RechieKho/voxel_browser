#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/protocol/handshake.hpp" // Decoded<>
#include "vb/protocol/message.hpp"

// World replication messages (spec §8.5), lane 1. The chunk payload is an
// opaque blob produced by vb::world::encode_chunk_payload — the protocol layer
// stays independent of the voxel data model.

namespace vb::protocol {

struct S2CChunkAdd {
	static constexpr MessageType kType = MessageType::kS2CChunkAdd;

	core::ChunkCoord coord{};
	std::uint64_t revision = 0;
	std::vector<std::byte> payload; // world::encode_chunk_payload output

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CChunkAdd> decode(std::span<const std::byte> in);
};

struct BlockChange {
	std::uint32_t local_index = 0; // 0 .. CHUNK_VOLUME-1
	core::BlockId block = core::BlockId::kAir;

	bool operator==(const BlockChange &) const = default;
};

struct LightChange {
	std::uint32_t local_index = 0;
	std::uint8_t packed = 0;

	bool operator==(const LightChange &) const = default;
};

struct S2CChunkDelta {
	static constexpr MessageType kType = MessageType::kS2CChunkDelta;

	core::ChunkCoord coord{};
	std::uint64_t base_revision = 0;
	std::uint64_t new_revision = 0;
	std::vector<BlockChange> blocks;
	std::vector<LightChange> light;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CChunkDelta> decode(std::span<const std::byte> in);
};

struct S2CChunkRemove {
	static constexpr MessageType kType = MessageType::kS2CChunkRemove;

	core::ChunkCoord coord{};

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CChunkRemove> decode(std::span<const std::byte> in);
};

// --- block editing (spec §8.5 / §5.2) -----------------------------------

enum class BlockEditAction : std::uint8_t {
	kBreak = 0, // remove the block at `pos`
	kPlace = 1, // fill the (empty) cell at `pos` with `block`
};
constexpr bool valid(BlockEditAction a) {
	return a == BlockEditAction::kBreak || a == BlockEditAction::kPlace;
}

struct C2SBlockEdit {
	static constexpr MessageType kType = MessageType::kC2SBlockEdit;

	std::uint32_t predicted_seq = 0; // client's optimistic-apply id
	BlockEditAction action = BlockEditAction::kBreak;
	core::IVec3 pos{}; // world voxel coordinate
	core::BlockId block = core::BlockId::kAir; // kPlace only

	void encode(std::vector<std::byte> &out) const;
	static Decoded<C2SBlockEdit> decode(std::span<const std::byte> in);
};

struct S2CBlockEditResult {
	static constexpr MessageType kType = MessageType::kS2CBlockEditResult;

	std::uint32_t predicted_seq = 0;
	bool accepted = false; // false -> client rolls its optimistic apply back
	core::IVec3 pos{};

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CBlockEditResult> decode(std::span<const std::byte> in);
};

} // namespace vb::protocol
