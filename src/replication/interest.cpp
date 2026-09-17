#include "vb/replication/interest.hpp"

#include <algorithm>
#include <cstdlib>

#if defined(VB_WITH_REPLICATION)
#include <librg.h>
#endif

namespace vb::replication {

#if defined(VB_WITH_REPLICATION)

namespace {

// librg chunk coordinates are int16 and a chunk id is (roughly) bounded by
// chunkamount.x * chunkamount.y * chunkamount.z fitting a signed 32-bit int
// internally (docs/replication.md's "clamp/verify world extent fits" item).
// 1024 chunks/axis keeps that product ~1.07e9 (well under INT32_MAX) while
// covering +-512*cell_size world units per axis around the origin -- ample
// for the player counts / world sizes this project targets today. An entity
// that strays outside this range gets LIBRG_CHUNK_INVALID from librg and is
// simply excluded from everyone's interest set until it re-enters.
constexpr std::uint16_t kLibrgChunksPerAxis = 1024;

std::int64_t librg_id_of(core::NetId id) {
	return static_cast<std::int64_t>(static_cast<std::uint32_t>(id));
}

} // namespace

InterestGrid::InterestGrid(double cell_size)
		: cell_size_(cell_size > 0.0 ? cell_size : 32.0) {
	librg_world_ = librg_world_create();
	auto *world = static_cast<librg_world *>(librg_world_);
	const auto size = static_cast<std::uint16_t>(
			std::clamp(cell_size_, 1.0, 65535.0));
	librg_config_chunksize_set(world, size, size, size);
	librg_config_chunkamount_set(
			world, kLibrgChunksPerAxis, kLibrgChunksPerAxis, kLibrgChunksPerAxis);
	librg_config_chunkoffset_set(
			world, LIBRG_OFFSET_MID, LIBRG_OFFSET_MID, LIBRG_OFFSET_MID);
}

InterestGrid::~InterestGrid() {
	librg_world_destroy(static_cast<librg_world *>(librg_world_));
}

void InterestGrid::upsert(const EntityState &state) {
	auto *world = static_cast<librg_world *>(librg_world_);
	const std::int64_t id = librg_id_of(state.net_id);
	if (!librg_entity_tracked(world, id)) {
		librg_entity_track(world, id);
		// Self-owned: librg_world_query() accumulates its visible-chunk set
		// from entities the querying id owns, so every tracked entity must
		// own itself to be usable as a query owner later.
		librg_entity_owner_set(world, id, id);
	}
	librg_entity_chunk_set(world, id,
			librg_chunk_from_realpos(world, state.pos.x, state.pos.y, state.pos.z));
	entities_[state.net_id] = state;
}

void InterestGrid::remove(core::NetId id) {
	librg_entity_untrack(static_cast<librg_world *>(librg_world_), librg_id_of(id));
	entities_.erase(id);
}

void InterestGrid::clear() {
	librg_world_destroy(static_cast<librg_world *>(librg_world_));
	librg_world_ = librg_world_create();
	auto *world = static_cast<librg_world *>(librg_world_);
	const auto size = static_cast<std::uint16_t>(
			std::clamp(cell_size_, 1.0, 65535.0));
	librg_config_chunksize_set(world, size, size, size);
	librg_config_chunkamount_set(
			world, kLibrgChunksPerAxis, kLibrgChunksPerAxis, kLibrgChunksPerAxis);
	librg_config_chunkoffset_set(
			world, LIBRG_OFFSET_MID, LIBRG_OFFSET_MID, LIBRG_OFFSET_MID);
	entities_.clear();
}

std::vector<core::NetId> InterestGrid::visible_from(
		core::Vec3d /*eye*/, int radius_cells, core::NetId self) const {
	auto *world = static_cast<librg_world *>(librg_world_);
	const std::int64_t self_id = librg_id_of(self);
	if (!librg_entity_tracked(world, self_id)) {
		return {};
	}
	const auto radius = static_cast<std::uint8_t>(
			std::clamp(radius_cells, 0, 255));

	std::vector<std::int64_t> buf(entities_.size() + 1);
	size_t amount = buf.size();
	std::int32_t overflow = librg_world_query(world, self_id, radius, buf.data(), &amount);
	if (overflow > 0) {
		buf.resize(buf.size() + static_cast<std::size_t>(overflow));
		amount = buf.size();
		overflow = librg_world_query(world, self_id, radius, buf.data(), &amount);
	}
	buf.resize(amount);

	std::vector<core::NetId> out;
	out.reserve(buf.size());
	for (std::int64_t id : buf) {
		const auto net_id = static_cast<core::NetId>(static_cast<std::uint32_t>(id));
		if (net_id != self) {
			out.push_back(net_id);
		}
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}

#else // !VB_WITH_REPLICATION -- hand-rolled linear scan backend

InterestGrid::InterestGrid(double cell_size)
		: cell_size_(cell_size > 0.0 ? cell_size : 32.0) {}

InterestGrid::~InterestGrid() = default;

void InterestGrid::upsert(const EntityState &state) { entities_[state.net_id] = state; }
void InterestGrid::remove(core::NetId id) { entities_.erase(id); }
void InterestGrid::clear() { entities_.clear(); }

std::vector<core::NetId> InterestGrid::visible_from(
		core::Vec3d eye, int radius_cells, core::NetId self) const {
	const std::int64_t ex = cell_of(eye.x);
	const std::int64_t ey = cell_of(eye.y);
	const std::int64_t ez = cell_of(eye.z);
	const std::int64_t r = radius_cells < 0 ? 0 : radius_cells;

	std::vector<core::NetId> out;
	for (const auto &[id, s] : entities_) {
		if (id == self) {
			continue;
		}
		const std::int64_t dx = std::llabs(cell_of(s.pos.x) - ex);
		const std::int64_t dy = std::llabs(cell_of(s.pos.y) - ey);
		const std::int64_t dz = std::llabs(cell_of(s.pos.z) - ez);
		if (dx <= r && dy <= r && dz <= r) {
			out.push_back(id);
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

#endif // VB_WITH_REPLICATION

} // namespace vb::replication
