#include "vb/protocol/chat.hpp"

#include "vb/protocol/byte_buffer.hpp"

namespace vb::protocol {

using core::Err;

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
