#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/net/handshake.hpp" // OutgoingFrame + frame_message
#include "vb/protocol/world.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk_lifecycle.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/worker_pool.hpp"

// Server-side world replication (spec §8.5): keeps the loaded chunk set aligned
// with players' view distance, and produces per-player S2C_ChunkAdd /
// S2C_ChunkRemove frames as chunks enter and leave each player's box. Owned by
// the server session; driven once per tick.

namespace vb::net {

// Phase 4.2 seam: a script host may veto/observe block edits without
// WorldReplicator depending on script types (pure std::function, no sol2).
struct BlockEditHooks {
	// Fires before the edit is applied; return false to veto it (the edit is
	// then rejected exactly like a reach/target-validity failure).
	std::function<bool(core::NetId editor, core::IVec3 pos,
			core::BlockId existing, core::BlockId new_block, bool is_break)>
			before_edit;
	// Fires after the edit is applied (chunk mutated), before delta fan-out.
	std::function<void(core::NetId editor, core::IVec3 pos,
			core::BlockId removed, core::BlockId placed, bool is_break)>
			after_edit;
};

class WorldReplicator {
public:
	WorldReplicator(world::World &world, worldgen::WorldGenWorkerPool &pool,
			const world::BlockRegistry &registry, int view_distance_chunks,
			int vertical_view_chunks = 4);

	struct PlayerFrames {
		core::NetId id = core::NetId::kInvalid;
		std::vector<OutgoingFrame> frames;
	};

	// `players` maps net id -> authoritative world position.
	std::vector<PlayerFrames> tick(
			const std::vector<std::pair<core::NetId, core::Vec3d>> &players);

	void forget_player(core::NetId id) {
		last_sent_.erase(id);
		last_sent_revision_.erase(id);
	}

	// Validate + apply one block edit (spec §5.2 / §8.5). Fills `out_result` for
	// the editor and returns an S2C_ChunkDelta frame for every player who has the
	// affected chunk. `eye_pos` is the editor's authoritative eye position, used
	// for the reach check. A rejected edit changes nothing and returns no deltas.
	std::vector<PlayerFrames> apply_block_edit(core::NetId editor,
			core::Vec3d eye_pos, const protocol::C2SBlockEdit &edit,
			protocol::S2CBlockEditResult &out_result);

	// Chunks that a player currently mirrors (for edit fan-out + tests).
	bool player_has_chunk(core::NetId id, core::ChunkCoord c) const;

	const world::World &world() const { return world_; }
	world::World &world() { return world_; }
	std::size_t requested_chunk_count() const {
		return lifecycle_.requested_count();
	}

	// Phase 4.2: install a script host's block-edit veto/observer hooks.
	void set_block_edit_hooks(BlockEditHooks hooks) { hooks_ = std::move(hooks); }

private:
	BlockEditHooks hooks_;
	world::World &world_;
	world::ChunkLifecycleSystem lifecycle_;
	int view_distance_;
	int vertical_view_;
	std::unordered_map<core::NetId, std::vector<core::ChunkCoord>> last_sent_;
	// The chunk revision most recently sent to each player, for each chunk
	// they mirror. Presence/absence alone (last_sent_ above) only tells tick()
	// when a chunk enters/leaves a player's view; it says nothing about a
	// chunk that stays visible but changes server-side without an explicit
	// edit going through apply_block_edit -- which is exactly what a
	// lighting::relight_column() cascade does (see chunk_lifecycle.cpp): a
	// chunk can be generated, lit assuming open sky (nothing loaded above it
	// yet) and sent to a player, then have that guess corrected once its real
	// neighbour above finishes generating on another thread, arbitrarily many
	// ticks later. Without tracking the revision we last actually sent, that
	// correction never reaches an already-connected player. tick() compares
	// this against the world's current revision for every visible chunk and
	// re-sends a full S2C_ChunkAdd when they differ.
	std::unordered_map<core::NetId, std::unordered_map<core::ChunkCoord, std::uint64_t>>
			last_sent_revision_;
};

} // namespace vb::net
