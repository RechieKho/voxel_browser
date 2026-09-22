#include "vb/assetsync/cache.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <system_error>

#include "vb/assetsync/manifest.hpp"
#include "vb/core/log.hpp"
#include "vb/protocol/byte_buffer.hpp"

namespace vb::assetsync {

namespace {

// ifstream/istreambuf_iterator<char> can't fill a vector<std::byte> directly
// (no implicit char -> std::byte conversion); read via a known size instead.
std::vector<std::byte> read_whole_file(const std::filesystem::path &p) {
	std::ifstream f(p, std::ios::binary | std::ios::ate);
	if (!f) {
		return {};
	}
	const auto size = f.tellg();
	f.seekg(0);
	std::vector<std::byte> out(static_cast<std::size_t>(size));
	if (size > 0) {
		f.read(reinterpret_cast<char *>(out.data()), size);
	}
	return out;
}

// Client-side defense-in-depth ceiling against a hostile/buggy server.
// ClientConfig has no per-file field (only a total asset_cache_mb cap), so
// this mirrors ServerConfig::asset_max_file_mb's default rather than adding
// a new config surface for this phase.
constexpr std::uint64_t kMaxAssetFileBytes = 32u * 1024u * 1024u;

std::int64_t now_unix() {
	return std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count();
}

std::string hex(std::uint64_t v) {
	static const char *digits = "0123456789abcdef";
	std::string out(16, '0');
	for (int i = 15; i >= 0; --i) {
		out[static_cast<std::size_t>(i)] = digits[v & 0xF];
		v >>= 4;
	}
	return out;
}

std::string hash_hex(core::AssetHash h) { return hex(h.hi) + hex(h.lo); }

} // namespace

ClientAssetCache::ClientAssetCache(std::filesystem::path cache_root,
		std::uint64_t cap_bytes) : cache_root_(std::move(cache_root)),
								   cap_bytes_(cap_bytes) {
	std::error_code ec;
	std::filesystem::create_directories(cache_root_, ec);
	load_index();
}

std::filesystem::path ClientAssetCache::shard_dir(core::AssetHash h) const {
	return cache_root_ / hex(h.hi).substr(0, 2);
}

std::filesystem::path ClientAssetCache::file_path(core::AssetHash h) const {
	return shard_dir(h) / hash_hex(h);
}

void ClientAssetCache::load_index() {
	const std::vector<std::byte> raw = read_whole_file(cache_root_ / "index.bin");
	if (raw.empty()) {
		return;
	}
	protocol::ByteReader r({ raw.data(), raw.size() });
	const std::uint64_t n = r.varint();
	for (std::uint64_t i = 0; i < n && !r.failed(); ++i) {
		core::AssetHash h;
		h.hi = r.u64();
		h.lo = r.u64();
		IndexEntry e;
		e.size = r.u64();
		e.last_used_unix = r.i64();
		if (!r.failed()) {
			index_[h] = e;
		}
	}
}

void ClientAssetCache::save_index() const {
	std::vector<std::byte> out;
	protocol::ByteWriter w(out);
	w.varint(index_.size());
	for (const auto &[h, e] : index_) {
		w.u64(h.hi);
		w.u64(h.lo);
		w.u64(e.size);
		w.i64(e.last_used_unix);
	}
	const std::filesystem::path tmp = cache_root_ / "index.bin.tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		f.write(reinterpret_cast<const char *>(out.data()),
				static_cast<std::streamsize>(out.size()));
	}
	std::error_code ec;
	std::filesystem::rename(tmp, cache_root_ / "index.bin", ec);
	if (ec) {
		VB_WARN("assetsync", "failed to commit cache index: ", ec.message());
	}
}

void ClientAssetCache::touch(core::AssetHash h) {
	auto it = index_.find(h);
	if (it != index_.end()) {
		it->second.last_used_unix = now_unix();
	}
}

std::uint64_t ClientAssetCache::total_cached_bytes() const {
	std::uint64_t total = 0;
	for (const auto &[h, e] : index_) {
		(void)h;
		total += e.size;
	}
	return total;
}

