#include "vb/worldgen/structure.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace vb::worldgen {

const char *replace_policy_name(ReplacePolicy policy) {
	switch (policy) {
		case ReplacePolicy::kAir:
			return "air";
		case ReplacePolicy::kAirAndPlants:
			return "air_and_plants";
		case ReplacePolicy::kAll:
			return "all";
	}
	return "air";
}

std::optional<ReplacePolicy> parse_replace_policy(const std::string &name) {
	if (name == "air") {
		return ReplacePolicy::kAir;
	}
	if (name == "air_and_plants") {
		return ReplacePolicy::kAirAndPlants;
	}
	if (name == "all") {
		return ReplacePolicy::kAll;
	}
	return std::nullopt;
}

PlacementSpec merge_placement(const PlacementSpec &base, const PlacementSpec &over) {
	PlacementSpec out = base;
	if (over.on) {
		out.on = over.on;
	}
	if (over.replace) {
		out.replace = over.replace;
	}
	if (over.rotate) {
		out.rotate = over.rotate;
	}
	if (over.mirror) {
		out.mirror = over.mirror;
	}
	if (over.min_spacing) {
		out.min_spacing = over.min_spacing;
	}
	if (over.max_slope) {
		out.max_slope = over.max_slope;
	}
	if (over.y_min) {
		out.y_min = over.y_min;
	}
	if (over.y_max) {
		out.y_max = over.y_max;
	}
	if (over.cluster) {
		out.cluster = over.cluster;
	}
	return out;
}

std::string validate_structure_spec(const StructureSpec &spec) {
	const std::string who = "structure '" + spec.name + "': ";
	if (spec.name.empty()) {
		return "structure: 'name' is required";
	}
	const auto dim_ok = [](int v) { return v >= 1 && v <= kMaxStructureDim; };
	if (!dim_ok(spec.size.x) || !dim_ok(spec.size.y) || !dim_ok(spec.size.z)) {
		return who + "size must be 1.." + std::to_string(kMaxStructureDim) +
				" on every axis (got " + std::to_string(spec.size.x) + "x" +
				std::to_string(spec.size.y) + "x" + std::to_string(spec.size.z) + ")";
	}
	if (spec.anchor.x < 0 || spec.anchor.x >= spec.size.x || spec.anchor.y < 0 ||
			spec.anchor.y >= spec.size.y || spec.anchor.z < 0 || spec.anchor.z >= spec.size.z) {
		return who + "anchor lies outside the structure's size";
	}
	if (spec.variants.empty()) {
		return who + "needs at least one variant";
	}
	for (std::size_t vi = 0; vi < spec.variants.size(); ++vi) {
		const StructureVariantSpec &variant = spec.variants[vi];
		const std::string vwho = who + "variant " + std::to_string(vi + 1) + ": ";
		if (!(variant.weight > 0.0) || !std::isfinite(variant.weight)) {
			return vwho + "weight must be a positive number";
		}
		if (variant.layers.size() != static_cast<std::size_t>(spec.size.y)) {
			return vwho + "expected " + std::to_string(spec.size.y) + " layers, got " +
					std::to_string(variant.layers.size());
		}
		for (std::size_t y = 0; y < variant.layers.size(); ++y) {
			const auto &rows = variant.layers[y];
			if (rows.size() != static_cast<std::size_t>(spec.size.z)) {
				return vwho + "layer " + std::to_string(y + 1) + " has " +
						std::to_string(rows.size()) + " rows, expected " +
						std::to_string(spec.size.z);
			}
			for (std::size_t z = 0; z < rows.size(); ++z) {
				if (rows[z].size() != static_cast<std::size_t>(spec.size.x)) {
					return vwho + "layer " + std::to_string(y + 1) + " row " +
							std::to_string(z + 1) + " is " + std::to_string(rows[z].size()) +
							" characters long, expected " + std::to_string(spec.size.x);
				}
				for (const char c : rows[z]) {
					if (spec.palette.find(c) == spec.palette.end()) {
						return vwho + "layer " + std::to_string(y + 1) + " row " +
								std::to_string(z + 1) + " uses '" + std::string(1, c) +
								"', which is not in the palette";
					}
				}
			}
		}
	}
	return validate_placement_spec(spec.placement, "structure '" + spec.name + "'");
}

