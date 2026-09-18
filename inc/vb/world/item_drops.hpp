#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"

// Dropped-item entities (spec §5.1 / §11.3, REMAINING_TASKS.md 5.1's
// "Dropped-item entity" gap). No generic EnTT registry exists yet (Phase 3.1
// is deferred until Lua entity kinds need it), so this is a small, real,
// hardcoded system -- same posture as the Phase 2 worldgen pipeline or the
// Phase 3.5 placeholder billboards: a working feature shipped ahead of the
// general one it will eventually fold into. A drop is replicated to nearby
// clients through the exact same interest-grid / S2C_EntitySnapshot path a
// player already uses (`vb::replication::InterestGrid`, keyed by NetId) --
// ServerSession upserts/removes drops there, so no new wire message is
// needed and the client's existing EntityRenderer (3.5) draws one as a
// billboard for free.
//
// Deliberately pure: no net/script/session dependency, so it's unit-testable
// standalone. ServerSession owns one, drives tick() once per server tick, and
// applies the returned pickups/removals to its own interest grid + a script
// host's inventory (see PackRuntime::attach_session).

namespace vb::world {

// EntityKindId for a dropped-item entity in S2C_EntitySnapshot records. A
// reserved sentinel at the top of the id space so it can't collide with a
// pack's own `vb.register_entity` ids (those count up from 1); nothing
// currently branches on kind (the client draws every remote entity the same
// way regardless, per Phase 3.5), so this is informational for now.
inline constexpr core::EntityKindId kItemDropKind =
		static_cast<core::EntityKindId>(0xFFFFu);

struct ItemDrop {
	core::NetId id = core::NetId::kInvalid;
	core::BlockId item = core::BlockId::kAir;
	std::uint16_t count = 0;
	core::Vec3d pos{};
	double age = 0.0; // seconds since spawn
	// Phase 6.11: resolved at spawn() time (the caller's per-item override, or
	// the system's own engine-default), not looked up again on every tick.
	double pickup_radius = 0.0;
	double lifetime_seconds = 0.0;
};

struct ItemPickup {
	core::NetId player = core::NetId::kInvalid;
	core::BlockId item = core::BlockId::kAir;
	std::uint16_t count = 0;
};

struct ItemDropTickResult {
	// One entry per player who walked within pickup range of a drop this
	// tick -- the caller credits their inventory.
	std::vector<ItemPickup> pickups;
	// Drop ids that no longer exist (picked up or expired) -- the caller
	// removes them from its own replication interest grid.
	std::vector<core::NetId> removed;
};

class ItemDropSystem {
public:
	// `pickup_radius`: a player within this many blocks of a drop's position
	// auto-collects it. `lifetime_seconds`: an untouched drop despawns after
	// this long, so the world doesn't accumulate item entities forever.
	explicit ItemDropSystem(
			double pickup_radius = 1.5, double lifetime_seconds = 120.0);

	// Spawns one drop at `pos` and returns its id (for the caller's own
	// interest-grid upsert -- ItemDropSystem doesn't know about replication).
	// Ids come from a high, non-overlapping range so they can never collide
	// with a player's NetId (players start at 1 and count up).
	//
	// `pickup_radius`/`lifetime_seconds`, if set, override this system's own
	// construction-time defaults for this one drop (Phase 6.11: a pack's
	// per-item `register_block{pickup_radius=..., item_lifetime_seconds=...}`)
	// -- resolved once here and stored on the drop, not re-read every tick.
	core::NetId spawn(core::Vec3d pos, core::BlockId item, std::uint16_t count,
			std::optional<double> pickup_radius = std::nullopt,
			std::optional<double> lifetime_seconds = std::nullopt);

	// Ages every drop, checks each given player position against every drop
	// for pickup (nearest player wins if more than one is in range this
	// tick), and expires anything past `lifetime_seconds`. Pure output --
	// applying the result (inventory credit, interest-grid removal) is the
	// caller's job.
	ItemDropTickResult tick(double dt,
			const std::vector<std::pair<core::NetId, core::Vec3d>> &players);

	const std::unordered_map<core::NetId, ItemDrop> &drops() const {
		return drops_;
	}
	std::size_t count() const { return drops_.size(); }

private:
	double pickup_radius_;
	double lifetime_seconds_;
	std::unordered_map<core::NetId, ItemDrop> drops_;
	// Starts past any plausible player NetId range (ServerSession's
	// next_net_id_ starts at 1 and counts up by one per join) so the two id
	// spaces never collide without needing to share a counter.
	std::uint32_t next_id_ = 0x8000'0000u;
};

} // namespace vb::world
