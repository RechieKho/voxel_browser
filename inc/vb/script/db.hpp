#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Generic per-key persistent store backing vb.db (spec §10.6, Phase 6.4).
// Distinct from vb.storage's single pack-global JSON blob (PackRuntime::Impl
// ::storage): a vb.db key is whatever the script chooses ("user:" .. name,
// "session:" .. token, ...), so each one lives in its own file, content-
// addressed by sha256(key) under a 2-hex-prefix shard directory -- the same
// shape as vb::assetsync::ClientAssetCache's on-disk layout
// (src/assetsync/cache.cpp), reused here so an arbitrary/hostile key string
// never has to survive as a literal filename. There is no list/enumerate:
// spec only calls for get/set/delete by an already-known key.
//
// Single-threaded, no in-memory cache -- called only from PackRuntime on the
// server's main thread, same as everything else in vb::script. A later swap
// to a real embedded database (SQLite is the leading candidate per
// REMAINING_TASKS.md 6.4) is a pure implementation-detail change behind this
// same interface.

namespace vb::script {

class ScriptDb {
public:
	explicit ScriptDb(std::filesystem::path root);

	std::optional<std::string> get(std::string_view key) const;
	void set(std::string_view key, std::string_view value);
	void erase(std::string_view key);

private:
	std::filesystem::path entry_path(std::string_view key) const;

	std::filesystem::path root_;
};

} // namespace vb::script
