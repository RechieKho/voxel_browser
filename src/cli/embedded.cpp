#include "vb/cli/embedded.hpp"

namespace vb::cli {

std::vector<EmbeddedEntry> embedded_under(std::string_view prefix) {
	std::vector<EmbeddedEntry> out;
	for (std::size_t i = 0; i < kEmbeddedFileCount; ++i) {
		const std::string_view path = kEmbeddedFiles[i].path;
		if (path.substr(0, prefix.size()) == prefix) {
			out.push_back({ std::string(path.substr(prefix.size())),
					std::string(reinterpret_cast<const char *>(kEmbeddedFiles[i].data), kEmbeddedFiles[i].size) });
		}
	}
	return out;
}

} // namespace vb::cli
