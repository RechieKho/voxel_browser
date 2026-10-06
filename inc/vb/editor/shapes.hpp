#pragma once

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "vb/core/math.hpp"
#include "vb/editor/volume.hpp"

// Pure geometry the editing tools share (docs/structure-editor.md §I, S4).

namespace vb::editor {

struct Box {
	core::IVec3 min{};
	core::IVec3 max{}; // inclusive

	core::IVec3 size() const { return { max.x - min.x + 1, max.y - min.y + 1, max.z - min.z + 1 }; }
	bool contains(core::IVec3 p) const {
		return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
	}
	bool operator==(const Box &) const = default;
};

// The box spanned by two corners, in either order.
inline Box box_between(core::IVec3 a, core::IVec3 b) {
	return { { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) },
		{ std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) } };
}

// Clips `box` to [0, size); returns false when nothing is left.
inline bool clip_box(Box &box, core::IVec3 size) {
	box.min = { std::max(box.min.x, 0), std::max(box.min.y, 0), std::max(box.min.z, 0) };
	box.max = { std::min(box.max.x, size.x - 1), std::min(box.max.y, size.y - 1), std::min(box.max.z, size.z - 1) };
	return box.min.x <= box.max.x && box.min.y <= box.max.y && box.min.z <= box.max.z;
}

// Every cell of a box, x fastest.
inline std::vector<core::IVec3> box_positions(const Box &box) {
	std::vector<core::IVec3> out;
	for (int y = box.min.y; y <= box.max.y; ++y) {
		for (int z = box.min.z; z <= box.max.z; ++z) {
			for (int x = box.min.x; x <= box.max.x; ++x) {
				out.push_back({ x, y, z });
			}
		}
	}
	return out;
}

// A 3D Bresenham line from a to b, endpoints included.
inline std::vector<core::IVec3> line_positions(core::IVec3 a, core::IVec3 b) {
	std::vector<core::IVec3> out;
	const int dx = std::abs(b.x - a.x);
	const int dy = std::abs(b.y - a.y);
	const int dz = std::abs(b.z - a.z);
	const int sx = a.x < b.x ? 1 : -1;
	const int sy = a.y < b.y ? 1 : -1;
	const int sz = a.z < b.z ? 1 : -1;
	const int dm = std::max({ dx, dy, dz });
	int x = a.x;
	int y = a.y;
	int z = a.z;
	int ex = dm / 2;
	int ey = dm / 2;
	int ez = dm / 2;
	for (int i = 0; i <= dm; ++i) {
		out.push_back({ x, y, z });
		ex -= dx;
		ey -= dy;
		ez -= dz;
		if (ex < 0) {
			ex += dm;
			x += sx;
		}
		if (ey < 0) {
			ey += dm;
			y += sy;
		}
		if (ez < 0) {
			ez += dm;
			z += sz;
		}
	}
	return out;
}

struct Symmetry {
	bool mirror_x = false;
	bool mirror_z = false;

	bool any() const { return mirror_x || mirror_z; }
};

// `pos` plus its mirror images across the structure's center planes, without
// duplicates (a cell on the axis maps to itself).
inline std::vector<core::IVec3> mirror_positions(core::IVec3 pos, core::IVec3 size, Symmetry sym) {
	std::vector<core::IVec3> out{ pos };
	const auto add = [&](core::IVec3 p) {
		if (std::find(out.begin(), out.end(), p) == out.end()) {
			out.push_back(p);
		}
	};
	if (sym.mirror_x) {
		add({ size.x - 1 - pos.x, pos.y, pos.z });
	}
	if (sym.mirror_z) {
		add({ pos.x, pos.y, size.z - 1 - pos.z });
	}
	if (sym.mirror_x && sym.mirror_z) {
		add({ size.x - 1 - pos.x, pos.y, size.z - 1 - pos.z });
	}
	return out;
}

// The 6-connected region of cells equal to the cell at `start`, within the
// volume.
inline std::vector<core::IVec3> flood_region(const Volume &volume, core::IVec3 start) {
	std::vector<core::IVec3> out;
	if (!volume.in_bounds(start)) {
		return out;
	}
	const Cell target = volume.get(start);
	std::vector<bool> seen(volume.volume(), false);
	std::vector<core::IVec3> stack{ start };
	seen[volume.index(start.x, start.y, start.z)] = true;
	constexpr core::IVec3 kDirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	while (!stack.empty()) {
		const core::IVec3 p = stack.back();
		stack.pop_back();
		out.push_back(p);
		for (const core::IVec3 d : kDirs) {
			const core::IVec3 n{ p.x + d.x, p.y + d.y, p.z + d.z };
			if (!volume.in_bounds(n) || volume.get(n) != target) {
				continue;
			}
			const std::size_t i = volume.index(n.x, n.y, n.z);
			if (!seen[i]) {
				seen[i] = true;
				stack.push_back(n);
			}
		}
	}
	return out;
}

} // namespace vb::editor
