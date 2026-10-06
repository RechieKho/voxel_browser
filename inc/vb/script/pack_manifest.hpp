#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "vb/core/version_req.hpp"

// A content pack's `pack.toml` (docs: content-pack-format). Only the fields
// the engine reads are modelled. Needs no Lua, so the dedicated server, the
// singleplayer host, `--check-pack` and `vb` all share it.

namespace vb::script {

struct PackManifest {
	bool found = false; // pack.toml exists and parsed
	std::string name;
	std::string version;
	std::string entry;
	// Raw `engine_version_req`, nullopt when the key is absent.
	std::optional<std::string> engine_version_req;
	int engine_version_req_line = 0; // 1-based line of that key, 0 if absent
	std::string error; // parse error ("pack.toml:3: ..."), empty on success / absent
};

// Reads `<pack_dir>/pack.toml`. A missing file is not an error (`found` = false).
PackManifest read_pack_manifest(const std::filesystem::path &pack_dir);

// Checks the manifest's `engine_version_req` against `engine` (see
// core::check_engine_req). A pack without a pack.toml passes silently.
core::EngineReqCheck check_pack_engine_req(const PackManifest &manifest, const core::SemVer &engine);

} // namespace vb::script
