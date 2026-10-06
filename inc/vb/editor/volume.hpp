#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vb/core/math.hpp"

// The editor's dense voxel grid (docs/structure-editor.md §J). A cell is an
// index into a StructureDoc's NameTable, with 0 meaning "keep" (leave the
// existing terrain alone). Storing names rather than block ids is what lets
// the editor keep a block it doesn't know about (a pack block registered
// outside the data script) instead of dropping it.

namespace vb::editor {

using Cell = std::uint16_t;
inline constexpr Cell kKeepCell = 0;

class Volume {
public:
	Volume() = default;
	explicit Volume(core::IVec3 size) : size_(size), cells_(volume_of(size), kKeepCell) {}

	core::IVec3 size() const { return size_; }
	std::size_t volume() const { return cells_.size(); }

	bool in_bounds(int x, int y, int z) const {
		return x >= 0 && y >= 0 && z >= 0 && x < size_.x && y < size_.y && z < size_.z;
	}
	bool in_bounds(core::IVec3 p) const { return in_bounds(p.x, p.y, p.z); }

	// Same layout as worldgen::StructureVariant: (y * size.z + z) * size.x + x.
	std::size_t index(int x, int y, int z) const {
		return (static_cast<std::size_t>(y) * static_cast<std::size_t>(size_.z) +
					   static_cast<std::size_t>(z)) *
				static_cast<std::size_t>(size_.x) +
				static_cast<std::size_t>(x);
	}

	// Out-of-bounds reads return kKeepCell; out-of-bounds writes are ignored
	// and report false.
	Cell get(int x, int y, int z) const { return in_bounds(x, y, z) ? cells_[index(x, y, z)] : kKeepCell; }
	Cell get(core::IVec3 p) const { return get(p.x, p.y, p.z); }
	bool set(int x, int y, int z, Cell cell) {
		if (!in_bounds(x, y, z)) {
			return false;
		}
		cells_[index(x, y, z)] = cell;
		return true;
	}
	bool set(core::IVec3 p, Cell cell) { return set(p.x, p.y, p.z, cell); }

	const std::vector<Cell> &cells() const { return cells_; }

	// A copy with a new size. The old content is placed at `shift` in the new
	// grid (so growing on the -x side shifts by +grow); cells that fall
	// outside are dropped and new cells are keep.
	Volume resized(core::IVec3 new_size, core::IVec3 shift) const {
		Volume out(new_size);
		for (int y = 0; y < size_.y; ++y) {
			for (int z = 0; z < size_.z; ++z) {
				for (int x = 0; x < size_.x; ++x) {
					out.set(x + shift.x, y + shift.y, z + shift.z, get(x, y, z));
				}
			}
		}
		return out;
	}

	bool operator==(const Volume &) const = default;

private:
	static std::size_t volume_of(core::IVec3 s) {
		return s.x <= 0 || s.y <= 0 || s.z <= 0
				? 0
				: static_cast<std::size_t>(s.x) * static_cast<std::size_t>(s.y) *
						static_cast<std::size_t>(s.z);
	}

	core::IVec3 size_{ 0, 0, 0 };
	std::vector<Cell> cells_;
};

} // namespace vb::editor
