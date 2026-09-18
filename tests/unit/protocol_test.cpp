#include <doctest/doctest.h>

#include <ostream>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "vb/core/version.hpp"
#include "vb/protocol/assetsync.hpp"
#include "vb/protocol/byte_buffer.hpp"
#include "vb/protocol/chat.hpp"
#include "vb/protocol/handshake.hpp"
#include "vb/protocol/input.hpp"
#include "vb/protocol/inventory.hpp"
#include "vb/protocol/message.hpp"
#include "vb/protocol/snapshot.hpp"
#include "vb/protocol/world.hpp"

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
	CHECK(lane_for(MessageType::kS2CKeybindRegistry) == Lane::kWorld);
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

TEST_CASE("chat / open_ui round-trip") {
	auto c1 = round_trip(C2SChat{ "hi there" });
	CHECK(c1.text == "hi there");

	auto c2 = round_trip(S2CChat{ "hello world" });
	CHECK(c2.text == "hello world");

	auto u2 = round_trip(S2COpenUi{ "inventory", R"({"slots":3})" });
	CHECK(u2.ui_name == "inventory");
	CHECK(u2.ctx_json == R"({"slots":3})");

	// Empty ctx_json (no ctx table passed) round-trips too.
	auto u3 = round_trip(S2COpenUi{ "pause", "" });
	CHECK(u3.ui_name == "pause");
	CHECK(u3.ctx_json.empty());

	auto e2 = round_trip(C2SUiEvent{ "inventory", "close_btn", "click", "null" });
	CHECK(e2.ui_name == "inventory");
	CHECK(e2.widget_id == "close_btn");
	CHECK(e2.event_kind == "click");
	CHECK(e2.value_json == "null");
}

TEST_CASE("player join / leave / list round-trip") {
	auto j = round_trip(S2CPlayerJoin{ vb::core::NetId{ 3 }, "Alice" });
	CHECK(j.net_id == vb::core::NetId{ 3 });
	CHECK(j.name == "Alice");

	auto l = round_trip(S2CPlayerLeave{ vb::core::NetId{ 3 } });
	CHECK(l.net_id == vb::core::NetId{ 3 });

	auto empty = round_trip(S2CPlayerList{});
	CHECK(empty.players.empty());

	S2CPlayerList list;
	list.players.push_back({ vb::core::NetId{ 1 }, "Alice" });
	list.players.push_back({ vb::core::NetId{ 2 }, "Bob" });
	auto list2 = round_trip(list);
	REQUIRE(list2.players.size() == 2);
	CHECK(list2.players[0].net_id == vb::core::NetId{ 1 });
	CHECK(list2.players[0].name == "Alice");
	CHECK(list2.players[1].net_id == vb::core::NetId{ 2 });
	CHECK(list2.players[1].name == "Bob");
}

TEST_CASE("inventory round-trips, including an empty snapshot") {
	auto empty = round_trip(S2CInventory{});
	CHECK(empty.slots.empty());

	S2CInventory inv;
	inv.slots.push_back({ vb::core::BlockId{ 3 }, 5 });
	inv.slots.push_back({ vb::core::BlockId{ 7 }, 64 });
	auto inv2 = round_trip(inv);
	REQUIRE(inv2.slots.size() == 2);
	CHECK(inv2.slots[0].item == vb::core::BlockId{ 3 });
	CHECK(inv2.slots[0].count == 5);
	CHECK(inv2.slots[1].item == vb::core::BlockId{ 7 });
	CHECK(inv2.slots[1].count == 64);
}

TEST_CASE("block registry round-trips, including an empty list") {
	auto empty = round_trip(S2CBlockRegistry{});
	CHECK(empty.blocks.empty());

	S2CBlockRegistry reg;
	reg.blocks.push_back({ "base:air", false, false, false, 0, 0 });
	reg.blocks.push_back({ "base:stone", true, true, false, 0, 0 });
	reg.blocks.push_back({ "test:glow", true, true, false, 15, 0 });
	reg.blocks.push_back({ "test:crumbly", true, true, false, 0, 5 });
	auto r2 = round_trip(reg);
	REQUIRE(r2.blocks.size() == 4);
	CHECK(r2.blocks[0].name == "base:air");
	CHECK_FALSE(r2.blocks[0].solid);
	CHECK(r2.blocks[2].name == "test:glow");
	CHECK(r2.blocks[2].light_emission == 15);
	CHECK(r2.blocks[3].max_damage == 5); // Phase 6.5
	CHECK(r2.blocks == reg.blocks);
}

TEST_CASE("block-break begin/stop round-trip (Phase 6.5)") {
	auto begin = round_trip(
			C2SBlockBreakBegin{ { 1, 2, 3 }, { 0, 1, 0 } });
	CHECK(begin.pos == vb::core::IVec3{ 1, 2, 3 });
	CHECK(begin.face == vb::core::IVec3{ 0, 1, 0 });

	auto begin_neg = round_trip(
			C2SBlockBreakBegin{ { -5, -100, 7 }, { -1, 0, 0 } });
	CHECK(begin_neg.pos == vb::core::IVec3{ -5, -100, 7 });
	CHECK(begin_neg.face == vb::core::IVec3{ -1, 0, 0 });

	auto stop = round_trip(C2SBlockBreakStop{ { 1, 2, 3 } });
	CHECK(stop.pos == vb::core::IVec3{ 1, 2, 3 });
}

