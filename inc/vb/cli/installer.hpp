#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

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

// Size + SHA-256 check of a downloaded archive against its manifest entry.
Status verify_archive(const std::filesystem::path &file, const ArtifactInfo &artifact);

// Removes all but the `keep` newest installed releases, never the default and
// never a tag in `protected_versions` (in use by a server instance). Returns the
// removed tags; tags spared only because they are protected go to `skipped`.
Status prune_releases(const Layout &layout, int keep, std::vector<std::string> &removed,
		const std::set<std::string> &protected_versions = {},
		std::vector<std::string> *skipped = nullptr);

} // namespace vb::cli
