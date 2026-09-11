#include "vb/net/world_replicator.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "vb/protocol/world.hpp"
#include "vb/world/chunk_codec.hpp"
#include "vb/world/chunk_interest.hpp"
#include "vb/world/lighting.hpp"
#include "vb/world/paletted_chunk_store.hpp"

namespace vb::net {

namespace {

constexpr double kMaxReachBlocks = 5.5;

core::ChunkCoord chunk_of_pos(core::Vec3d p) {
	return core::chunk_of({ static_cast<std::int32_t>(std::floor(p.x)),
			static_cast<std::int32_t>(std::floor(p.y)),
			static_cast<std::int32_t>(std::floor(p.z)) });
}

std::uint32_t local_index_of_world(core::IVec3 p) {
	return static_cast<std::uint32_t>(world::index_of(
			core::floor_mod(p.x, core::kChunkDim),
			core::floor_mod(p.y, core::kChunkDim),
			core::floor_mod(p.z, core::kChunkDim)));
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

bool WorldReplicator::player_has_chunk(core::NetId id, core::ChunkCoord c) const {
	const auto it = last_sent_.find(id);
	if (it == last_sent_.end()) {
		return false;
	}
	return std::binary_search(it->second.begin(), it->second.end(), c);
}

std::vector<WorldReplicator::PlayerFrames> WorldReplicator::apply_block_edit(
		core::NetId editor, core::Vec3d eye_pos,
		const protocol::C2SBlockEdit &edit,
		protocol::S2CBlockEditResult &out_result) {
	out_result.predicted_seq = edit.predicted_seq;
	out_result.pos = edit.pos;
	out_result.accepted = false;

	const core::IVec3 p = edit.pos;
	const core::ChunkCoord cc = core::chunk_of(p);
	const world::BlockRegistry &reg = world_.registry();

	const core::Vec3d center{ static_cast<double>(p.x) + 0.5,
		static_cast<double>(p.y) + 0.5, static_cast<double>(p.z) + 0.5 };
	if ((center - eye_pos).length() > kMaxReachBlocks) {
		return {};
	}
	if (!world_.has_chunk(cc)) {
		return {};
	}

	const core::BlockId existing = world_.get_block(p);
	core::BlockId new_block = core::BlockId::kAir;
	if (edit.action == protocol::BlockEditAction::kBreak) {
		if (existing == core::BlockId::kAir) {
			return {};
		}
	} else {
		if (existing != core::BlockId::kAir) {
			return {};
		}
		if (edit.block == core::BlockId::kAir || !reg.contains(edit.block)) {
			return {};
		}
		bool touches = false;
		for (const core::IVec3 d : { core::IVec3{ 1, 0, 0 }, core::IVec3{ -1, 0, 0 },
					 core::IVec3{ 0, 1, 0 }, core::IVec3{ 0, -1, 0 },
					 core::IVec3{ 0, 0, 1 }, core::IVec3{ 0, 0, -1 } }) {
			if (reg.is_solid(world_.get_block(
						{ p.x + d.x, p.y + d.y, p.z + d.z }))) {
				touches = true;
				break;
			}
		}
		if (!touches) {
			return {};
		}
		new_block = edit.block;
	}
	// [Phase 4.2] a Lua "block_break"/"block_place" handler may veto here.

	world::Chunk *chunk = world_.find_chunk(cc);
	if (chunk == nullptr) {
		return {};
	}

	const std::array<world::Light, world::kChunkVolume> light_before =
			chunk->light_volume();

	world_.set_block(p, new_block); // bumps revision + dirty flags
	const world::LightEngine light_engine(reg);
	light_engine.relight_chunk(*chunk);

	protocol::S2CChunkDelta delta;
	delta.coord = cc;
	delta.new_revision = chunk->revision();
	delta.base_revision = delta.new_revision - 1;
	delta.blocks.push_back({ local_index_of_world(p), new_block });
	const auto &light_after = chunk->light_volume();
	for (std::size_t i = 0; i < world::kChunkVolume; ++i) {
		if (light_after[i].packed != light_before[i].packed) {
			delta.light.push_back(
					{ static_cast<std::uint32_t>(i), light_after[i].packed });
		}
	}

	out_result.accepted = true;

	std::vector<PlayerFrames> out;
	bool editor_covered = false;
	for (const auto &[id, sent] : last_sent_) {
		if (!std::binary_search(sent.begin(), sent.end(), cc)) {
			continue;
		}
		PlayerFrames pf;
		pf.id = id;
		pf.frames.push_back(frame_message(delta));
		out.push_back(std::move(pf));
		if (id == editor) {
			editor_covered = true;
		}
	}
	if (!editor_covered) {
		PlayerFrames pf;
		pf.id = editor;
		pf.frames.push_back(frame_message(delta));
		out.push_back(std::move(pf));
	}
	return out;
}

} // namespace vb::net
