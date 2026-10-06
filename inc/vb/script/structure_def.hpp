#pragma once

#if VB_WITH_LUA

#include <sol/sol.hpp>

#include "vb/worldgen/structure.hpp"

// Table -> StructureSpec parsing, shared by `vb.register_structure`
// (PackRuntime) and the structure editor, so both accept exactly the same
// files (docs/structure-editor.md §C, §G).

namespace vb::script {

// Reads a structure table (name, size, anchor, palette, variants, placement).
// Throws sol::error naming the structure on any malformed field, including
// unknown keys, so a typo is never silently ignored. Block names are not
// checked against a registry here; see worldgen::resolve_structure.
worldgen::StructureSpec parse_structure(const sol::table &def);

// Reads the placement fields (on, replace, rotate, mirror, min_spacing,
// max_slope, y_min, y_max, cluster) out of `tbl`. Keys in `extra_allowed` (a
// biome entry's `structure` and `spawn_rate`) are skipped; any other unknown
// key throws sol::error prefixed with `context`.
worldgen::PlacementSpec parse_placement(const sol::table &tbl, const std::string &context,
		std::initializer_list<const char *> extra_allowed = {});

} // namespace vb::script

#endif // VB_WITH_LUA
