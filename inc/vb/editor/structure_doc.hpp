#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vb/editor/volume.hpp"
#include "vb/worldgen/structure.hpp"

// The structure being edited (docs/structure-editor.md §H, §J): one Volume per
// variant sharing a NameTable of block names, plus the anchor and placement
// defaults. Converts to and from the authoring form (worldgen::StructureSpec)
// that structures/*.lua files hold, without ever resolving names against a
// registry -- so a name the block data script doesn't define survives a load
// and save unchanged.

namespace vb::editor {

// Block names used by a document's cells. Cell n (n >= 1) is entry n - 1.
class NameTable {
public:
	struct Entry {
		std::string name;
		// Palette character this name is written with; 0 until assigned. A
		// loaded file's characters are kept so an unchanged file saves
		// byte-identical.
		char key = 0;
		// Written even when no cell uses it (a loaded file's unused entry).
		bool keep_unused = false;
	};

	Cell intern(const std::string &name);
	std::optional<Cell> find(const std::string &name) const;
	const std::string &name(Cell cell) const { return entries_[static_cast<std::size_t>(cell) - 1].name; }
	std::size_t size() const { return entries_.size(); }
	const std::vector<Entry> &entries() const { return entries_; }
	std::vector<Entry> &entries() { return entries_; }

private:
	std::vector<Entry> entries_;
};

struct DocVariant {
	Volume volume;
	double weight = 1.0;
};

class StructureDoc {
public:
	std::string name;
	core::IVec3 anchor{ 0, 0, 0 };
	worldgen::PlacementSpec placement;
	std::vector<DocVariant> variants;
	NameTable names;
	// Palette character for "keep" cells.
	char keep_key = '.';

	// An empty structure: one all-keep variant.
	static StructureDoc create(const std::string &name, core::IVec3 size, core::IVec3 anchor);
	// Fails (nullopt, `error` set) when `spec` doesn't pass
	// worldgen::validate_structure_spec.
	static std::optional<StructureDoc> from_spec(const worldgen::StructureSpec &spec, std::string *error);

	core::IVec3 size() const { return variants.empty() ? core::IVec3{ 0, 0, 0 } : variants[0].volume.size(); }

	// Assigns a palette character to every used name that lacks one (and keeps
	// them from then on, so later edits never rename an existing key), then
	// builds the spec. Non-const for that reason.
	worldgen::StructureSpec to_spec();

	// True if any cell of any variant uses `cell`.
	bool uses(Cell cell) const;
};

} // namespace vb::editor
