#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

// `vb host --watch`: detecting that a content pack changed on disk.

namespace vb::cli {

struct FileStamp {
	std::uintmax_t size = 0;
	std::int64_t mtime = 0; // file_time_type ticks; only compared for equality
	friend bool operator==(const FileStamp &, const FileStamp &) = default;
};

using PackSnapshot = std::map<std::string, FileStamp>; // relative path -> stamp

// Every regular file under `dir`, except what a running pack writes itself:
// storage.json, db/ and anything dot-prefixed (editors' swap files, .git). A
// restart caused by the pack's own state files would loop forever.
PackSnapshot snapshot_pack(const std::filesystem::path &dir);

// Relative paths that were added, removed or modified between two snapshots
// (empty = identical), sorted.
std::string describe_changes(const PackSnapshot &before, const PackSnapshot &after);

} // namespace vb::cli
