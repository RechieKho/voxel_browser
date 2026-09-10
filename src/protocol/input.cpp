#include "vb/protocol/input.hpp"

#include "vb/protocol/byte_buffer.hpp"

namespace vb::protocol {

using core::Err;

void C2SInputBatch::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.varint(cmds.size());
	for (const auto &c : cmds) {
		w.u32(c.seq);
		w.f32(c.dt);
		w.f32(c.move.x);
		w.f32(c.move.y);
		w.f32(c.move.z);
		w.f32(c.yaw);
		w.f32(c.pitch);
		w.u8(c.buttons);
	}
}

Decoded<C2SInputBatch> C2SInputBatch::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	C2SInputBatch m;
	const std::uint64_t n = r.varint();
	if (n > kMaxCmds) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	m.cmds.reserve(static_cast<std::size_t>(n));
	for (std::uint64_t i = 0; i < n && !r.failed(); ++i) {
		InputCmd c;
		c.seq = r.u32();
		c.dt = r.f32();
		c.move.x = r.f32();
		c.move.y = r.f32();
		c.move.z = r.f32();
		c.yaw = r.f32();
		c.pitch = r.f32();
		c.buttons = r.u8();
		m.cmds.push_back(c);
	}
	r.expect_consumed();
	if (r.failed()) {
		return Err{ r.error() };
	}
	return m;
}

} // namespace vb::protocol
