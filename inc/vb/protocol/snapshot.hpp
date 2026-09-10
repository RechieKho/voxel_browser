#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/protocol/handshake.hpp" // Decoded<>
#include "vb/protocol/message.hpp"

// S2C_EntitySnapshot (spec §8.4) — lane 2, unreliable, one per server tick.
// Carries newly-visible entities in full (`entered`), position/rotation deltas
// for still-visible ones (`updated`), and ids that left the interest set
// (`removed`). See docs/replication.md.

namespace vb::protocol {

struct EntityRecord {
	core::NetId net_id = core::NetId::kInvalid;
	core::EntityKindId kind = core::EntityKindId::kInvalid;
	core::Vec3d pos{};
	core::Vec2f rot{}; // yaw, pitch (degrees)
	core::Vec3f vel{};
	std::uint8_t flags = 0;

	bool operator==(const EntityRecord &) const = default;
};

struct S2CEntitySnapshot {
	static constexpr MessageType kType = MessageType::kS2CEntitySnapshot;

	std::uint32_t server_tick = 0;
	std::uint32_t last_acked_input_seq = 0; // local player; 0 until Phase 3
	std::vector<EntityRecord> entered;
	std::vector<EntityRecord> updated;
	std::vector<core::NetId> removed;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CEntitySnapshot> decode(std::span<const std::byte> in);
};

} // namespace vb::protocol
