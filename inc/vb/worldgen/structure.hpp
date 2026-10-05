#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/world/block.hpp"

// Decorative structures (docs/structure-editor.md §C-E). Two layers:
//
//   * StructureSpec / PlacementSpec -- the authoring form: block *names*, one
//     string per row, every placement field optional so a biome entry can
//     override only some of them. This is what a `structures/*.lua` data
//     script contains, what vb::script::parse_structure produces, and what the
//     structure editor edits and writes back. No Lua types, so it builds with
//     VB_WITH_LUA off.
//   * StructureDef / PlacementRule -- the resolved form: block ids, dense cell
//     arrays, concrete placement numbers. Built once per pack by
//     resolve_structure()/resolve_placement(), immutable, and safe to evaluate
//     lock-free from WorldGenWorkerPool threads (no sol2, no registry access).

namespace vb::worldgen {

// A structure's size is capped on every axis so the cross-chunk anchor search
// radius stays bounded (docs/structure-editor.md §C).
inline constexpr int kMaxStructureDim = 64;

// Cell value meaning "leave the existing terrain untouched" (a `false`
// palette entry).
inline constexpr core::BlockId kKeepCell = static_cast<core::BlockId>(0xFFFF);

enum class ReplacePolicy : std::uint8_t {
	kAir, // only write into air
	kAirAndPlants, // air or blocks flagged `replaceable`
	kAll, // overwrite anything
};

const char *replace_policy_name(ReplacePolicy policy);
// "air" | "air_and_plants" | "all".
std::optional<ReplacePolicy> parse_replace_policy(const std::string &name);

// --- Authoring form --------------------------------------------------------

// Every field is optional: a structure's `placement` provides defaults and a
// biome's decoration entry may override any of them.
struct PlacementSpec {
	std::optional<std::vector<std::string>> on; // block names under the anchor
	std::optional<ReplacePolicy> replace;
	std::optional<bool> rotate;
	std::optional<bool> mirror;
	std::optional<int> min_spacing;
	std::optional<int> max_slope;
	std::optional<int> y_min;
	std::optional<int> y_max;
	std::optional<double> cluster;

	bool operator==(const PlacementSpec &) const = default;
	bool empty() const { return *this == PlacementSpec{}; }
};

struct StructureVariantSpec {
	double weight = 1.0;
	// layers[y][z] is one row of `size.x` palette characters; layer 0 is the
	// bottom.
	std::vector<std::vector<std::string>> layers;

	bool operator==(const StructureVariantSpec &) const = default;
};

struct StructureSpec {
	std::string name;
	core::IVec3 size{ 1, 1, 1 };
	core::IVec3 anchor{ 0, 0, 0 };
	// Palette character -> block name; nullopt is "keep" (`false` in Lua).
	// Ordered so writers emit a stable key order.
	std::map<char, std::optional<std::string>> palette;
	std::vector<StructureVariantSpec> variants;
	PlacementSpec placement;

	bool operator==(const StructureSpec &) const = default;
};

// --- Resolved form ---------------------------------------------------------

struct StructureVariant {
	core::IVec3 size{ 1, 1, 1 };
	// size.x*size.y*size.z cells, index = (y * size.z + z) * size.x + x;
	// kKeepCell leaves terrain alone.
	std::vector<core::BlockId> cells;
	double weight = 1.0;

	bool operator==(const StructureVariant &) const = default;

	core::BlockId at(int x, int y, int z) const {
		return cells[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size.z) +
							  static_cast<std::size_t>(z)) *
						 static_cast<std::size_t>(size.x) +
				static_cast<std::size_t>(x)];
	}
};

struct StructureDef {
	std::string name;
	core::IVec3 anchor{ 0, 0, 0 };
	std::vector<StructureVariant> variants;
	// Largest horizontal reach from the anchor over every variant, rotation
	// and mirror (Chebyshev, in blocks). Bounds the pull-stamping search.
	int radius_xz = 0;

	bool operator==(const StructureDef &) const = default;
};

struct PlacementRule {
	std::uint32_t structure = 0; // index into PackWorldGenPipeline::structures
	double spawn_rate = 0.0; // expected placements per 32x32 column
	std::vector<core::BlockId> on; // empty = any solid block
	ReplacePolicy replace = ReplacePolicy::kAir;
	bool rotate = false;
	bool mirror = false;
	int min_spacing = 4;
	int max_slope = 2;
	int y_min = 0;
	int y_max = 255;
	double cluster = 0.0;

	bool operator==(const PlacementRule &) const = default;
};

// Defaults every unset PlacementSpec field falls back to.
inline constexpr bool kDefaultRotate = false;
inline constexpr bool kDefaultMirror = false;
inline constexpr int kDefaultMinSpacing = 4;
inline constexpr int kDefaultMaxSlope = 2;
inline constexpr int kDefaultYMin = 0;
inline constexpr int kDefaultYMax = 255;
inline constexpr double kDefaultCluster = 0.0;

// `over`'s set fields win over `base`'s.
PlacementSpec merge_placement(const PlacementSpec &base, const PlacementSpec &over);

// Validates a spec's shape (size/anchor ranges, layer and row counts, palette
// keys used). Returns an empty string when valid, otherwise a message that
// names the structure. Shared by the Lua parser and the editor's writer.
std::string validate_structure_spec(const StructureSpec &spec);

// Checks the placement numbers (positive spacing, 0..1 cluster, y range).
// Returns an empty string when valid. `context` names the owner in the message.
std::string validate_placement_spec(const PlacementSpec &spec, const std::string &context);

// Resolves block names against `registry`. Returns false and fills `error`
// (naming the structure and the block) on an unknown name. `base:air` is a
// valid explicit-air palette entry.
bool resolve_structure(const StructureSpec &spec, const world::BlockRegistry &registry,
		StructureDef &out, std::string &error);

// Builds a concrete rule from defaults <- `spec`. `context` names the owner
// (structure or biome) in errors.
bool resolve_placement(std::uint32_t structure_index, double spawn_rate,
		const PlacementSpec &spec, const world::BlockRegistry &registry,
		const std::string &context, PlacementRule &out, std::string &error);

// Maps a cell offset (dx, dz) from the anchor through a mirror (negate x)
// then `rotation` quarter turns about +y. Used by placement and the editor.
inline void transform_offset(int dx, int dz, int rotation, bool mirror, int &out_x,
		int &out_z) {
	if (mirror) {
		dx = -dx;
	}
	switch (rotation & 3) {
		case 0:
			out_x = dx;
			out_z = dz;
			break;
		case 1:
			out_x = -dz;
			out_z = dx;
			break;
		case 2:
			out_x = -dx;
			out_z = -dz;
			break;
		default:
			out_x = dz;
			out_z = -dx;
			break;
	}
}

} // namespace vb::worldgen
