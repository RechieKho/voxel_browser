#pragma once

#include <filesystem>

// What a running server writes into its own content pack directory, as
// opposed to what the pack author ships: vb.storage's `storage.json` and
// vb.db's `db/` tree (`db/<xx>/<sha256>`, plus `*.tmp` while a write is in
// flight). Server-private and constantly changing, so it is never part of
// the pack's content: the asset manifest never offers it to clients
// (assetsync::build_manifest) and `vb pack dev` never restarts over it
// (cli::snapshot_pack).

namespace vb::core {

// `relative` is pack-root-relative.
inline bool is_pack_runtime_state(const std::filesystem::path &relative) {
	if (relative.empty()) {
		return false;
	}
	return *relative.begin() == "db" || relative == "storage.json";
}

} // namespace vb::core
