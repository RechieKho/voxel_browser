#pragma once

#include <filesystem>
#include <string>

#include "vb/cli/layout.hpp"
#include "vb/cli/signature.hpp"
#include "vb/cli/source.hpp"
#include "vb/cli/store.hpp"

// `vb self update` (dev-cli.md §8): replace the running vb with the newest
// release's CLI archive, verified like any install.

namespace vb::cli {

struct SelfUpdateOptions {
	std::string current_version; // this binary's version string (vb::kVersionString)
	std::filesystem::path target; // the executable to replace (default: the running one)
	std::string platform; // empty = current_platform()
	bool force = false; // reinstall the same version / replace a development build
	bool check_only = false; // report, change nothing
	bool run_version_check = true; // run `<new vb> --version` before swapping it in
	TrustPolicy trust; // release.toml signature policy
	ProgressFn progress;
};

struct SelfUpdateResult {
	Status status;
	std::string latest; // newest version the source offers
	bool up_to_date = false; // nothing newer than the current version
	bool updated = false; // the binary was replaced
};

SelfUpdateResult self_update(const Layout &layout, Source &source, const SelfUpdateOptions &opts);

} // namespace vb::cli
