#include <doctest/doctest.h>

#include <ostream>

#include <filesystem>
#include <string>
#include <vector>

#include "vb/assetsync/cache.hpp"
#include "vb/assetsync/manifest.hpp"

#if VB_WITH_COMPRESSION

using vb::assetsync::ClientAssetCache;
using vb::core::AssetHash;
using vb::protocol::AssetEntryRecord;
using vb::protocol::AssetKind;
using vb::protocol::S2CAssetData;

namespace {

std::filesystem::path temp_cache_dir(const char *name) {
	auto p = std::filesystem::temp_directory_path() /
			(std::string("vb_asset_cache_test_") + name);
	std::filesystem::remove_all(p);
	return p;
}

std::vector<std::byte> bytes_of(std::string_view s) {
	return { reinterpret_cast<const std::byte *>(s.data()),
		reinterpret_cast<const std::byte *>(s.data()) + s.size() };
}

// Feeds one small "file" through ingest_chunk in a single chunk.
bool ingest_whole_file(ClientAssetCache &cache, AssetHash hash,
		std::string_view content) {
	S2CAssetData d;
	d.hash = hash;
	d.seq = 0;
	d.total_chunks = 1;
	d.bytes = bytes_of(content);
	return cache.ingest_chunk(d);
}

AssetEntryRecord entry_for(std::string path, AssetHash hash, std::uint64_t size) {
	return { std::move(path), hash, size, AssetKind::kData };
}

} // namespace

TEST_CASE("compute_missing reports only hashes not already cached") {
	const auto dir = temp_cache_dir("missing");
	ClientAssetCache cache(dir, 1024ull * 1024ull);

	const AssetHash cached_hash = vb::assetsync::hash_bytes(bytes_of("cached"));
	cache.compute_missing({ entry_for("cached.txt", cached_hash, 6) });
	REQUIRE(ingest_whole_file(cache, cached_hash, "cached"));

	const AssetHash new_a{ 1, 1 };
	const AssetHash new_b{ 2, 2 };
	std::vector<AssetEntryRecord> entries{
		entry_for("cached.txt", cached_hash, 6),
		entry_for("a.txt", new_a, 10),
		entry_for("b.txt", new_b, 10),
	};

	const auto missing = cache.compute_missing(entries);
	REQUIRE(missing.size() == 2);
	CHECK(((missing[0] == new_a && missing[1] == new_b) ||
			(missing[0] == new_b && missing[1] == new_a)));
	// The already-cached entry should already be in the virtual FS.
	CHECK(cache.virtual_fs().count("cached.txt") == 1);

	std::filesystem::remove_all(dir);
}

TEST_CASE("ingest_chunk rejects a completed file that fails hash verification") {
	const auto dir = temp_cache_dir("corrupt");
	ClientAssetCache cache(dir, 1024ull * 1024ull);

	const AssetHash claimed = vb::assetsync::hash_bytes(bytes_of("expected"));
	std::vector<AssetEntryRecord> entries{ entry_for("f.bin", claimed, 8) };
	cache.compute_missing(entries);

	S2CAssetData d;
	d.hash = claimed;
	d.seq = 0;
	d.total_chunks = 1;
	d.bytes = bytes_of("corrupt!"); // wrong content, same claimed hash
	CHECK_FALSE(cache.ingest_chunk(d));
	CHECK(cache.virtual_fs().empty());

	// Nothing should have been committed to disk.
	bool any_file = false;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
		if (entry.is_regular_file() && entry.path().filename() != "index.bin") {
			any_file = true;
		}
	}
	CHECK_FALSE(any_file);

	std::filesystem::remove_all(dir);
}

TEST_CASE("LRU eviction keeps the cache under its byte cap") {
	const auto dir = temp_cache_dir("lru");
	// Cap only large enough for one ~10-byte file at a time.
	ClientAssetCache cache(dir, 12ull);

	const AssetHash h1 = vb::assetsync::hash_bytes(bytes_of("0123456789"));
	const AssetHash h2 = vb::assetsync::hash_bytes(bytes_of("abcdefghij"));
	cache.compute_missing({ entry_for("first.bin", h1, 10), entry_for("second.bin", h2, 10) });

	REQUIRE(ingest_whole_file(cache, h1, "0123456789"));
	REQUIRE(ingest_whole_file(cache, h2, "abcdefghij"));

	// Re-open a fresh cache instance pointed at the same directory and ask
	// what's still missing -- the first file should have been evicted to
	// make room for the second.
	ClientAssetCache reopened(dir, 12ull);
	const auto missing = reopened.compute_missing(
			{ entry_for("first.bin", h1, 10), entry_for("second.bin", h2, 10) });
	REQUIRE(missing.size() == 1);
	CHECK(missing[0] == h1);

	std::filesystem::remove_all(dir);
}

#endif // VB_WITH_COMPRESSION