TEST_CASE("move params round-trip (Phase 6.7)") {
	auto defaults = round_trip(S2CMoveParams{});
	CHECK(defaults.gravity == doctest::Approx(28.0));
	CHECK(defaults.fly == false);

	S2CMoveParams p;
	p.half_width = 0.35;
	p.height = 1.9;
	p.eye_height = 1.7;
	p.walk_speed = 5.0;
	p.sprint_speed = 9.0;
	p.accel = 40.0;
	p.air_accel = 8.0;
	p.friction = 10.0;
	p.gravity = 12.5;
	p.jump_speed = 6.0;
	p.terminal_velocity = 50.0;
	p.step_height = 1.1;
	p.fly_speed = 20.0;
	p.fly = true;
	auto p2 = round_trip(p);
	CHECK(p2.half_width == doctest::Approx(0.35));
	CHECK(p2.height == doctest::Approx(1.9));
	CHECK(p2.eye_height == doctest::Approx(1.7));
	CHECK(p2.walk_speed == doctest::Approx(5.0));
	CHECK(p2.sprint_speed == doctest::Approx(9.0));
	CHECK(p2.accel == doctest::Approx(40.0));
	CHECK(p2.air_accel == doctest::Approx(8.0));
	CHECK(p2.friction == doctest::Approx(10.0));
	CHECK(p2.gravity == doctest::Approx(12.5));
	CHECK(p2.jump_speed == doctest::Approx(6.0));
	CHECK(p2.terminal_velocity == doctest::Approx(50.0));
	CHECK(p2.step_height == doctest::Approx(1.1));
	CHECK(p2.fly_speed == doctest::Approx(20.0));
	CHECK(p2.fly == true);
}

TEST_CASE("day/night curve round-trips, including an empty list (Phase 6.8)") {
	auto empty = round_trip(S2CDayNightCurve{});
	CHECK(empty.keyframes.empty());

	S2CDayNightCurve curve;
	curve.keyframes = {
		{ 0, 0.1, 10, 20, 30 },
		{ 12000, 0.9, 200, 210, 220 },
	};
	auto r2 = round_trip(curve);
	REQUIRE(r2.keyframes.size() == 2);
	CHECK(r2.keyframes[0].tick == 0);
	CHECK(r2.keyframes[0].brightness == doctest::Approx(0.1));
	CHECK(r2.keyframes[0].r == 10);
	CHECK(r2.keyframes[1].tick == 12000);
	CHECK(r2.keyframes[1].brightness == doctest::Approx(0.9));
	CHECK(r2.keyframes[1].b == 220);
	CHECK(r2.keyframes == curve.keyframes);
}

TEST_CASE("keybind registry round-trips, including an empty list") {
	auto empty = round_trip(S2CKeybindRegistry{});
	CHECK(empty.names.empty());

	S2CKeybindRegistry reg;
	reg.names = { "dash", "interact", "toggle_map" };
	auto r2 = round_trip(reg);
	REQUIRE(r2.names.size() == 3);
	CHECK(r2.names[0] == "dash");
	CHECK(r2.names[2] == "toggle_map");
	CHECK(r2.names == reg.names);
}

TEST_CASE("keybind registry decode rejects more than kMaxKeybinds names") {
	S2CKeybindRegistry reg;
	for (std::size_t i = 0; i <= S2CKeybindRegistry::kMaxKeybinds; ++i) {
		reg.names.push_back("bind_" + std::to_string(i));
	}
	std::vector<std::byte> bytes;
	reg.encode(bytes);
	auto decoded = S2CKeybindRegistry::decode(as_span(bytes));
	CHECK_FALSE(decoded);
	CHECK(decoded.error() == vb::core::ProtocolError::kLengthExceeded);
}

TEST_CASE("time of day round-trips") {
	auto t = round_trip(S2CTimeOfDay{ 12345 });
	CHECK(t.time_of_day == 12345);
}

TEST_CASE("asset sync messages round-trip") {
	const vb::core::AssetHash h1{ 0x1122334455667788ull, 0x99AABBCCDDEEFF00ull };
	const vb::core::AssetHash h2{ 0xDEADBEEFDEADBEEFull, 0x1ull };

	auto req = round_trip(C2SAssetManifestRequest{ h1 });
	CHECK(req.known_manifest_hash.lo == h1.lo);
	CHECK(req.known_manifest_hash.hi == h1.hi);

	S2CAssetManifest manifest;
	manifest.manifest_hash = h1;
	manifest.total_bytes = 4096;
	manifest.entries.push_back({ "scripts/init.lua", h1, 128, AssetKind::kScript });
	manifest.entries.push_back({ "textures/stone.png", h2, 2048, AssetKind::kTexture });
	auto m2 = round_trip(manifest);
	CHECK(m2.manifest_hash.lo == h1.lo);
	CHECK(m2.total_bytes == 4096);
	REQUIRE(m2.entries.size() == 2);
	CHECK(m2.entries[0] == manifest.entries[0]);
	CHECK(m2.entries[1].kind == AssetKind::kTexture);

	auto empty_manifest = round_trip(S2CAssetManifest{ h1, 0, {} });
	CHECK(empty_manifest.entries.empty());

	auto empty_req = round_trip(C2SAssetRequest{});
	CHECK(empty_req.missing.empty());
	auto req2 = round_trip(C2SAssetRequest{ { h1, h2 } });
	REQUIRE(req2.missing.size() == 2);
	CHECK(req2.missing[1].lo == h2.lo);

	S2CAssetData data;
	data.hash = h2;
	data.seq = 3;
	data.total_chunks = 7;
	data.bytes = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };
	auto d2 = round_trip(data);
	CHECK(d2.hash.hi == h2.hi);
	CHECK(d2.seq == 3);
	CHECK(d2.total_chunks == 7);
	REQUIRE(d2.bytes.size() == 3);
	CHECK(d2.bytes[2] == std::byte{ 3 });

	// Zero-length chunk (a 0-byte file's single terminating chunk).
	auto d3 = round_trip(S2CAssetData{ h1, 0, 1, {} });
	CHECK(d3.bytes.empty());
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
