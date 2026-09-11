#include "vb/protocol/world.hpp"

#include "vb/protocol/byte_buffer.hpp"

namespace vb::protocol {

using core::Err;

namespace {

inline constexpr std::uint64_t kMaxChunkBytes = 8u * 1024u * 1024u;

constexpr std::uint64_t chunk_volume() {
	return static_cast<std::uint64_t>(core::kChunkDim) *
			static_cast<std::uint64_t>(core::kChunkDim) *
			static_cast<std::uint64_t>(core::kChunkDim);
}

void write_coord(ByteWriter &w, core::ChunkCoord c) {
	w.i32(c.x);
	w.i32(c.y);
	w.i32(c.z);
}

core::ChunkCoord read_coord(ByteReader &r) {
	core::ChunkCoord c;
	c.x = r.i32();
	c.y = r.i32();
	c.z = r.i32();
	return c;
}

template <typename T>
Decoded<T> finish(ByteReader &r, T value) {
	r.expect_consumed();
	if (r.failed()) {
		return Err{ r.error() };
	}
	return value;
}

} // namespace

// --- S2CChunkAdd ----------------------------------------------------------
void S2CChunkAdd::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	write_coord(w, coord);
	w.u64(revision);
	w.varint(payload.size());
	w.bytes({ payload.data(), payload.size() });
}

Decoded<S2CChunkAdd> S2CChunkAdd::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CChunkAdd m;
	m.coord = read_coord(r);
	m.revision = r.u64();
	const std::uint64_t len = r.varint();
	if (len > kMaxChunkBytes) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	const auto bytes = r.bytes(static_cast<std::size_t>(len));
	if (!r.failed()) {
		m.payload.assign(bytes.begin(), bytes.end());
	}
	return finish(r, std::move(m));
}

// --- S2CChunkDelta ------------------------------------------------------
void S2CChunkDelta::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	write_coord(w, coord);
	w.u64(base_revision);
	w.u64(new_revision);
	w.varint(blocks.size());
	for (const BlockChange &c : blocks) {
		w.varint(c.local_index);
		w.u16(static_cast<std::uint16_t>(c.block));
	}
	w.varint(light.size());
	for (const LightChange &c : light) {
		w.varint(c.local_index);
		w.u8(c.packed);
	}
}

Decoded<S2CChunkDelta> S2CChunkDelta::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CChunkDelta m;
	m.coord = read_coord(r);
	m.base_revision = r.u64();
	m.new_revision = r.u64();

	const std::uint64_t nblocks = r.varint();
	if (nblocks > chunk_volume()) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	for (std::uint64_t i = 0; i < nblocks && !r.failed(); ++i) {
		BlockChange c;
		c.local_index = static_cast<std::uint32_t>(r.varint());
		c.block = static_cast<core::BlockId>(r.u16());
		if (c.local_index >= chunk_volume()) {
			return Err{ core::ProtocolError::kMalformed };
		}
		m.blocks.push_back(c);
	}

	const std::uint64_t nlight = r.varint();
	if (nlight > chunk_volume()) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	for (std::uint64_t i = 0; i < nlight && !r.failed(); ++i) {
		LightChange c;
		c.local_index = static_cast<std::uint32_t>(r.varint());
		c.packed = r.u8();
		if (c.local_index >= chunk_volume()) {
			return Err{ core::ProtocolError::kMalformed };
		}
		m.light.push_back(c);
	}

	return finish(r, std::move(m));
}

// --- S2CChunkRemove --------------------------------------------------
void S2CChunkRemove::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	write_coord(w, coord);
}

Decoded<S2CChunkRemove> S2CChunkRemove::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CChunkRemove m;
	m.coord = read_coord(r);
	return finish(r, std::move(m));
}

namespace {

void write_ivec3(ByteWriter &w, core::IVec3 v) {
	w.svarint(v.x);
	w.svarint(v.y);
	w.svarint(v.z);
}

core::IVec3 read_ivec3(ByteReader &r) {
	core::IVec3 v;
	v.x = static_cast<std::int32_t>(r.svarint());
	v.y = static_cast<std::int32_t>(r.svarint());
	v.z = static_cast<std::int32_t>(r.svarint());
	return v;
}

} // namespace

// --- C2SBlockEdit --------------------------------------------------------
void C2SBlockEdit::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.u32(predicted_seq);
	w.u8(static_cast<std::uint8_t>(action));
	write_ivec3(w, pos);
	w.u16(static_cast<std::uint16_t>(block));
}

Decoded<C2SBlockEdit> C2SBlockEdit::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	C2SBlockEdit m;
	m.predicted_seq = r.u32();
	const auto action = static_cast<BlockEditAction>(r.u8());
	if (!valid(action)) {
		return Err{ core::ProtocolError::kBadEnum };
	}
	m.action = action;
	m.pos = read_ivec3(r);
	m.block = static_cast<core::BlockId>(r.u16());
	return finish(r, std::move(m));
}

// --- S2CBlockEditResult -------------------------------------------------
void S2CBlockEditResult::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.u32(predicted_seq);
	w.boolean(accepted);
	write_ivec3(w, pos);
}

Decoded<S2CBlockEditResult> S2CBlockEditResult::decode(
		std::span<const std::byte> in) {
	ByteReader r(in);
	S2CBlockEditResult m;
	m.predicted_seq = r.u32();
	m.accepted = r.boolean();
	m.pos = read_ivec3(r);
	return finish(r, std::move(m));
}

} // namespace vb::protocol
