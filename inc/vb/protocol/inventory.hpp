#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/protocol/handshake.hpp" // Decoded<>
#include "vb/protocol/message.hpp"

// Inventory sync (spec §5.1's "real inventory" gap): a pack's
// player:get_inventory()/player:give() (Phase 4.2) were server-Lua-only state
// with no wire message keeping a client's view in sync. This gives the client
// a live copy to render a hotbar / feed ui.define("base:inventory") from,
// instead of only the one-shot snapshot a script can hand open_ui explicitly.

namespace vb::protocol {

struct InventorySlot {
	core::BlockId item = core::BlockId::kAir;
	std::uint16_t count = 0;
};

// Sent to one player whenever their inventory changes (currently: after
// player:give()). Always a full snapshot, not a delta -- inventories are
// small and this mirrors S2CPlayerList's "just resend the whole list" posture
// rather than adding delta-tracking machinery for a handful of slots.
struct S2CInventory {
	static constexpr MessageType kType = MessageType::kS2CInventory;

	std::vector<InventorySlot> slots;

	void encode(std::vector<std::byte> &out) const;
	static Decoded<S2CInventory> decode(std::span<const std::byte> in);
};

} // namespace vb::protocol
