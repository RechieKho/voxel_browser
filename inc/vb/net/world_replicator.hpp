#pragma once

#include <unordered_map>
#include <utility>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/net/handshake.hpp" // OutgoingFrame + frame_message
#include "vb/world/block.hpp"
#include "vb/world/chunk_lifecycle.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/worker_pool.hpp"

// Server-side world replication (spec §8.5): keeps the loaded chunk set aligned
// with players' view distance, and produces per-player S2C_ChunkAdd /
// S2C_ChunkRemove frames as chunks enter and leave each player's box. Owned by
// the server session; driven once per tick.

namespace vb::net {

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

	void forget_player(core::NetId id) { last_sent_.erase(id); }

	const world::World &world() const { return world_; }
	std::size_t requested_chunk_count() const {
		return lifecycle_.requested_count();
	}

private:
	world::World &world_;
	world::ChunkLifecycleSystem lifecycle_;
	int view_distance_;
	int vertical_view_;
	std::unordered_map<core::NetId, std::vector<core::ChunkCoord>> last_sent_;
};

} // namespace vb::net
