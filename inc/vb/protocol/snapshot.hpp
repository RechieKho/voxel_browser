#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/protocol/handshake.hpp" // Decoded<>
#include "vb/protocol/message.hpp"
#include "vb/protocol/world.hpp" // EntityVisualOverride

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
	// Entity-management follow-up (spec architecture_spec/rendering.md
	// §11.3's "Per-instance override"): a script entity's `vb.world.spawn`
	// `visual_override` option, learned once and cached client-side
	// (ClientSession's own entity_visual_overrides_ map) rather than resent
	// every tick. Only ever populated on an `entered` record -- ServerSession
	// only attaches it there (see net::ServerSession::to_record); `updated`/
	// `local` records always leave this nullopt, which means "unchanged", not
	// "cleared" (there is no clear path yet -- the override is fixed for the
	// entity's whole replicated lifetime, same as `kind`). Absent entirely
	// (the common case) costs one bool on the wire.
	std::optional<EntityVisualOverride> visual_override;

	bool operator==(const EntityRecord &) const = default;
};

struct S2CEntitySnapshot {
	static constexpr MessageType kType = MessageType::kS2CEntitySnapshot;

	std::uint32_t server_tick = 0;
	std::uint32_t last_acked_input_seq = 0; // highest InputCmd seq simulated
	std::vector<EntityRecord> entered;
	std::vector<EntityRecord> updated;
	std::vector<core::NetId> removed;

	// The recipient's own authoritative state (interest culling excludes self,
	// so it is carried separately for client-side reconciliation, spec §8.4).
	bool has_local = false;
	EntityRecord local{};

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CEntitySnapshot> decode(std::span<const std::byte> in);
};

} // namespace vb::protocol
