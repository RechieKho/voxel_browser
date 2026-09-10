#include <doctest/doctest.h>

#include <ostream>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vb/core/version.hpp"
#include "vb/protocol/byte_buffer.hpp"
#include "vb/protocol/handshake.hpp"
#include "vb/protocol/message.hpp"
#include "vb/protocol/snapshot.hpp"

using namespace vb::protocol;

namespace {

std::span<const std::byte> as_span(const std::vector<std::byte> &v) {
	return { v.data(), v.size() };
}

template <typename T>
T round_trip(const T &in) {
	std::vector<std::byte> bytes;
	in.encode(bytes);
	auto decoded = T::decode(as_span(bytes));
	REQUIRE(decoded);
	return std::move(*decoded);
}

} // namespace

TEST_CASE("varint / svarint round-trip across boundaries") {
	const std::uint64_t values[] = { 0, 1, 127, 128, 300, 16383, 16384,
		std::uint64_t(1) << 35, ~std::uint64_t(0) };
	for (std::uint64_t v : values) {
		std::vector<std::byte> b;
		ByteWriter(b).varint(v);
		ByteReader r(as_span(b));
		CHECK(r.varint() == v);
		CHECK_FALSE(r.failed());
		CHECK(r.at_end());
	}
	for (std::int64_t v : { std::int64_t(0), std::int64_t(-1), std::int64_t(1),
				 std::int64_t(-1000000), std::int64_t(1) << 40 }) {
		std::vector<std::byte> b;
		ByteWriter(b).svarint(v);
		ByteReader r(as_span(b));
		CHECK(r.svarint() == v);
		CHECK_FALSE(r.failed());
	}
}

TEST_CASE("ByteReader reports underrun instead of reading garbage") {
	std::vector<std::byte> b;
	ByteWriter(b).u32(0xDEADBEEF);
	ByteReader r({ b.data(), 2 }); // only 2 of 4 bytes
	CHECK(r.u32() == 0);
	CHECK(r.failed());
	CHECK(r.error() == vb::core::ProtocolError::kShortBuffer);
}

TEST_CASE("overlong varint is rejected") {
	std::vector<std::byte> b(11, std::byte{ 0x80 }); // never-terminating
	ByteReader r(as_span(b));
	r.varint();
	CHECK(r.error() == vb::core::ProtocolError::kOverlongVarint);
}

TEST_CASE("frame envelope round-trips and detects partial buffers") {
	std::vector<std::byte> payload;
	ByteWriter(payload).string("hi there");

	std::vector<std::byte> wire;
	write_frame(wire, MessageType::kC2SChat, as_span(payload));
	CHECK(wire.size() == kEnvelopeBytes + payload.size());

	std::size_t consumed = 0;
	auto frame = read_frame(as_span(wire), consumed);
	REQUIRE(frame);
	CHECK(frame->header.type == MessageType::kC2SChat);
	CHECK(consumed == wire.size());

	auto partial = read_frame({ wire.data(), wire.size() - 1 }, consumed);
	CHECK_FALSE(partial);
	CHECK(partial.error() == vb::core::ProtocolError::kShortBuffer);
	CHECK(consumed == 0);
}

TEST_CASE("lane assignment matches the spec") {
	CHECK(lane_for(MessageType::kC2SHello) == Lane::kControl);
	CHECK(lane_for(MessageType::kS2CChunkAdd) == Lane::kWorld);
	CHECK(lane_for(MessageType::kS2CEntitySnapshot) == Lane::kSnapshot);
	CHECK(lane_for(MessageType::kS2CAssetData) == Lane::kAssets);
	CHECK(lane_for(MessageType::kC2SInputBatch) == Lane::kInput);
}

TEST_CASE("handshake structs round-trip") {
	C2SHello hello{ vb::kEngineProtocolVersion, 0x1122334455667788ull, "vb-test/0" };
	auto h2 = round_trip(hello);
	CHECK(h2.engine_protocol_version == vb::kEngineProtocolVersion);
	CHECK(h2.client_nonce == 0x1122334455667788ull);
	CHECK(h2.client_version == "vb-test/0");

	S2CServerInfo info{ "base", "1.0.0", vb::kEngineProtocolVersion, 20, "hello!",
		AuthMode::kNone };
	auto i2 = round_trip(info);
	CHECK(i2.pack_name == "base");
	CHECK(i2.tick_rate == 20);
	CHECK(i2.auth_mode == AuthMode::kNone);

	S2CJoinAccept accept{ vb::core::NetId{ 7 }, { 1.5, 64.25, -3.0 }, 0xABCDEF, 1200 };
	auto a2 = round_trip(accept);
	CHECK(a2.your_net_id == vb::core::NetId{ 7 });
	CHECK(a2.spawn_pos.y == doctest::Approx(64.25));
	CHECK(a2.world_seed == 0xABCDEF);
	CHECK(a2.time_of_day == 1200);

	auto d2 = round_trip(S2CDisconnect{ DisconnectReason::kServerFull, "full" });
	CHECK(d2.reason == DisconnectReason::kServerFull);
	CHECK(d2.message == "full");

	round_trip(C2SReady{});
}

TEST_CASE("entity snapshot round-trips") {
	S2CEntitySnapshot s;
	s.server_tick = 4242;
	s.last_acked_input_seq = 17;
	s.entered.push_back({ vb::core::NetId{ 3 }, vb::core::EntityKindId{ 1 },
			{ 1.0, 2.0, 3.0 }, { 45.0f, -10.0f }, { 0.1f, 0.0f, -0.2f }, 0 });
	s.updated.push_back({ vb::core::NetId{ 4 }, vb::core::EntityKindId{ 0 },
			{ -8.0, 64.0, 0.0 }, {}, {}, 2 });
	s.removed.push_back(vb::core::NetId{ 9 });

	auto s2 = round_trip(s);
	CHECK(s2.server_tick == 4242);
	CHECK(s2.last_acked_input_seq == 17);
	REQUIRE(s2.entered.size() == 1);
	CHECK(s2.entered[0] == s.entered[0]);
	REQUIRE(s2.updated.size() == 1);
	CHECK(s2.updated[0].pos.y == doctest::Approx(64.0));
	REQUIRE(s2.removed.size() == 1);
	CHECK(s2.removed[0] == vb::core::NetId{ 9 });
}

TEST_CASE("decode rejects a bad enum and trailing bytes") {
	std::vector<std::byte> bytes;
	S2CServerInfo{ "p", "v", 1, 20, "m", AuthMode::kNone }.encode(bytes);
	bytes.back() = std::byte{ 0x7F }; // clobber auth_mode
	auto bad = S2CServerInfo::decode(as_span(bytes));
	CHECK_FALSE(bad);
	CHECK(bad.error() == vb::core::ProtocolError::kBadEnum);

	std::vector<std::byte> extra;
	C2SReady{}.encode(extra);
	extra.push_back(std::byte{ 0 });
	auto trailing = C2SReady::decode(as_span(extra));
	CHECK_FALSE(trailing);
	CHECK(trailing.error() == vb::core::ProtocolError::kTrailingBytes);
}
