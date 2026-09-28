#include "vb/world/region_store.hpp"

#include <fstream>

#include "vb/core/log.hpp"
#include "vb/core/math.hpp"
#include "vb/protocol/byte_buffer.hpp"
#include "vb/protocol/compression.hpp"
#include "vb/world/chunk_codec.hpp"

namespace vb::world {

namespace {

// Chunks per region file, per axis (X/Z only -- see region_store.hpp). Picked
// as a round number comfortably larger than any shipped default view
// distance; not a format constant, just how finely files are split.
constexpr std::int32_t kRegionChunks = 16;

constexpr std::uint32_t kMagic = 0x47524256u; // "VBRG" little-endian

// Version 1: no per-entry flags byte, payload is always the raw chunk_codec
// blob. Version 2 (current writer) adds a per-entry u8 flags byte right after
// the revision -- bit 0 (kEntryCompressed) says the following payload bytes
// are LZ4-framed (protocol::compress_lz4's own u32-size-prefixed format) and
// need protocol::decompress_lz4() before reaching chunk_codec. Both versions
// are readable; only version 2 is ever written, so an existing world's
// region files upgrade in place the first time an edited chunk in them is
// re-saved (an untouched region file is never rewritten at all).
constexpr std::uint32_t kVersionNoFlags = 1;
constexpr std::uint32_t kVersion = 2;
constexpr std::uint8_t kEntryCompressed = 1u << 0;

// Same policy as net::kCompressionThresholdBytes (inc/vb/net/handshake.hpp):
// below this, LZ4's own fixed overhead (block format + compress_lz4's 4-byte
// original-size prefix) tends to not pay for itself. Kept as this module's
// own constant rather than a cross-module include -- world/ has no business
// depending on net/ just for one size constant.
constexpr std::size_t kCompressionThresholdBytes = 128;

// Compresses `payload` for on-disk storage if VB_WITH_COMPRESSION is built in,
// it's large enough to bother, and doing so actually shrinks it. Returns the
// bytes to write plus whether kEntryCompressed should be set for them.
std::pair<std::vector<std::byte>, bool> compress_for_disk(
		const std::vector<std::byte> &payload) {
#if VB_WITH_COMPRESSION
	if (payload.size() >= kCompressionThresholdBytes) {
		std::vector<std::byte> compressed = protocol::compress_lz4(payload);
		if (compressed.size() < payload.size()) {
			return { std::move(compressed), true };
		}
	}
#endif
	return { payload, false };
}

// ifstream/istreambuf_iterator<char> can't fill a vector<std::byte> directly
// (no implicit char -> std::byte conversion) -- read via a known size
// instead. Same idiom as assetsync/cache.cpp's own read_whole_file(), kept as
// a separate local copy rather than a cross-module dependency.
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

} // namespace

RegionStore::RegionStore(std::filesystem::path world_dir) : world_dir_(std::move(world_dir)) {}

std::int64_t RegionStore::region_key(core::ChunkCoord coord) {
	const std::int32_t rx = core::floor_div(coord.x, kRegionChunks);
	const std::int32_t rz = core::floor_div(coord.z, kRegionChunks);
	return (static_cast<std::int64_t>(rx) << 32) |
			static_cast<std::uint32_t>(rz);
}

std::filesystem::path RegionStore::region_path(std::int64_t key) const {
	const auto rx = static_cast<std::int32_t>(key >> 32);
	const auto rz = static_cast<std::int32_t>(key & 0xFFFFFFFFu);
	return world_dir_ / ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".vbr");
}

