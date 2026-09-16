#include <doctest/doctest.h>

#include <ostream>

#include <optional>

#include "vb/net/integrated.hpp"
#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/protocol/input.hpp"

#if VB_WITH_COMPRESSION
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "vb/assetsync/cache.hpp"
#include "vb/assetsync/manifest.hpp"
#endif

using namespace vb::net;
using vb::core::NetId;
using vb::core::Vec3d;
using vb::protocol::InputCmd;

namespace {

InputCmd forward_cmd(std::uint32_t seq) {
	InputCmd c;
	c.seq = seq;
	c.dt = 0.05f;
	c.move.z = 1.0f; // forward
	c.yaw = 90.0f; // -> +X world
	return c;
}

} // namespace

TEST_CASE("C2SInputBatch round-trips") {
	vb::protocol::C2SInputBatch in;
	in.cmds = { forward_cmd(1), forward_cmd(2), forward_cmd(3) };
	in.cmds[2].buttons = vb::protocol::kInputJump | vb::protocol::kInputSprint;

	std::vector<std::byte> bytes;
	in.encode(bytes);
	auto out = vb::protocol::C2SInputBatch::decode(
			{ bytes.data(), bytes.size() });
	REQUIRE(out);
	REQUIRE(out->cmds.size() == 3);
	CHECK(out->cmds[2].seq == 3);
	CHECK(out->cmds[2].buttons ==
			(vb::protocol::kInputJump | vb::protocol::kInputSprint));
	CHECK(out->cmds[0].move.z == doctest::Approx(1.0));
}

TEST_CASE("integrated: client prediction converges to server authority") {
	HandshakeServerConfig sc;
	sc.world_seed = 1;
	IntegratedGame game(sc, HandshakeClientConfig{ "P", "", "v", 1 });

	for (int i = 0; i < 32 && !game.client_joined(); ++i) {
		game.tick(0.05);
	}
	REQUIRE(game.client_joined());

	const NetId id = game.client().join_accept()->your_net_id;
	const Vec3d spawn = game.client().join_accept()->spawn_pos;
	game.client().set_local_feet(spawn);

	// Fly so the test doesn't depend on terrain being streamed in.
	vb::physics::MoveParams fly;
	fly.fly = true;
	game.server().set_move_params(fly);
	game.client().set_move_params(fly);

	for (std::uint32_t seq = 1; seq <= 40; ++seq) {
		game.client().push_input(forward_cmd(seq));
		game.tick(0.05);
	}
	// Let the last acks arrive.
	for (int i = 0; i < 8; ++i) {
		game.tick(0.05);
	}

	const auto *srv = game.server().player_move_state(id);
	REQUIRE(srv != nullptr);
	const Vec3d predicted = game.client().predicted_feet();

	// Moved a meaningful distance along +X from spawn.
	CHECK(srv->position.x - spawn.x > 3.0);
	// Prediction agrees with the server within a small epsilon.
	CHECK(predicted.x == doctest::Approx(srv->position.x).epsilon(0.02));
	CHECK(predicted.z == doctest::Approx(srv->position.z).epsilon(0.02));
	CHECK(game.client().last_acked_input_seq() > 0);
	CHECK(game.client().unacked_input_count() < 40);
}

TEST_CASE("integrated: a second client sees the first move (interpolated)") {
	LoopbackNetwork net;
	ServerSession server(net.server(), [] {
		HandshakeServerConfig c;
		c.world_seed = 1;
		return c;
	}());
	vb::physics::MoveParams fly;
	fly.fly = true;
	server.set_move_params(fly);
	REQUIRE(net.server().listen(0));

	vb::net::Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	std::optional<ClientSession> a;
	a.emplace(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });

	vb::net::Transport &tb = net.create_client();
	auto idb = tb.connect("x", 0);
	REQUIRE(idb);
	std::optional<ClientSession> b;
	b.emplace(tb, *idb, HandshakeClientConfig{ "B", "", "v", 2 });

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			a->tick(0.05);
			b->tick(0.05);
		}
	};
	pump(16);
	REQUIRE(a->joined());
	REQUIRE(b->joined());
	const NetId a_id = a->join_accept()->your_net_id;
	a->set_move_params(fly);
	a->set_local_feet(a->join_accept()->spawn_pos);
	b->set_local_feet(b->join_accept()->spawn_pos);

	const Vec3d a_spawn = a->join_accept()->spawn_pos;
	Vec3d first;
	for (std::uint32_t seq = 1; seq <= 30; ++seq) {
		a->push_input(forward_cmd(seq));
		pump(1);
		if (seq == 5) {
			first = b->interpolated_pos(a_id);
		}
	}
	pump(4);

	REQUIRE(b->remote_entities().count(a_id) == 1);
	const Vec3d seen = b->interpolated_pos(a_id);
	CHECK(seen.x - a_spawn.x > 2.0);
	CHECK(seen.x > first.x); // it kept moving as B watched
}

#if VB_WITH_COMPRESSION

