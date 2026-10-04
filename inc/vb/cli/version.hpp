#pragma once

#include <optional>
#include <string>
#include <string_view>

// Version names for installed copies: release tags ("v1.2.3") and link names
// ("dev"). See architecture_spec/dev-cli.md §3/§5.

namespace vb::cli {

struct Version {
	int major = 0;
	int minor = 0;
	int patch = 0;

	friend bool operator==(const Version &, const Version &) = default;
	friend auto operator<=>(const Version &, const Version &) = default;
};

// Parses "v1.2.3" or "1.2.3" (a tag suffix such as "-3-gabc" is rejected: only
// clean release tags are installable). nullopt otherwise.
std::optional<Version> parse_version(std::string_view text);

// Canonical tag form: "v1.2.3".
std::string to_tag(const Version &v);

// A link name is [a-z][a-z0-9_-]{0,31}, must not parse as a version, and must
// not collide with the reserved selectors ("latest", "default").
bool is_valid_link_name(std::string_view name);

} // namespace vb::cli
