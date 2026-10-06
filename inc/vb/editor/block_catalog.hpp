#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/world/block.hpp"

// What the editor knows about blocks (docs/structure-editor.md §G, §H): the
// engine's built-in set plus every entry of the pack's block data script,
// added through the same parse_block_type / register_block_type the server
// uses -- so ids, names, textures and flags match what the server builds.

namespace vb::editor {

// Registered after every real block (BlockCatalog::add_missing_marker). A structure cell naming a block the
// catalog doesn't know renders as this and is kept (by name) on save.
inline constexpr const char *kMissingBlockName = "editor:missing";
// The synthetic texture the missing marker is drawn with; the app supplies the
// bytes (a magenta checker) under this key in its virtual file system.
inline constexpr const char *kMissingTexturePath = "editor/missing.png";

class BlockCatalog {
public:
	BlockCatalog();

	const world::BlockRegistry &registry() const { return registry_; }
	world::BlockRegistry &registry() { return registry_; }

	// Names for the palette panel: every registered block except air and the
	// missing marker, in registry order.
	std::vector<std::string> block_names() const;

	bool known(const std::string &name) const;
	// The block id to draw `name` with: its registry id, base:air's id 0, or
	// the missing marker's id for an unknown name.
	core::BlockId render_id(const std::string &name) const;
	core::BlockId missing_id() const { return missing_id_; }

	// Registers the missing-block marker after every real block, so real
	// blocks keep exactly the ids the server gives them. Idempotent.
	void add_missing_marker();

private:
	world::BlockRegistry registry_;
	core::BlockId missing_id_ = core::BlockId::kAir;
};

struct CatalogResult {
	bool ok = false;
	// "<script>: <message>" when !ok.
	std::string error;
	BlockCatalog catalog;
	// The nearest ancestor folder of the script holding a pack.toml, else the
	// script's own folder.
	std::filesystem::path pack_root;
	// `name` from the pack's pack.toml; the folder name when there is none.
	std::string pack_name;
};

// Evaluates `block_data_script` (a data script returning a list of block
// tables) in a bare Lua state and builds a catalog from it. Never runs pack
// code. Without VB_WITH_LUA it always fails.
CatalogResult load_block_catalog(const std::filesystem::path &block_data_script);

// pack.toml discovery used by load_block_catalog, exposed for the workspace
// and tests.
std::filesystem::path find_pack_root(const std::filesystem::path &script);
std::string read_pack_name(const std::filesystem::path &pack_root);

} // namespace vb::editor
