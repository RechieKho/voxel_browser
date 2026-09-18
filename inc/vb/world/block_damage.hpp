#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"

// Shared block-damage breaking (spec §10.7, REMAINING_TASKS.md 6.5). A sparse
// pos -> damage map -- only blocks currently taking damage exist in it.
// Pure and dependency-free, same posture as world::ItemDropSystem: no net/
// script/session dependency, so it's unit-testable standalone. ServerSession
// owns one, ticks it once per server tick with Lua-hook callbacks it doesn't
// know the contents of, and drives the *existing* C2S_BlockEdit/
// BlockEditSystem/on_break pipeline when a block's damage reaches its
// max_damage -- this system only decides *when* that fires, never replaces
// it. The engine ships zero built-in accrual/heal policy (§10.7): with no
// damage_tick_fn/health_tick_fn wired in (no pack attached), damage never
// accrues and never heals.

namespace vb::world {

struct BlockDamageState {
	float damage = 0.0f;
	std::uint16_t max_damage = 0;
	std::uint64_t last_touched_tick = 0;
	std::vector<core::NetId> contributors;
};

// One block whose damage reached max_damage this tick. `contributor` is
// whichever player was contributing at the moment it completed (the first in
// registration order, arbitrary among concurrent "breaking together"
// contributors) -- the caller needs *someone's* eye position to satisfy
// apply_block_edit's reach check and attribute the on_break event to.
struct CompletedBreak {
	core::IVec3 pos{};
	core::NetId contributor = core::NetId::kInvalid;
};

struct BlockDamageTickResult {
	// Damage summed to (or past) max_damage this tick -- the caller commits
	// the actual break (existing BlockEdit pipeline) and forgets this pos.
	std::vector<CompletedBreak> completed;
	// Damage value changed but didn't complete -- worth re-replicating to
	// nearby players once a wire message consumes this (not wired yet, no
	// client renders cracks -- see REMAINING_TASKS.md 6.5's texture-atlas
	// dependency note).
	std::vector<core::IVec3> changed;
	// Damage returned to 0 with no more contributors -- caller despawns any
	// replicated record for this pos.
	std::vector<core::IVec3> cleared;
};

class BlockDamageSystem {
public:
	// Starts (or refreshes) `player` contributing to `pos`. Callers gate on
	// `max_damage > 0` themselves -- this system is never consulted for a
	// block whose max_damage is 0 (spec's "today's instant break" case).
	void begin(core::IVec3 pos, core::NetId player, std::uint16_t max_damage,
			std::uint64_t tick);

	// Stops `player` contributing to `pos` (released, moved off target).
	// No-op if they weren't contributing.
	void stop(core::IVec3 pos, core::NetId player);

	// Removes `player` from every pos they're contributing to (disconnect).
	void remove_player(core::NetId player);

	// `damage_tick_fn(pos, player, max_damage)` fires once per tick per
	// (damaged pos, contributing player) pair, returning the delta to add.
	// `health_tick_fn(pos, damage, max_damage, ticks_since_last_hit)` fires
	// once per tick per damaged pos regardless of contributors, returning a
	// replacement damage value or nullopt for "unchanged" -- the engine only
	// tracks last_touched_tick and calls the hook (no built-in heal policy).
	// Either callback may be empty (no pack attached / no handler
	// registered), in which case that half of the tick is a no-op.
	BlockDamageTickResult tick(std::uint64_t server_tick,
			const std::function<float(core::IVec3, core::NetId, std::uint16_t)>
					&damage_tick_fn,
			const std::function<std::optional<float>(
					core::IVec3, float, std::uint16_t, std::uint64_t)>
					&health_tick_fn);

	const std::unordered_map<core::IVec3, BlockDamageState> &states() const {
		return states_;
	}

private:
	std::unordered_map<core::IVec3, BlockDamageState> states_;
};

} // namespace vb::world
