#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/protocol/assetsync.hpp"

// Client-side content-addressed asset cache (spec §9.2, Phase 4.4). Backs
// the HandshakeClientHost hooks ClientSession wires into ClientHandshake:
// decides what's missing from a manifest, ingests + verifies streamed
// chunks, and assembles the in-memory virtual pack filesystem. Shared across
// every server this client ever connects to -- identical files download
// once, ever.

namespace vb::assetsync {

class ClientAssetCache {
public:
	// `cache_root` e.g. vb::core::user_cache_dir() / "assets"; `cap_bytes`
	// from ClientConfig::asset_cache_mb. Creates `cache_root` if needed and
	// loads any existing on-disk index.
	ClientAssetCache(std::filesystem::path cache_root, std::uint64_t cap_bytes);

	// Reconnect fast path (in-session only this phase -- not persisted
	// across process restarts; the on-disk CAS already makes a restarted
	// process's *transfer* a no-op regardless, see docs/lua-api.md-style
	// note in REMAINING_TASKS.md).
	core::AssetHash last_known_manifest_hash_for(std::string_view server_key) const;
	void remember_manifest_hash(std::string_view server_key, core::AssetHash hash);

	// Starts a new transfer cycle: returns the hashes from `entries` not
	// already present in the cache, and immediately reads every
	// already-cached entry into the virtual FS (touching its LRU
	// last-used time). Resets ingest/all_received bookkeeping.
	std::vector<core::AssetHash> compute_missing(
			const std::vector<protocol::AssetEntryRecord> &entries);

	// Appends one streamed chunk. Returns false on a protocol violation
	// (unexpected hash/seq) or a completed file failing hash verification --
	// spec §9.4 says either aborts the connection. Evicts LRU entries as
	// needed to stay under the cap before committing a newly-completed file
	// to disk.
	bool ingest_chunk(const protocol::S2CAssetData &chunk);

	// True once every hash returned by the last compute_missing() has been
	// fully received and committed (or there were none).
	bool all_received() const;

	// path -> bytes, assembled from cache reads (already-cached entries) and
	// completed transfers. Nothing consumes this yet (Lua require / texture
	// loader land in later phases).
	const std::unordered_map<std::string, std::vector<std::byte>> &virtual_fs() const {
		return virtual_fs_;
	}

private:
	struct IndexEntry {
		std::uint64_t size = 0;
		std::int64_t last_used_unix = 0;
	};
	struct PendingFile {
		std::string path;
		std::uint64_t expected_size = 0;
		std::vector<std::byte> buffer;
		std::uint32_t chunks_received = 0;
		std::uint32_t total_chunks = 0;
		bool done = false;
	};

	std::filesystem::path shard_dir(core::AssetHash h) const;
	std::filesystem::path file_path(core::AssetHash h) const;
	void load_index();
	void save_index() const;
	void touch(core::AssetHash h);
	void evict_until_fits(std::uint64_t incoming_size);
	std::uint64_t total_cached_bytes() const;
	bool read_cached_file(core::AssetHash h, std::vector<std::byte> &out) const;
	void commit_file(core::AssetHash h, const std::vector<std::byte> &bytes);

	std::filesystem::path cache_root_;
	std::uint64_t cap_bytes_;
	std::unordered_map<core::AssetHash, IndexEntry> index_;
	std::unordered_map<std::string, core::AssetHash> last_manifest_hash_;
	std::unordered_map<core::AssetHash, PendingFile> pending_;
	std::unordered_map<std::string, std::vector<std::byte>> virtual_fs_;
};

} // namespace vb::assetsync
