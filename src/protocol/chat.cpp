#include "vb/protocol/chat.hpp"

#include "vb/protocol/byte_buffer.hpp"

namespace vb::protocol {

using core::Err;

namespace {

// Generous cap on S2CPlayerList entries, same defensive-decode posture as
// S2CBlockRegistry's kMaxBlockRegistryRecords.
inline constexpr std::uint64_t kMaxPlayerListEntries = 4096u;

template <typename T>
Decoded<T> finish(ByteReader &r, T value) {
	r.expect_consumed();
	if (r.failed()) {
		return Err{ r.error() };
	}
	return value;
}

} // namespace

void C2SChat::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.string(text);
}

Decoded<C2SChat> C2SChat::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	C2SChat m;
	m.text = r.string();
	return finish(r, std::move(m));
}

void S2CChat::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.string(text);
}

Decoded<S2CChat> S2CChat::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CChat m;
	m.text = r.string();
	return finish(r, std::move(m));
}

void S2COpenUi::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.string(ui_name);
	w.string(ctx_json);
}

Decoded<S2COpenUi> S2COpenUi::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2COpenUi m;
	m.ui_name = r.string();
	m.ctx_json = r.string();
	return finish(r, std::move(m));
}

void S2CPlayerJoin::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.u32(static_cast<std::uint32_t>(net_id));
	w.string(name);
}

Decoded<S2CPlayerJoin> S2CPlayerJoin::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CPlayerJoin m;
	m.net_id = static_cast<core::NetId>(r.u32());
	m.name = r.string();
	return finish(r, std::move(m));
}

void S2CPlayerLeave::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.u32(static_cast<std::uint32_t>(net_id));
}

Decoded<S2CPlayerLeave> S2CPlayerLeave::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CPlayerLeave m;
	m.net_id = static_cast<core::NetId>(r.u32());
	return finish(r, std::move(m));
}

void S2CPlayerList::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.varint(players.size());
	for (const auto &p : players) {
		w.u32(static_cast<std::uint32_t>(p.net_id));
		w.string(p.name);
	}
}

Decoded<S2CPlayerList> S2CPlayerList::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CPlayerList m;
	const std::uint64_t n = r.varint();
	if (n > kMaxPlayerListEntries) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	m.players.reserve(static_cast<std::size_t>(n));
	for (std::uint64_t i = 0; i < n && !r.failed(); ++i) {
		PlayerListEntry e;
		e.net_id = static_cast<core::NetId>(r.u32());
		e.name = r.string();
		m.players.push_back(std::move(e));
	}
	return finish(r, std::move(m));
}

void C2SUiEvent::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.string(ui_name);
	w.string(widget_id);
	w.string(event_kind);
	w.string(value_json);
}

Decoded<C2SUiEvent> C2SUiEvent::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	C2SUiEvent m;
	m.ui_name = r.string();
	m.widget_id = r.string();
	m.event_kind = r.string();
	m.value_json = r.string();
	return finish(r, std::move(m));
}

} // namespace vb::protocol
