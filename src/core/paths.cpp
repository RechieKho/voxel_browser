#include "vb/core/paths.hpp"

#include <cstdlib>

namespace vb::core {

namespace {

std::filesystem::path env_path(const char *name) {
#if defined(_WIN32)
	std::size_t len = 0;
	char *buf = nullptr;
	if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr) {
		std::filesystem::path p(buf);
		std::free(buf);
		return p;
	}
	return {};
#else
	const char *v = std::getenv(name);
	return v != nullptr ? std::filesystem::path(v) : std::filesystem::path{};
#endif
}

} // namespace

std::filesystem::path user_cache_dir() {
#if defined(_WIN32)
	std::filesystem::path base = env_path("LOCALAPPDATA");
	if (base.empty()) {
		base = env_path("TEMP");
	}
	return base / "voxel_browser" / "cache";
#elif defined(__APPLE__)
	std::filesystem::path home = env_path("HOME");
	return home / "Library" / "Caches" / "voxel_browser";
#else
	std::filesystem::path xdg = env_path("XDG_CACHE_HOME");
	if (!xdg.empty()) {
		return xdg / "voxel_browser";
	}
	std::filesystem::path home = env_path("HOME");
	return home / ".cache" / "voxel_browser";
#endif
}

} // namespace vb::core