void ClientAssetCache::evict_until_fits(std::uint64_t incoming_size) {
	if (incoming_size > cap_bytes_) {
		return; // can't ever fit; caller's caps check handles rejection
	}
	while (total_cached_bytes() + incoming_size > cap_bytes_ && !index_.empty()) {
		auto oldest = index_.begin();
		for (auto it = index_.begin(); it != index_.end(); ++it) {
			if (it->second.last_used_unix < oldest->second.last_used_unix) {
				oldest = it;
			}
		}
		std::error_code ec;
		std::filesystem::remove(file_path(oldest->first), ec);
		index_.erase(oldest);
	}
}

bool ClientAssetCache::read_cached_file(core::AssetHash h,
		std::vector<std::byte> &out) const {
	std::error_code ec;
	if (!std::filesystem::exists(file_path(h), ec)) {
		return false;
	}
	out = read_whole_file(file_path(h));
	return true;
}

void ClientAssetCache::commit_file(core::AssetHash h,
		const std::vector<std::byte> &bytes) {
	evict_until_fits(bytes.size());
	std::error_code ec;
	std::filesystem::create_directories(shard_dir(h), ec);
	std::ofstream f(file_path(h), std::ios::binary | std::ios::trunc);
	f.write(reinterpret_cast<const char *>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
	index_[h] = IndexEntry{ bytes.size(), now_unix() };
	save_index();
}

core::AssetHash ClientAssetCache::last_known_manifest_hash_for(
		std::string_view server_key) const {
	auto it = last_manifest_hash_.find(std::string(server_key));
	return it != last_manifest_hash_.end() ? it->second : core::AssetHash{};
}

void ClientAssetCache::remember_manifest_hash(std::string_view server_key,
		core::AssetHash hash) {
	last_manifest_hash_[std::string(server_key)] = hash;
}

std::vector<core::AssetHash> ClientAssetCache::compute_missing(
		const std::vector<protocol::AssetEntryRecord> &entries) {
	pending_.clear();
	virtual_fs_.clear();
	std::vector<core::AssetHash> missing;
	for (const auto &e : entries) {
		auto it = index_.find(e.hash);
		if (it == index_.end()) {
			missing.push_back(e.hash);
			pending_[e.hash] = PendingFile{ e.path, e.size, {}, 0, 0, false };
			continue;
		}
		touch(e.hash);
		std::vector<std::byte> bytes;
		if (read_cached_file(e.hash, bytes)) {
			virtual_fs_[e.path] = std::move(bytes);
		} else {
			// Index says we have it but the file is gone (manual deletion,
			// disk issue) -- re-request it rather than silently omitting it.
			index_.erase(it);
			missing.push_back(e.hash);
			pending_[e.hash] = PendingFile{ e.path, e.size, {}, 0, 0, false };
		}
	}
	save_index();
	return missing;
}

bool ClientAssetCache::ingest_chunk(const protocol::S2CAssetData &chunk) {
	auto it = pending_.find(chunk.hash);
	if (it == pending_.end() || it->second.done) {
		return false; // unrequested or already-completed hash: protocol violation
	}
	PendingFile &pf = it->second;
	if (chunk.seq != pf.chunks_received) {
		return false; // out-of-order chunk
	}
	if (pf.expected_size > kMaxAssetFileBytes) {
		return false; // server's own manifest already violated the cap
	}
	pf.buffer.insert(pf.buffer.end(), chunk.bytes.begin(), chunk.bytes.end());
	if (pf.buffer.size() > kMaxAssetFileBytes) {
		return false;
	}
	pf.total_chunks = chunk.total_chunks;
	++pf.chunks_received;
	if (pf.chunks_received < pf.total_chunks) {
		return true; // more chunks still expected
	}

	if (assetsync::hash_bytes({ pf.buffer.data(), pf.buffer.size() }) != chunk.hash) {
		pending_.erase(it); // nothing committed; caller should abort the connection
		return false;
	}

	commit_file(chunk.hash, pf.buffer);
	virtual_fs_[pf.path] = pf.buffer;
	pf.done = true;
	return true;
}

bool ClientAssetCache::all_received() const {
	for (const auto &[h, pf] : pending_) {
		(void)h;
		if (!pf.done) {
			return false;
		}
	}
	return true;
}

} // namespace vb::assetsync
