#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "vb/editor/block_catalog.hpp"
#include "vb/editor/structure_doc.hpp"

// The content pack the editor works in (docs/structure-editor.md §H): the
// block catalog from its block data script and the structure files under
// `<pack>/structures/`. Reads data scripts only and writes nothing but
// `structures/<name>.lua` and `structures/all.lua`.

namespace vb::editor {

struct EditorError {
	std::string file;
	std::string message;
};

struct StructureEntry {
	std::string name; // e.g. "base:oak_tree"; the file stem if the file failed to load
	std::filesystem::path path;
	bool ok = false;
};

// Lists the structure files (without all.lua) in `structures_dir`, sorted by
// file name; empty when the folder doesn't exist.
std::vector<std::filesystem::path> list_structure_files(const std::filesystem::path &structures_dir);

// Regenerates `<structures_dir>/all.lua` from the files present.
bool write_structures_index(const std::filesystem::path &structures_dir, std::string *error);

// Writes an empty structure (one all-keep variant, anchor at the bottom
// center) into `<pack_root>/structures/` and regenerates all.lua. `name` is
// the full "pack:local" name; fails if the file exists. `out_path` receives the
// file written.
bool create_structure_file(const std::filesystem::path &pack_root, const std::string &name, core::IVec3 size,
		std::filesystem::path *out_path, std::string *error);

class Workspace {
public:
	// Loads the block data script and scans `structures/`. nullopt (and
	// `error` set) when the script can't be loaded at all; a broken structure
	// file only adds to errors().
	static std::optional<Workspace> open(
			const std::filesystem::path &block_data_script, std::string *error);

	const BlockCatalog &catalog() const { return catalog_; }
	const std::filesystem::path &block_script() const { return block_script_; }
	const std::filesystem::path &pack_root() const { return pack_root_; }
	const std::string &pack_name() const { return pack_name_; }
	std::filesystem::path structures_dir() const { return pack_root_ / "structures"; }
	// "<pack name>:" -- the prefix new structures are named with.
	std::string name_prefix() const { return pack_name_ + ":"; }

	const std::vector<StructureEntry> &structures() const { return structures_; }
	const std::vector<EditorError> &errors() const { return errors_; }

	// Re-evaluates the block data script and rescans the structure files. If
	// the script fails the previous catalog stays, the failure is added to
	// errors() and false is returned.
	bool reload();

	// Loads a structure by its name ("base:oak_tree") or file stem.
	std::optional<StructureDoc> open_structure(const std::string &name_or_stem, std::string *error) const;

	// Where `doc_name` is saved.
	std::filesystem::path path_for(const std::string &doc_name) const;

	// Writes the structure file and regenerates all.lua, then rescans.
	bool save(StructureDoc &doc, std::string *error);

	// Regenerates structures/all.lua from the files present.
	bool write_index(std::string *error) const;

private:
	void scan();

	std::filesystem::path block_script_;
	std::filesystem::path pack_root_;
	std::string pack_name_;
	BlockCatalog catalog_;
	std::vector<StructureEntry> structures_;
	std::vector<EditorError> errors_;
};

} // namespace vb::editor
