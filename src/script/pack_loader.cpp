#include "vb/script/pack_loader.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "vb/core/log.hpp"

namespace vb::script {

namespace {

std::vector<std::filesystem::path> sorted_lua_files(const std::filesystem::path &dir) {
	std::vector<std::filesystem::path> out;
	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec)) {
		return out;
	}
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
		if (entry.is_regular_file() && entry.path().extension() == ".lua") {
			out.push_back(entry.path());
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

// Returns false only on a real (non-kDisabled) load failure.
bool load_one(PackRuntime &rt, const std::filesystem::path &pack_root,
		const std::filesystem::path &file, bool &scripting_disabled_logged) {
	std::ifstream in(file, std::ios::binary);
	if (!in) {
		VB_WARN("script", "content pack: could not open '", file.string(), "'");
		return true; // missing/unreadable file: not fatal, just skipped
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	const std::string chunk_name =
			std::filesystem::relative(file, pack_root).generic_string();
	const ScriptResult result = rt.load_pack_file(ss.str(), chunk_name);
	if (result.ok) {
		return true;
	}
	if (result.error == core::ScriptError::kDisabled) {
		if (!scripting_disabled_logged) {
			VB_INFO("script", "content pack: scripting disabled (built without "
							  "VB_WITH_LUA) -- running with built-in defaults only");
			scripting_disabled_logged = true;
		}
		return true;
	}
	VB_ERROR("script", "content pack: '", chunk_name, "' failed to load: ",
			result.message);
	return false;
}

} // namespace

bool load_content_pack(PackRuntime &rt, const std::filesystem::path &content_pack) {
	bool scripting_disabled_logged = false;

	for (const char *sub : { "blocks", "entities", "biomes" }) {
		for (const auto &file : sorted_lua_files(content_pack / sub)) {
			if (!load_one(rt, content_pack, file, scripting_disabled_logged)) {
				return false;
			}
		}
	}

	const std::filesystem::path entry = content_pack / "init.lua";
	std::error_code ec;
	if (std::filesystem::is_regular_file(entry, ec)) {
		if (!load_one(rt, content_pack, entry, scripting_disabled_logged)) {
			return false;
		}
	}

	return true;
}

} // namespace vb::script