std::string validate_placement_spec(const PlacementSpec &spec, const std::string &context) {
	const std::string who = context + ": ";
	if (spec.min_spacing && *spec.min_spacing < 1) {
		return who + "min_spacing must be at least 1";
	}
	if (spec.max_slope && *spec.max_slope < 0) {
		return who + "max_slope must not be negative";
	}
	if (spec.cluster && !(*spec.cluster >= 0.0 && *spec.cluster <= 1.0)) {
		return who + "cluster must be between 0 and 1";
	}
	if (spec.y_min && spec.y_max && *spec.y_min > *spec.y_max) {
		return who + "y_min is above y_max";
	}
	return {};
}

namespace {

// `base:air` is the one registered name that resolves to id 0 on purpose.
bool resolve_block(const world::BlockRegistry &registry, const std::string &name,
		core::BlockId &out) {
	const core::BlockId id = registry.find(name);
	if (id == core::BlockId::kAir && registry.get(core::BlockId::kAir).name != name) {
		return false;
	}
	out = id;
	return true;
}

} // namespace

bool resolve_structure(const StructureSpec &spec, const world::BlockRegistry &registry,
		StructureDef &out, std::string &error) {
	error = validate_structure_spec(spec);
	if (!error.empty()) {
		return false;
	}
	std::map<char, core::BlockId> ids;
	for (const auto &[key, name] : spec.palette) {
		if (!name) {
			ids[key] = kKeepCell;
			continue;
		}
		core::BlockId id;
		if (!resolve_block(registry, *name, id)) {
			error = "structure '" + spec.name + "': palette '" + std::string(1, key) +
					"' names unknown block '" + *name + "'";
			return false;
		}
		ids[key] = id;
	}

	StructureDef def;
	def.name = spec.name;
	def.anchor = spec.anchor;
	def.radius_xz = std::max({ spec.anchor.x, spec.size.x - 1 - spec.anchor.x, spec.anchor.z,
			spec.size.z - 1 - spec.anchor.z });
	for (const StructureVariantSpec &vs : spec.variants) {
		StructureVariant variant;
		variant.size = spec.size;
		variant.weight = vs.weight;
		variant.cells.reserve(static_cast<std::size_t>(spec.size.x) *
				static_cast<std::size_t>(spec.size.y) * static_cast<std::size_t>(spec.size.z));
		for (int y = 0; y < spec.size.y; ++y) {
			for (int z = 0; z < spec.size.z; ++z) {
				for (const char c : vs.layers[static_cast<std::size_t>(y)][static_cast<std::size_t>(z)]) {
					variant.cells.push_back(ids.at(c));
				}
			}
		}
		def.variants.push_back(std::move(variant));
	}
	out = std::move(def);
	return true;
}

bool resolve_placement(std::uint32_t structure_index, double spawn_rate,
		const PlacementSpec &spec, const world::BlockRegistry &registry,
		const std::string &context, PlacementRule &out, std::string &error) {
	error = validate_placement_spec(spec, context);
	if (!error.empty()) {
		return false;
	}
	PlacementRule rule;
	rule.structure = structure_index;
	rule.spawn_rate = spawn_rate;
	if (spec.on) {
		for (const std::string &name : *spec.on) {
			core::BlockId id;
			if (!resolve_block(registry, name, id)) {
				error = context + ": placement 'on' names unknown block '" + name + "'";
				return false;
			}
			rule.on.push_back(id);
		}
	}
	rule.replace = spec.replace.value_or(ReplacePolicy::kAir);
	rule.rotate = spec.rotate.value_or(kDefaultRotate);
	rule.mirror = spec.mirror.value_or(kDefaultMirror);
	rule.min_spacing = spec.min_spacing.value_or(kDefaultMinSpacing);
	rule.max_slope = spec.max_slope.value_or(kDefaultMaxSlope);
	rule.y_min = spec.y_min.value_or(kDefaultYMin);
	rule.y_max = spec.y_max.value_or(kDefaultYMax);
	rule.cluster = spec.cluster.value_or(kDefaultCluster);
	if (rule.y_min > rule.y_max) {
		error = context + ": placement y_min is above y_max";
		return false;
	}
	out = std::move(rule);
	return true;
}

} // namespace vb::worldgen
