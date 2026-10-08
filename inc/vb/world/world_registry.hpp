#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "vb/world/block.hpp"

// A saved world stores raw block ids (RegionStore), which only mean something
// against the block registry that wrote them. `<world_dir>/blocks.txt` records
// that registry -- block names in id order, plus the pack that wrote it -- so a
// world can't silently be loaded by a pack whose ids mean different blocks
// (base:sand turning into some other pack's block, untextured).

namespace vb::world {

inline constexpr const char *kWorldRegistryFile = "blocks.txt";

struct WorldRegistryCheck {
	bool ok = true;
	// ok: an informational note to log (may be empty). !ok: why the world
	// can't be used, naming the folder and the first conflicting id.
	std::string message;
};

// Compares `registry` (ids 0..size-1) with the registry recorded in
// `world_dir`, then records the current one. Compatible when the saved names
// are a prefix of the current ones -- a pack that only added blocks keeps
// every saved id's meaning. A world with region files but no record (saved
// before records existed) is accepted and recorded. Creates `world_dir` if
// needed.
WorldRegistryCheck check_world_registry(const std::filesystem::path &world_dir,
		const BlockRegistry &registry, std::string_view pack_name);

} // namespace vb::world
