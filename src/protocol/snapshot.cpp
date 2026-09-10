#include "vb/protocol/snapshot.hpp"

#include "vb/protocol/byte_buffer.hpp"

namespace vb::protocol {

using core::Err;

namespace {

// A snapshot from an untrusted server: cap list lengths so a bad count can't
// make us allocate wildly.
inline constexpr std::uint64_t kMaxRecords = 65536;

void write_record(ByteWriter &w, const EntityRecord &r) {
	w.u32(static_cast<std::uint32_t>(r.net_id));
	w.u16(static_cast<std::uint16_t>(r.kind));
	w.f64(r.pos.x);
	w.f64(r.pos.y);
	w.f64(r.pos.z);
	w.f32(r.rot.x);
	w.f32(r.rot.y);
	w.f32(r.vel.x);
	w.f32(r.vel.y);
	w.f32(r.vel.z);
	w.u8(r.flags);
}

EntityRecord read_record(ByteReader &r) {
	EntityRecord out;
	out.net_id = static_cast<core::NetId>(r.u32());
	out.kind = static_cast<core::EntityKindId>(r.u16());
	out.pos.x = r.f64();
	out.pos.y = r.f64();
	out.pos.z = r.f64();
	out.rot.x = r.f32();
	out.rot.y = r.f32();
	out.vel.x = r.f32();
	out.vel.y = r.f32();
	out.vel.z = r.f32();
	out.flags = r.u8();
	return out;
}

} // namespace

void S2CEntitySnapshot::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.u32(server_tick);
	w.u32(last_acked_input_seq);
	w.varint(entered.size());
	for (const auto &r : entered) {
		write_record(w, r);
	}
	w.varint(updated.size());
	for (const auto &r : updated) {
		write_record(w, r);
	}
	w.varint(removed.size());
	for (core::NetId id : removed) {
		w.u32(static_cast<std::uint32_t>(id));
	}
	w.boolean(has_local);
	if (has_local) {
		write_record(w, local);
	}
}

Decoded<S2CEntitySnapshot> S2CEntitySnapshot::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CEntitySnapshot m;
	m.server_tick = r.u32();
	m.last_acked_input_seq = r.u32();

	const std::uint64_t n_entered = r.varint();
	if (n_entered > kMaxRecords) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	m.entered.reserve(static_cast<std::size_t>(n_entered));
	for (std::uint64_t i = 0; i < n_entered && !r.failed(); ++i) {
		m.entered.push_back(read_record(r));
	}

	const std::uint64_t n_updated = r.varint();
	if (n_updated > kMaxRecords) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	m.updated.reserve(static_cast<std::size_t>(n_updated));
	for (std::uint64_t i = 0; i < n_updated && !r.failed(); ++i) {
		m.updated.push_back(read_record(r));
	}

	const std::uint64_t n_removed = r.varint();
	if (n_removed > kMaxRecords) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	m.removed.reserve(static_cast<std::size_t>(n_removed));
	for (std::uint64_t i = 0; i < n_removed && !r.failed(); ++i) {
		m.removed.push_back(static_cast<core::NetId>(r.u32()));
	}

	m.has_local = r.boolean();
	if (m.has_local && !r.failed()) {
		m.local = read_record(r);
	}

	r.expect_consumed();
	if (r.failed()) {
		return Err{ r.error() };
	}
	return m;
}

} // namespace vb::protocol
