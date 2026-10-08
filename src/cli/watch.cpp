#include "vb/cli/watch.hpp"

#include <system_error>

#include "vb/core/pack_layout.hpp"

namespace vb::cli {

namespace fs = std::filesystem;

namespace {

bool ignored(const fs::path &relative) {
	if (core::is_pack_runtime_state(relative)) {
		return true;
	}
	for (const fs::path &part : relative) {
		const std::string s = part.string();
		if (!s.empty() && s[0] == '.') {
			return true;
		}
	}
	return false;
}

} // namespace

PackSnapshot snapshot_pack(const fs::path &dir) {
	PackSnapshot out;
	std::error_code ec;
	for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
			!ec && it != end; it.increment(ec)) {
		std::error_code fec;
		if (!it->is_regular_file(fec)) {
			continue;
		}
		const fs::path rel = it->path().lexically_relative(dir);
		if (rel.empty() || ignored(rel)) {
			continue;
		}
		FileStamp stamp;
		stamp.size = it->file_size(fec);
		stamp.mtime = static_cast<std::int64_t>(it->last_write_time(fec).time_since_epoch().count());
		if (!fec) {
			out[rel.generic_string()] = stamp;
		}
	}
	return out;
}

std::string describe_changes(const PackSnapshot &before, const PackSnapshot &after) {
	std::string out;
	const auto note = [&](const char *what, const std::string &path) {
		out += (out.empty() ? "" : ", ") + std::string(what) + " " + path;
	};
	for (const auto &[path, stamp] : after) {
		const auto it = before.find(path);
		if (it == before.end()) {
			note("added", path);
		} else if (!(it->second == stamp)) {
			note("changed", path);
		}
	}
	for (const auto &[path, stamp] : before) {
		if (after.find(path) == after.end()) {
			note("removed", path);
		}
	}
	return out;
}

} // namespace vb::cli
