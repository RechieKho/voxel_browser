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

std::filesystem::path vb_home() { return env_path("VB_HOME"); }

} // namespace

std::filesystem::path user_data_dir() {
	if (const auto home = vb_home(); !home.empty()) {
		return home;
	}
#if defined(_WIN32)
	std::filesystem::path base = env_path("LOCALAPPDATA");
	if (base.empty()) {
		base = env_path("TEMP");
	}
	return base / "voxel_browser";
#elif defined(__APPLE__)
	return env_path("HOME") / "Library" / "Application Support" / "voxel_browser";
#else
	std::filesystem::path xdg = env_path("XDG_DATA_HOME");
	if (!xdg.empty()) {
		return xdg / "voxel_browser";
	}
	return env_path("HOME") / ".local" / "share" / "voxel_browser";
#endif
}

std::filesystem::path user_config_dir() {
	if (const auto home = vb_home(); !home.empty()) {
		return home / "config";
	}
#if defined(_WIN32)
	std::filesystem::path base = env_path("APPDATA");
	if (base.empty()) {
		base = env_path("TEMP");
	}
	return base / "voxel_browser";
#elif defined(__APPLE__)
	return env_path("HOME") / "Library" / "Application Support" / "voxel_browser" / "config";
#else
	std::filesystem::path xdg = env_path("XDG_CONFIG_HOME");
	if (!xdg.empty()) {
		return xdg / "voxel_browser";
	}
	return env_path("HOME") / ".config" / "voxel_browser";
#endif
}

std::filesystem::path user_cache_dir() {
	if (const auto home = vb_home(); !home.empty()) {
		return home / "cache";
	}
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
