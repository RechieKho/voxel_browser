#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "vb/cli/layout.hpp"
#include "vb/cli/store.hpp"

// `vb pack ...`: scaffolding and validation for content packs
// (architecture_spec/dev-experience.md §3.5). Non-interactive: every value has a flag and a
// default; `--json` prints what happened.

namespace vb::cli {

struct PackInitOptions {
	std::filesystem::path dir = ".";
	std::string tmpl = "minimal"; // minimal | ui | worldgen | base
	std::string name; // pack id; default derived from the directory name
	std::string engine_req; // default ">=<selected version>"
	std::string version; // installed version to take stubs / `base` from; default = the default
	bool force = false;
};

struct PackInitResult {
	Status status;
	std::string name;
	std::string engine_req;
	std::vector<std::string> created; // pack-relative paths
};

PackInitResult pack_init(const Layout &layout, const PackInitOptions &opts);

// (Re)writes <dir>/.vb/lua/*.lua from the version's sdk (or the copies baked into vb) and
// <dir>/.luarc.json when absent (or `overwrite_luarc`).
Status pack_write_types(const Layout &layout, const std::filesystem::path &dir, const std::string &version,
		bool overwrite_luarc, std::vector<std::string> *written);

// Newest installed entry that satisfies the pack's engine_version_req (or `explicit_version`,
// which must satisfy it too). nullopt + `why` (with the `vb install` hint) when none does.
std::optional<Entry> pick_entry_for_pack(const Layout &layout, const std::filesystem::path &pack_dir,
		const std::string &explicit_version, std::string *why);

struct PackHooks {
	// `vb host ...` and `vb launch ...` argument lists, run by the caller's implementation.
	std::function<int(const std::vector<std::string> &)> host;
	std::function<int(const std::vector<std::string> &)> launch;
};

// args = everything after `pack`. Returns a vb exit code (0 ok, 1 failure, 2 usage).
int run_pack_command(const Layout &layout, std::ostream &out, std::ostream &err,
		const std::vector<std::string> &args, const PackHooks &hooks);

} // namespace vb::cli
