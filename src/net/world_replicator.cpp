#include "vb/net/world_replicator.hpp"

#include <algorithm>
#include <cmath>

#include "vb/protocol/world.hpp"
#include "vb/world/chunk_codec.hpp"
#include "vb/world/chunk_interest.hpp"

namespace vb::net {

namespace {

core::ChunkCoord chunk_of_pos(core::Vec3d p) {
	return core::chunk_of({ static_cast<std::int32_t>(std::floor(p.x)),
			static_cast<std::int32_t>(std::floor(p.y)),
			static_cast<std::int32_t>(std::floor(p.z)) });
}

void sort_unique(std::vector<core::ChunkCoord> &v) {
	std::sort(v.begin(), v.end());
	v.erase(std::unique(v.begin(), v.end()), v.end());
}

} // namespace

WorldReplicator::WorldReplicator(world::World &world,
		worldgen::WorldGenWorkerPool &pool,
		const world::BlockRegistry &registry, int view_distance_chunks,
		int vertical_view_chunks) : world_(world),
									lifecycle_(world, pool, registry),
									view_distance_(view_distance_chunks),
									vertical_view_(vertical_view_chunks) {}

std::vector<WorldReplicator::PlayerFrames> WorldReplicator::tick(
		const std::vector<std::pair<core::NetId, core::Vec3d>> &players) {
	// 1. Union of every player's view box -> lifecycle.
	std::vector<core::ChunkCoord> desired;
	std::vector<std::pair<core::NetId, std::vector<core::ChunkCoord>>> per_player;
	per_player.reserve(players.size());
	for (const auto &[id, pos] : players) {
		auto view = world::chunks_in_view(chunk_of_pos(pos), view_distance_,
				vertical_view_);
		desired.insert(desired.end(), view.begin(), view.end());
		per_player.emplace_back(id, std::move(view));
	}
	sort_unique(desired);
	lifecycle_.update(desired);

	// 2. Per player: diff the loaded-and-visible set vs. what we last sent.
	std::vector<PlayerFrames> out;
	out.reserve(players.size());
	for (auto &[id, view] : per_player) {
		std::vector<core::ChunkCoord> visible;
		visible.reserve(view.size());
		for (core::ChunkCoord c : view) {
			if (world_.has_chunk(c)) {
				visible.push_back(c);
			}
		}
		// `view` is already sorted, so `visible` is too.

		const world::ChunkSetDiff diff =
				world::diff_chunk_sets(last_sent_[id], visible);

		PlayerFrames pf;
		pf.id = id;
		for (core::ChunkCoord c : diff.entered) {
			const world::Chunk *chunk = world_.find_chunk(c);
			if (chunk == nullptr) {
				continue;
			}
			protocol::S2CChunkAdd msg;
			msg.coord = c;
			msg.revision = chunk->revision();
			msg.payload = world::encode_chunk_payload(*chunk);
			pf.frames.push_back(frame_message(msg));
		}
		for (core::ChunkCoord c : diff.left) {
			protocol::S2CChunkRemove msg;
			msg.coord = c;
			pf.frames.push_back(frame_message(msg));
		}

		last_sent_[id] = std::move(visible);
		if (!pf.frames.empty()) {
			out.push_back(std::move(pf));
		}
	}
	return out;
}

} // namespace vb::net