RegionStore::Region &RegionStore::region_for(core::ChunkCoord coord) {
	const std::int64_t key = region_key(coord);
	auto it = regions_.find(key);
	if (it != regions_.end()) {
		return it->second;
	}

	Region region;
	const std::vector<std::byte> raw = read_whole_file(region_path(key));
	if (!raw.empty()) {
		protocol::ByteReader r(raw);
		const std::uint32_t magic = r.u32();
		const std::uint32_t version = r.u32();
		const std::uint64_t count = r.varint();
		bool entry_corrupt = false;
		if (!r.failed() && magic == kMagic &&
				(version == kVersion || version == kVersionNoFlags)) {
			for (std::uint64_t i = 0; i < count && !r.failed() && !entry_corrupt; ++i) {
				core::ChunkCoord c{};
				c.x = r.i32();
				c.y = r.i32();
				c.z = r.i32();
				const std::uint64_t revision = r.u64();
				const std::uint8_t flags = version == kVersion ? r.u8() : 0;
				const std::uint64_t len = r.varint();
				const auto raw_payload = r.bytes(static_cast<std::size_t>(len));
				if (r.failed()) {
					break;
				}
				std::vector<std::byte> payload(raw_payload.begin(), raw_payload.end());
				if ((flags & kEntryCompressed) != 0) {
#if VB_WITH_COMPRESSION
					auto decoded = protocol::decompress_lz4(payload);
					if (!decoded) {
						entry_corrupt = true;
						break;
					}
					payload = std::move(decoded.value());
#else
					VB_WARN("world", "region file ", region_path(key).string(),
							" holds an LZ4-compressed chunk but this binary was "
							"built without VB_WITH_COMPRESSION -- treating the "
							"region as empty");
					entry_corrupt = true;
					break;
#endif
				}
				region.entries[c] = Entry{ std::move(payload), revision };
			}
		}
		if (r.failed() || entry_corrupt || magic != kMagic ||
				(version != kVersion && version != kVersionNoFlags)) {
			VB_WARN("world", "region file ", region_path(key).string(),
					" is corrupt or unreadable -- treating it as empty (any "
					"chunks it held regenerate from worldgen instead)");
			region.entries.clear();
		}
	}

	return regions_.emplace(key, std::move(region)).first->second;
}

std::unique_ptr<Chunk> RegionStore::load(core::ChunkCoord coord) {
	Region &region = region_for(coord);
	auto it = region.entries.find(coord);
	if (it == region.entries.end()) {
		return nullptr;
	}
	auto chunk = std::make_unique<Chunk>(coord);
	if (!decode_chunk_payload(it->second.payload, *chunk)) {
		VB_WARN("world", "saved chunk (", coord.x, ",", coord.y, ",", coord.z,
				") failed to decode -- regenerating it instead");
		region.entries.erase(it);
		region.dirty = true;
		return nullptr;
	}
	chunk->set_revision(it->second.revision);
	return chunk;
}

void RegionStore::save_if_dirty(const Chunk &chunk) {
	if (chunk.revision() == 0) {
		return; // never edited -- worldgen would reproduce this exactly
	}
	Region &region = region_for(chunk.coord());
	auto it = region.entries.find(chunk.coord());
	if (it != region.entries.end() && it->second.revision == chunk.revision()) {
		return; // already cached/on-disk at this exact revision
	}
	region.entries[chunk.coord()] = Entry{ encode_chunk_payload(chunk), chunk.revision() };
	region.dirty = true;
}

void RegionStore::flush() {
	for (auto &[key, region] : regions_) {
		if (!region.dirty) {
			continue;
		}
		std::error_code ec;
		std::filesystem::create_directories(world_dir_, ec);

		std::vector<std::byte> out;
		protocol::ByteWriter w(out);
		w.u32(kMagic);
		w.u32(kVersion);
		w.varint(region.entries.size());
		for (const auto &[coord, entry] : region.entries) {
			w.i32(coord.x);
			w.i32(coord.y);
			w.i32(coord.z);
			w.u64(entry.revision);
			auto [disk_payload, compressed] = compress_for_disk(entry.payload);
			w.u8(compressed ? kEntryCompressed : 0);
			w.varint(disk_payload.size());
			w.bytes(disk_payload);
		}

		const auto path = region_path(key);
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f) {
			VB_WARN("world", "failed to open ", path.string(), " for writing -- ",
					region.entries.size(), " chunk(s) stay unsaved this flush");
			continue;
		}
		f.write(reinterpret_cast<const char *>(out.data()),
				static_cast<std::streamsize>(out.size()));
		region.dirty = false;
	}
}

} // namespace vb::world
