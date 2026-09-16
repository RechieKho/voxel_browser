#include "vb/protocol/inventory.hpp"

#include "vb/protocol/byte_buffer.hpp"

namespace vb::protocol {

using core::Err;

namespace {

// Generous cap, same defensive-decode posture as S2CPlayerList's entry cap --
// a real inventory has, at most, a few dozen slots.
inline constexpr std::uint64_t kMaxInventorySlots = 4096u;

} // namespace

void S2CInventory::encode(std::vector<std::byte> &out) const {
	ByteWriter w(out);
	w.varint(slots.size());
	for (const auto &s : slots) {
		w.u16(static_cast<std::uint16_t>(s.item));
		w.u16(s.count);
	}
}

Decoded<S2CInventory> S2CInventory::decode(std::span<const std::byte> in) {
	ByteReader r(in);
	S2CInventory m;
	const std::uint64_t n = r.varint();
	if (n > kMaxInventorySlots) {
		return Err{ core::ProtocolError::kLengthExceeded };
	}
	m.slots.reserve(static_cast<std::size_t>(n));
	for (std::uint64_t i = 0; i < n && !r.failed(); ++i) {
		InventorySlot s;
		s.item = static_cast<core::BlockId>(r.u16());
		s.count = r.u16();
		m.slots.push_back(s);
	}
	r.expect_consumed();
	if (r.failed()) {
		return Err{ r.error() };
	}
	return m;
}

} // namespace vb::protocol
