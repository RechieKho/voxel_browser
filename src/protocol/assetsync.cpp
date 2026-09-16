#include "vb/protocol/assetsync.hpp"

#include "vb/protocol/byte_buffer.hpp"

namespace vb::protocol {

using core::Err;

namespace {

inline constexpr std::uint64_t kMaxManifestEntries = 65536u;
inline constexpr std::uint64_t kMaxRequestedHashes = 65536u;
inline constexpr std::uint64_t kMaxAssetChunkBytes = 1u * 1024u * 1024u;

template <typename T>
Decoded<T> finish(ByteReader &r, T value) {
	r.expect_consumed();
	if (r.failed()) {
		return Err{ r.error() };
	}
	return value;
}

void write_hash(ByteWriter &w, core::AssetHash h) {
	w.u64(h.lo);
	w.u64(h.hi);
}

core::AssetHash read_hash(ByteReader &r) {
	core::AssetHash h;
	h.lo = r.u64();
	h.hi = r.u64();
	return h;
}

} // namespace

void C2SAssetManifestRequest::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	write_hash(w, known_manifest_hash);
}

Decoded<C2SAssetManifestRequest> C2SAssetManifestRequest::decode(
		std::span<const std::byte> in) {
	ByteReader r(in);
	C2SAssetManifestRequest m;
	m.known_manifest_hash = read_hash(r);
	return finish(r, std::move(m));
}

void S2CAssetManifest::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	write_hash(w, manifest_hash);
	w.u64(total_bytes);
	w.varint(entries.size());
	for (const auto &e : entries) {
		w.string(e.path);
		write_hash(w, e.hash);
		w.u64(e.size);
		w.u8(static_cast<std::uint8_t>(e.kind));
	}
}

Decoded<S2CAssetManifest> S2CAssetManifest::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CAssetManifest m;
	m.manifest_hash = read_hash(r);
	m.total_bytes = r.u64();
	const std::uint64_t n = r.varint();
	if (n > kMaxManifestEntries) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	m.entries.reserve(static_cast<std::size_t>(n));
	for (std::uint64_t i = 0; i < n && !r.failed(); ++i) {
		AssetEntryRecord e;
		e.path = r.string();
		e.hash = read_hash(r);
		e.size = r.u64();
		e.kind = static_cast<AssetKind>(r.u8());
		if (!r.failed() && !valid(e.kind)) {
			r.fail(core::ProtocolError::kBadEnum);
		}
		m.entries.push_back(std::move(e));
	}
	return finish(r, std::move(m));
}

void C2SAssetRequest::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.varint(missing.size());
	for (const auto &h : missing) {
		write_hash(w, h);
	}
}

Decoded<C2SAssetRequest> C2SAssetRequest::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	C2SAssetRequest m;
	const std::uint64_t n = r.varint();
	if (n > kMaxRequestedHashes) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	m.missing.reserve(static_cast<std::size_t>(n));
	for (std::uint64_t i = 0; i < n && !r.failed(); ++i) {
		m.missing.push_back(read_hash(r));
	}
	return finish(r, std::move(m));
}

void S2CAssetData::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	write_hash(w, hash);
	w.u32(seq);
	w.u32(total_chunks);
	w.varint(bytes.size());
	w.bytes({ bytes.data(), bytes.size() });
}

Decoded<S2CAssetData> S2CAssetData::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CAssetData m;
	m.hash = read_hash(r);
	m.seq = r.u32();
	m.total_chunks = r.u32();
	const std::uint64_t len = r.varint();
	if (len > kMaxAssetChunkBytes) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	const auto b = r.bytes(static_cast<std::size_t>(len));
	if (!r.failed()) {
		m.bytes.assign(b.begin(), b.end());
	}
	return finish(r, std::move(m));
}

} // namespace vb::protocol
