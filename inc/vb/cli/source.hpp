#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "vb/cli/store.hpp" // Status

// Where releases come from (dev-cli.md §4.2): GitHub release downloads, or a
// local directory holding release.toml + zips (`dir:/path`; tests, LAN mirrors).

namespace vb::cli {

// (bytes_done, bytes_total or 0 when unknown)
using ProgressFn = std::function<void(std::uint64_t, std::uint64_t)>;

class Source {
public:
	virtual ~Source() = default;

	// release.toml text for `version` ("latest" or a tag such as "v0.2.0").
	virtual Status fetch_manifest(std::string_view version, std::string &out) = 0;

	// Fetches archive `file` of `version` to `dest` (a *.part path). If `dest`
	// already holds a partial download the source may resume it.
	virtual Status fetch_file(std::string_view version, const std::string &file,
			const std::filesystem::path &dest, const ProgressFn &progress) = 0;

	virtual std::string describe() const = 0;
};

// DirSource: <dir>/release.toml + zips; serves exactly the one version that
// manifest describes ("latest" or that tag).
std::unique_ptr<Source> make_dir_source(std::filesystem::path dir);

// HttpSource: https://github.com/<owner>/<repo>/releases/{latest/download,download/<tag>}/...
// `base_url` replaces https://github.com for tests / mirrors.
std::unique_ptr<Source> make_http_source(std::string repo,
		std::string base_url = "https://github.com");

// "dir:/path" -> DirSource, "owner/repo" -> HttpSource. nullptr + `error` on a
// malformed spec.
std::unique_ptr<Source> make_source(std::string_view spec, std::string *error = nullptr);

// Source spec: $VB_SOURCE, else cli.toml `source`, else the project default.
std::string configured_source_spec(const Layout &layout);

} // namespace vb::cli
