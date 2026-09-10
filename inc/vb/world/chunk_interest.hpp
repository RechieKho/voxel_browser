#pragma once

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <vector>

#include "vb/core/ids.hpp"

// Per-player visible chunk set (spec §8.5 InterestManagementSystem). A box of
// chunks around the player's chunk: `radius` horizontally, a smaller
// `vertical_radius` (worlds are wide and flat). Results are sorted so diffs
// against the previous tick are a linear set-difference.

namespace vb::world {

inline std::vector<core::ChunkCoord> chunks_in_view(core::ChunkCoord center,
		int radius, int vertical_radius) {
	std::vector<core::ChunkCoord> out;
	const int r = radius < 0 ? 0 : radius;
	const int vr = vertical_radius < 0 ? 0 : vertical_radius;
	out.reserve(static_cast<std::size_t>((2 * r + 1)) *
			static_cast<std::size_t>((2 * r + 1)) *
			static_cast<std::size_t>((2 * vr + 1)));
	for (int dy = -vr; dy <= vr; ++dy) {
		for (int dz = -r; dz <= r; ++dz) {
			for (int dx = -r; dx <= r; ++dx) {
				out.push_back({ center.x + dx, center.y + dy, center.z + dz });
			}
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

struct ChunkSetDiff {
	std::vector<core::ChunkCoord> entered; // in cur, not prev
	std::vector<core::ChunkCoord> left; // in prev, not cur
};

// Both inputs must be sorted ascending.
inline ChunkSetDiff diff_chunk_sets(const std::vector<core::ChunkCoord> &prev,
		const std::vector<core::ChunkCoord> &cur) {
	ChunkSetDiff d;
	std::set_difference(cur.begin(), cur.end(), prev.begin(), prev.end(),
			std::back_inserter(d.entered));
	std::set_difference(prev.begin(), prev.end(), cur.begin(), cur.end(),
			std::back_inserter(d.left));
	return d;
}

} // namespace vb::world
