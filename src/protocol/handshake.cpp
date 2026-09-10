#include "vb/protocol/handshake.hpp"

namespace vb::protocol {

using core::Err;
using core::ProtocolError;

namespace {

template <typename T>
Decoded<T> finish(ByteReader &r, T value) {
	r.expect_consumed();
	if (r.failed()) {
		return Err{ r.error() };
	}
	return value;
}

} // namespace

// --- C2SHello ---------------------------------------------------------------
void C2SHello::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.u16(engine_protocol_version);
	w.u64(client_nonce);
	w.string(client_version);
}

Decoded<C2SHello> C2SHello::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	C2SHello m;
	m.engine_protocol_version = r.u16();
	m.client_nonce = r.u64();
	m.client_version = r.string();
	return finish(r, std::move(m));
}

// --- S2CServerInfo ---------------------------------------------------------
void S2CServerInfo::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.string(pack_name);
	w.string(pack_version);
	w.u16(engine_protocol_version);
	w.u16(tick_rate);
	w.string(motd);
	w.u8(static_cast<std::uint8_t>(auth_mode));
}

Decoded<S2CServerInfo> S2CServerInfo::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CServerInfo m;
	m.pack_name = r.string();
	m.pack_version = r.string();
	m.engine_protocol_version = r.u16();
	m.tick_rate = r.u16();
	m.motd = r.string();
	m.auth_mode = static_cast<AuthMode>(r.u8());
	if (!r.failed() && !valid(m.auth_mode)) {
		r.fail(ProtocolError::kBadEnum);
	}
	return finish(r, std::move(m));
}

// --- C2SAuth -------------------------------------------------------------
void C2SAuth::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.string(player_name);
	w.string(token);
}

Decoded<C2SAuth> C2SAuth::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	C2SAuth m;
	m.player_name = r.string();
	m.token = r.string();
	return finish(r, std::move(m));
}

// --- S2CAuthResult -----------------------------------------------------
void S2CAuthResult::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.boolean(ok);
	w.string(reason);
}

Decoded<S2CAuthResult> S2CAuthResult::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CAuthResult m;
	m.ok = r.boolean();
	m.reason = r.string();
	return finish(r, std::move(m));
}

// --- C2SReady ---------------------------------------------------------
void C2SReady::encode(std::vector<std::byte> &out) const { (void)out; }

Decoded<C2SReady> C2SReady::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	return finish(r, C2SReady{});
}

// --- S2CJoinAccept ---------------------------------------------------
void S2CJoinAccept::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.u32(static_cast<std::uint32_t>(your_net_id));
	w.f64(spawn_pos.x);
	w.f64(spawn_pos.y);
	w.f64(spawn_pos.z);
	w.u64(world_seed);
	w.u32(time_of_day);
}

Decoded<S2CJoinAccept> S2CJoinAccept::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CJoinAccept m;
	m.your_net_id = static_cast<core::NetId>(r.u32());
	m.spawn_pos.x = r.f64();
	m.spawn_pos.y = r.f64();
	m.spawn_pos.z = r.f64();
	m.world_seed = r.u64();
	m.time_of_day = r.u32();
	return finish(r, std::move(m));
}

// --- S2CDisconnect -------------------------------------------------
void S2CDisconnect::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.u8(static_cast<std::uint8_t>(reason));
	w.string(message);
}

Decoded<S2CDisconnect> S2CDisconnect::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CDisconnect m;
	m.reason = static_cast<DisconnectReason>(r.u8());
	m.message = r.string();
	if (!r.failed() && !valid(m.reason)) {
		r.fail(ProtocolError::kBadEnum);
	}
	return finish(r, std::move(m));
}

} // namespace vb::protocol
