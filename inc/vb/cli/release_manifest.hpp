#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vb/cli/store.hpp" // Status

// release.toml (dev-cli.md §4.2): what a release contains and how to verify it.

namespace vb::cli {

struct ArtifactInfo {
	std::string kind;     // "game" | "cli"
	std::string platform; // "linux-x86_64", "macos-universal", "windows-x86_64"
	std::string build;    // "release" | "debug"
	std::string file;     // archive file name (no directories)
	std::uint64_t size = 0;
	std::string sha256;   // lowercase hex
};

struct ReleaseManifest {
	int schema = 0;
	std::string version; // "v0.2.0"
	std::string commit;
	std::string date;
	int engine_protocol_version = 0;
	std::vector<ArtifactInfo> artifacts;
};

// Parses and validates (schema == 1, clean version tag, sane artifact fields:
// file names without path separators, 64-hex sha256).
Status parse_manifest(std::string_view text, ReleaseManifest &out);

// "linux-x86_64" etc. for the running binary; empty when unsupported.
std::string current_platform();

// The artifact for (kind, platform, build); nullptr when absent.
const ArtifactInfo *select_artifact(const ReleaseManifest &m, std::string_view kind,
		std::string_view platform, std::string_view build);

} // namespace vb::cli
