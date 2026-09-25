#include "vb/protocol/snapshot.hpp"

#include "vb/protocol/byte_buffer.hpp"

namespace vb::protocol {

using core::Err;

namespace {

// A snapshot from an untrusted server: cap list lengths so a bad count can't
// make us allocate wildly.
inline constexpr std::uint64_t kMaxRecords = 65536;
inline constexpr std::uint64_t kMaxVisualOverrideClips = 64u;

// Entity-management follow-up: EntityRecord::visual_override's own codec.
// Deliberately not shared with world.cpp's write_entity_visual/
// read_entity_visual (that one encodes a fully-specified EntityVisualDef for
// S2C_EntityKindRegistry; this one encodes a partial, all-optional
// EntityVisualOverride) -- no header currently exposes ByteWriter/ByteReader
// helpers across protocol .cpp files, same "no shared helper to import"
// posture as vb::world::RegionStore's own read_whole_file() (see STATE.md §4).
void write_visual_override(ByteWriter &w, const EntityVisualOverride &v) {
	w.boolean(v.texture.has_value());
	if (v.texture) {
		w.string(*v.texture);
	}
	const bool has_frame_size = v.frame_width.has_value() && v.frame_height.has_value();
	w.boolean(has_frame_size);
	if (has_frame_size) {
		w.u16(*v.frame_width);
		w.u16(*v.frame_height);
	}
	w.boolean(v.facings.has_value());
	if (v.facings) {
		w.u8(*v.facings);
	}
	const bool has_origin = v.origin_x.has_value() && v.origin_y.has_value();
	w.boolean(has_origin);
	if (has_origin) {
		w.f32(*v.origin_x);
		w.f32(*v.origin_y);
	}
	w.boolean(v.clips.has_value());
	if (v.clips) {
		w.varint(v.clips->size());
		for (const auto &c : *v.clips) {
			w.string(c.clip);
			w.u16(c.frames);
			w.f32(c.fps);
		}
	}
}

EntityVisualOverride read_visual_override(ByteReader &r) {
	EntityVisualOverride v;
	if (r.boolean()) {
		v.texture = r.string();
	}
	if (r.boolean()) {
		v.frame_width = r.u16();
		v.frame_height = r.u16();
	}
	if (r.boolean()) {
		v.facings = r.u8();
	}
	if (r.boolean()) {
		v.origin_x = r.f32();
		v.origin_y = r.f32();
	}
	if (r.boolean()) {
		const std::uint64_t n = r.varint();
		if (n > kMaxVisualOverrideClips) {
			r.fail(core::ProtocolError::kLengthExceeded);
			return v;
		}
		std::vector<EntityClipDef> clips;
		clips.reserve(static_cast<std::size_t>(n));
		for (std::uint64_t i = 0; i < n && !r.failed(); ++i) {
			EntityClipDef c;
			c.clip = r.string();
			c.frames = r.u16();
			c.fps = r.f32();
			clips.push_back(std::move(c));
		}
		v.clips = std::move(clips);
	}
	return v;
}

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
	w.boolean(r.visual_override.has_value());
	if (r.visual_override) {
		write_visual_override(w, *r.visual_override);
	}
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
	if (r.boolean()) {
		out.visual_override = read_visual_override(r);
	}
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
