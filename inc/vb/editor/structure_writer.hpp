#pragma once

#include <string>
#include <vector>

#include "vb/worldgen/structure.hpp"

// Canonical Lua text for a structure (docs/structure-editor.md §C, §J): stable
// key order and layout, so saving an unchanged file is byte-identical and
// diffs only show real edits.

namespace vb::editor {

// The text of `structures/<local>.lua`. Placement fields appear only when set.
std::string write_structure_lua(const worldgen::StructureSpec &spec);

// The text of the reserved `structures/all.lua`: a data script returning every
// structure in the folder. `stems` are the file names without extension,
// already sorted.
std::string write_all_index(const std::vector<std::string> &stems);

// The file stem a structure name is saved under: the part after the last ':'
// with anything but letters, digits, '_' and '-' replaced by '_'.
std::string structure_file_stem(const std::string &name);

// The reserved index file's stem; never listed as a structure.
inline constexpr const char *kAllIndexStem = "all";

} // namespace vb::editor