namespace {

std::filesystem::path make_asset_pack(const char *name) {
	auto p = std::filesystem::temp_directory_path() /
			(std::string("vb_netcode_assetsync_pack_") + name);
	std::filesystem::remove_all(p);
	std::filesystem::create_directories(p / "scripts");
	{
		std::ofstream f(p / "scripts" / "init.lua", std::ios::binary);
		f << "print('hello from the pack')";
	}
	{
		std::ofstream f(p / "readme.txt", std::ios::binary);
		f << "just a small text asset";
	}
	return p;
}

vb::net::HandshakeServerHost make_asset_host(
		std::shared_ptr<const vb::assetsync::Manifest> manifest_ptr,
		std::filesystem::path pack_root, int *file_bytes_calls = nullptr) {
	vb::net::HandshakeServerHost host;
	host.asset_manifest = [manifest_ptr] { return manifest_ptr; };
	host.asset_file_bytes = [manifest_ptr, pack_root, file_bytes_calls](
									vb::core::AssetHash h) -> std::optional<std::vector<std::byte>> {
		if (file_bytes_calls != nullptr) {
			++*file_bytes_calls;
		}
		if (!manifest_ptr) {
			return std::nullopt;
		}
		const auto *e = manifest_ptr->find(h);
		if (e == nullptr) {
			return std::nullopt;
		}
		std::ifstream f(pack_root / e->path, std::ios::binary);
		if (!f) {
			return std::nullopt;
		}
		std::vector<std::byte> buf(static_cast<std::size_t>(e->size));
		if (e->size > 0) {
			f.read(reinterpret_cast<char *>(buf.data()),
					static_cast<std::streamsize>(e->size));
		}
		return buf;
	};
	return host;
}

} // namespace

TEST_CASE("asset sync: a cold client downloads every pack file byte-identical") {
	const auto pack = make_asset_pack("cold");
	auto manifest_result = vb::assetsync::build_manifest(pack);
	REQUIRE(manifest_result);
	auto manifest_ptr = std::make_shared<const vb::assetsync::Manifest>(
			std::move(*manifest_result));

	LoopbackNetwork net;
	HandshakeServerHost host = make_asset_host(manifest_ptr, pack);
	ServerSession server(net.server(), HandshakeServerConfig{}, host);
	REQUIRE(net.server().listen(0));

	vb::net::Transport &t = net.create_client();
	auto id = t.connect("x", 0);
	REQUIRE(id);
	const auto cache_dir = std::filesystem::temp_directory_path() /
			"vb_netcode_assetsync_cache_cold";
	std::filesystem::remove_all(cache_dir);
	vb::assetsync::ClientAssetCache cache(cache_dir, 16ull * 1024ull * 1024ull);
	ClientSession client(t, *id, HandshakeClientConfig{ "Cold", "", "v", 1 }, &cache);

	for (int i = 0; i < 40 && !client.joined() && !client.failed(); ++i) {
		server.tick(0.05);
		client.tick(0.05);
	}
	REQUIRE(client.joined());

	REQUIRE(client.virtual_pack_fs().size() == manifest_ptr->entries.size());
	for (const auto &e : manifest_ptr->entries) {
		auto it = client.virtual_pack_fs().find(e.path);
		REQUIRE(it != client.virtual_pack_fs().end());
		std::ifstream f(pack / e.path, std::ios::binary);
		std::vector<char> on_disk((std::istreambuf_iterator<char>(f)),
				std::istreambuf_iterator<char>());
		REQUIRE(it->second.size() == on_disk.size());
		CHECK(std::memcmp(it->second.data(), on_disk.data(), on_disk.size()) == 0);
	}

	std::filesystem::remove_all(pack);
	std::filesystem::remove_all(cache_dir);
}

TEST_CASE("asset sync: a second connection with an unchanged pack transfers nothing") {
	const auto pack = make_asset_pack("reconnect");
	auto manifest_result = vb::assetsync::build_manifest(pack);
	REQUIRE(manifest_result);
	auto manifest_ptr = std::make_shared<const vb::assetsync::Manifest>(
			std::move(*manifest_result));

	const auto cache_dir = std::filesystem::temp_directory_path() /
			"vb_netcode_assetsync_cache_reconnect";
	std::filesystem::remove_all(cache_dir);

	// First connection: populates the cache.
	{
		LoopbackNetwork net;
		HandshakeServerHost host = make_asset_host(manifest_ptr, pack);
		ServerSession server(net.server(), HandshakeServerConfig{}, host);
		REQUIRE(net.server().listen(0));
		vb::net::Transport &t = net.create_client();
		auto id = t.connect("x", 0);
		REQUIRE(id);
		vb::assetsync::ClientAssetCache cache(cache_dir, 16ull * 1024ull * 1024ull);
		ClientSession client(t, *id, HandshakeClientConfig{ "First", "", "v", 1 }, &cache);
		for (int i = 0; i < 40 && !client.joined() && !client.failed(); ++i) {
			server.tick(0.05);
			client.tick(0.05);
		}
		REQUIRE(client.joined());
	}

	// Second connection: a fresh cache instance over the same directory
	// (simulating a client process restart) should never touch file bytes.
	int file_bytes_calls = 0;
	{
		LoopbackNetwork net;
		HandshakeServerHost host = make_asset_host(manifest_ptr, pack, &file_bytes_calls);
		ServerSession server(net.server(), HandshakeServerConfig{}, host);
		REQUIRE(net.server().listen(0));
		vb::net::Transport &t = net.create_client();
		auto id = t.connect("x", 0);
		REQUIRE(id);
		vb::assetsync::ClientAssetCache cache(cache_dir, 16ull * 1024ull * 1024ull);
		ClientSession client(t, *id, HandshakeClientConfig{ "Second", "", "v", 1 }, &cache);
		for (int i = 0; i < 40 && !client.joined() && !client.failed(); ++i) {
			server.tick(0.05);
			client.tick(0.05);
		}
		REQUIRE(client.joined());
		CHECK(client.virtual_pack_fs().size() == manifest_ptr->entries.size());
	}

	CHECK(file_bytes_calls == 0);

	std::filesystem::remove_all(pack);
	std::filesystem::remove_all(cache_dir);
}

#endif // VB_WITH_COMPRESSION
