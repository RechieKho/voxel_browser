#pragma once

#include <cstdint>
#include <filesystem>

#include "vb/cli/store.hpp" // Status

namespace vb::cli {

struct ExtractLimits {
	std::uint64_t max_total_bytes = 4ull << 30; // zip-bomb guard
	std::uint64_t max_entries = 20000;
};

// Extracts `zip_path` into `dest` (must exist). Rejects absolute paths, `..`
// or drive-letter components, backslashes, symlinks and oversized archives
// (zip-slip / zip-bomb). Executable bits are NOT taken from the archive: the
// known binaries (voxel_browser, voxel_browser_server, vb; `.exe` on Windows)
// at the archive root are chmod +x explicitly.
Status extract_zip(const std::filesystem::path &zip_path, const std::filesystem::path &dest,
		const ExtractLimits &limits = {});

} // namespace vb::cli
