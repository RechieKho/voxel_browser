#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"

// Phase-1 hand-rolled interest management (spec §8.4, docs/replication.md).
// Same diff semantics as librg's chunk-radius query behind a narrow interface,
// so librg can be swapped in underneath later (§19 Q3). Linear scan — fine for
// the player counts of the first playable base; librg replaces it for scale.

namespace vb::replication {

struct EntityState {
	core::NetId net_id = core::NetId::kInvalid;
	core::EntityKindId kind = core::EntityKindId::kInvalid;
	core::Vec3d pos{};
	core::Vec2f rot{}; // yaw, pitch
	core::Vec3f vel{};

	bool operator==(const EntityState &) const = default;
};

class InterestGrid {
public:
	explicit InterestGrid(double cell_size = 32.0) : cell_size_(cell_size > 0.0 ? cell_size : 32.0) {}

	void upsert(const EntityState &state) { entities_[state.net_id] = state; }
	void remove(core::NetId id) { entities_.erase(id); }
	void clear() { entities_.clear(); }
	std::size_t size() const { return entities_.size(); }

	const EntityState *get(core::NetId id) const {
		const auto it = entities_.find(id);
		return it == entities_.end() ? nullptr : &it->second;
	}

	// Net ids whose interest cell is within `radius_cells` (Chebyshev distance)
	// of `eye`'s cell, excluding `self`. Sorted ascending for stable diffs.
	std::vector<core::NetId> visible_from(core::Vec3d eye, int radius_cells,
			core::NetId self) const {
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

private:
	std::int64_t cell_of(double v) const {
		return static_cast<std::int64_t>(std::floor(v / cell_size_));
	}

	double cell_size_;
	std::unordered_map<core::NetId, EntityState> entities_;
};

struct InterestDiff {
	std::vector<core::NetId> entered; // in cur, not in prev
	std::vector<core::NetId> stayed; // in both
	std::vector<core::NetId> left; // in prev, not in cur
};

// Both inputs must be sorted ascending (visible_from() returns them that way).
inline InterestDiff diff_interest(const std::vector<core::NetId> &prev,
		const std::vector<core::NetId> &cur) {
	InterestDiff d;
	std::set_difference(cur.begin(), cur.end(), prev.begin(), prev.end(),
			std::back_inserter(d.entered));
	std::set_intersection(cur.begin(), cur.end(), prev.begin(), prev.end(),
			std::back_inserter(d.stayed));
	std::set_difference(prev.begin(), prev.end(), cur.begin(), cur.end(),
			std::back_inserter(d.left));
	return d;
}

} // namespace vb::replication
