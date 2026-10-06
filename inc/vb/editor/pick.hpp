#pragma once

#include "vb/core/math.hpp"
#include "vb/editor/volume.hpp"

// Mouse picking against the structure (docs/structure-editor.md §I): which
// cell the cursor is on and which face of it, so "place" knows the empty cell
// next to the face and "remove" knows the cell itself.

namespace vb::editor {

struct PickResult {
	bool hit = false;
	// True if the ray stopped at an existing cell (otherwise it hit the
	// ground plane or nothing).
	bool on_cell = false;
	core::IVec3 cell{}; // the cell hit (when on_cell) or the ground cell
	core::IVec3 normal{}; // outward face normal of the hit face, axis-aligned
	// Where a new block goes: the cell across `normal` from `cell`, or the
	// ground cell itself. May be out of bounds; callers check.
	core::IVec3 place{};
};

struct PickOptions {
	// Cells with y > max_y are ignored (the layer-slice slider).
	int max_y = 1 << 20;
	// Height of the ground plane in cell units (the anchor's bottom face).
	int ground_y = 0;
	// How far past the structure the ground plane counts as a hit.
	int ground_pad = 4;
};

// `origin` and `dir` are in cell space (volume corner at 0,0,0; one unit per
// cell). A cell counts as hittable when it isn't keep.
PickResult pick_cell(const Volume &volume, core::Vec3d origin, core::Vec3d dir, const PickOptions &options = {});

} // namespace vb::editor
