#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Files baked into `vb` at build time (cmake/EmbedFiles.cmake): the `vb pack init` templates under
// "templates/pack/" and the Lua API stubs under "sdk/lua/". Keys are repo-relative paths with
// forward slashes.

namespace vb::cli {

struct EmbeddedFile {
	const char *path; // nullptr terminates the table
	const unsigned char *data;
	std::size_t size;
};

extern const EmbeddedFile kEmbeddedFiles[];
extern const std::size_t kEmbeddedFileCount;

// Every embedded file whose path starts with `prefix`, as {path-without-prefix, bytes}.
struct EmbeddedEntry {
	std::string path;
	std::string bytes;
};
std::vector<EmbeddedEntry> embedded_under(std::string_view prefix);

} // namespace vb::cli
