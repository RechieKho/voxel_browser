#pragma once

#include <string>
#include <vector>
#include <string_view>

#include "vb/cli/layout.hpp"
#include "vb/cli/release_manifest.hpp"
#include "vb/cli/source.hpp"

namespace vb::cli {

struct InstallOptions {
	std::string build = "release"; // "release" | "debug"
	std::string platform;          // empty = current_platform()
	bool force = false;            // reinstall an already installed version
	bool keep_download = false;
	bool wait_for_lock = true;
	bool run_version_check = true; // run `<server> --version` before committing
	ProgressFn progress;
};

struct InstallResult {
	Status status;
	std::string version;       // resolved tag, e.g. "v0.2.0"
	bool already_installed = false;
};

// dev-cli.md §5.3: lock -> manifest -> download .part -> verify size+SHA-256 ->
// extract into versions/.staging-* -> validate -> receipt -> atomic rename.
// Any failure removes the staging dir; versions/<v> is never half-written.
InstallResult install_release(const Layout &layout, Source &source,
		std::string_view version_or_latest, const InstallOptions &opts);

// Removes all but the `keep` newest installed releases, never the default.
// Returns the removed tags.
Status prune_releases(const Layout &layout, int keep, std::vector<std::string> &removed);

} // namespace vb::cli
