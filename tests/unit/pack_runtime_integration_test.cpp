#include <doctest/doctest.h>

#include <ostream>

#include <filesystem>
#include <memory>
#include <string>

#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/protocol/world.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/script/ui_runtime.hpp"
#include "vb/world/block.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/worker_pool.hpp"

#if VB_WITH_LUA

// End-to-end proof that Phase 4.2's block-edit veto/callback seam and
// player_join veto actually reach a real ServerSession/WorldReplicator over
// a LoopbackTransport, not just the bindings in isolation (pack_runtime_test.cpp).

using namespace vb::net;
using vb::core::IVec3;
using vb::core::NetId;
using vb::core::Vec3d;
namespace wg = vb::worldgen;

namespace {

std::filesystem::path temp_storage(const char *name) {
	auto p = std::filesystem::temp_directory_path() /
			(std::string("vb_pack_runtime_integration_") + name + ".json");
	std::filesystem::remove(p);
	return p;
}

IVec3 surface_voxel(const vb::world::BlockSolidQuery &q, int x, int z) {
	for (int y = 80; y > -16; --y) {
		if (q.solid_at({ x, y, z })) {
			return { x, y, z };
		}
	}
	return { x, 0, z };
}

} // namespace

TEST_CASE("pack script vetoes a block break and observes on_break") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("veto"));
	REQUIRE(rt.load_pack_file(R"(
		on_break_calls = 0
		local function count() on_break_calls = on_break_calls + 1 end
		-- The scanned surface voxel is base:grass or base:sand (beach)
		-- depending on the noise at that column; attach to both.
		vb.register_block({ name = "base:grass", on_break = count })
		vb.register_block({ name = "base:sand", on_break = count })
		vb.on("block_break", function(player, pos) return pos.y > 0 end)
	)"));
	rt.freeze();

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	auto replicator = std::make_unique<WorldReplicator>(world, pool, registry,
			/*view*/ 1, /*vview*/ 2);
	rt.attach_world(*replicator);
	server.set_world_replicator(std::move(replicator));
	rt.attach_session(server);
	REQUIRE(net.server().listen(0));

	Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	ClientSession client(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			client.tick(0.05);
			rt.dispatch_tick(0.05);
		}
	};

	pump(20);
	REQUIRE(client.joined());
	const NetId a_id = client.join_accept()->your_net_id;

	// A surface voxel (y > 0): the veto only rejects y <= 0.
	const IVec3 target = surface_voxel(world, 4, 4);
	REQUIRE(target.y > 0);
	server.set_player_state(
			a_id, Vec3d{ target.x + 0.5, target.y + 2.0, target.z + 0.5 });
	pump(6);
	REQUIRE(client.chunk_store().solid_at(target));

	vb::protocol::C2SBlockEdit e;
	e.predicted_seq = 1;
	e.action = vb::protocol::BlockEditAction::kBreak;
	e.pos = target;
	client.push_block_edit(e);
	pump(6);

	CHECK_FALSE(world.solid_at(target)); // accepted
	CHECK_FALSE(client.chunk_store().solid_at(target));
	REQUIRE(rt.load_pack_file("assert(on_break_calls == 1)"));

	// A deep target: same reach setup but the veto (pos.y > 0) should now
	// reject it, so the block stays intact.
	const IVec3 deep{ 20, -5, 20 };
	server.set_player_state(
			a_id, Vec3d{ deep.x + 0.5, deep.y + 2.0, deep.z + 0.5 });
	pump(6);
	REQUIRE(world.solid_at(deep)); // generator fills solid stone this deep

	vb::protocol::C2SBlockEdit e2;
	e2.predicted_seq = 2;
	e2.action = vb::protocol::BlockEditAction::kBreak;
	e2.pos = deep;
	client.push_block_edit(e2);
	pump(6);

	CHECK(world.solid_at(deep)); // vetoed: unchanged
}

TEST_CASE("pack script vetoes a specific player's join") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("join_veto"));
	REQUIRE(rt.load_pack_file(
			R"(vb.on("player_join", function(name) return name ~= "Blocked" end))"));
	rt.freeze();

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	HandshakeServerHost host;
	rt.install_join_veto(host);
	ServerSession server(net.server(), cfg, host);
	REQUIRE(net.server().listen(0));

	Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	ClientSession good(ta, *ida, HandshakeClientConfig{ "Alice", "", "v", 1 });

	Transport &tb = net.create_client();
	auto idb = tb.connect("x", 0);
	REQUIRE(idb);
	ClientSession blocked(tb, *idb, HandshakeClientConfig{ "Blocked", "", "v", 2 });

	for (int i = 0; i < 20; ++i) {
		server.tick(0.05);
		good.tick(0.05);
		blocked.tick(0.05);
	}

	CHECK(good.joined());
	CHECK(blocked.failed());
}

TEST_CASE("player_leave dispatch fires with the right net id") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("leave"));
	REQUIRE(rt.load_pack_file(R"(
		left_fired = false
		vb.on("player_leave", function(p) left_fired = true end)
	)"));
	rt.freeze();

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	auto replicator = std::make_unique<WorldReplicator>(world, pool, registry,
			/*view*/ 0, /*vview*/ 0);
	rt.attach_world(*replicator);
	server.set_world_replicator(std::move(replicator));
	rt.attach_session(server);
	REQUIRE(net.server().listen(0));

	Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	ClientSession client(ta, *ida, HandshakeClientConfig{ "Leaver", "", "v", 1 });

	for (int i = 0; i < 20; ++i) {
		server.tick(0.05);
		client.tick(0.05);
		rt.dispatch_tick(0.05);
	}
	REQUIRE(client.joined());
	const NetId expected_id = client.join_accept()->your_net_id;

	ta.close(*ida, "left"); // triggers the server's kDisconnected event

	NetId captured_id = NetId::kInvalid;
	for (int i = 0; i < 10; ++i) {
		server.tick(0.05);
		for (auto &l : server.take_leaves()) {
			captured_id = l.net_id;
			rt.dispatch_player_leave(l);
		}
		rt.dispatch_tick(0.05);
	}

	CHECK(captured_id == expected_id);
	const auto check = rt.load_pack_file("assert(left_fired == true)");
	REQUIRE(check);
}

TEST_CASE("client UI round trip: server open_ui -> click -> server ui_event") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("ui"));
	REQUIRE(rt.load_pack_file(R"(
		seen_ui_name = nil
		seen_widget_id = nil
		seen_kind = nil
		seen_value = nil
		vb.on("ui_event", function(player, ui_name, widget_id, kind, value)
			seen_ui_name = ui_name
			seen_widget_id = widget_id
			seen_kind = kind
			seen_value = value
		end)
		-- Piggyback on block_break: it's the first event that hands the
		-- script a PlayerHandle for an online player (player_join fires
		-- before a net id/session exist).
		vb.on("block_break", function(player, pos)
			player:open_ui("test_ui", { greeting = "hi" })
			return true
		end)
	)"));
	rt.freeze();

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	auto replicator = std::make_unique<WorldReplicator>(world, pool, registry,
			/*view*/ 1, /*vview*/ 2);
	rt.attach_world(*replicator);
	server.set_world_replicator(std::move(replicator));
	rt.attach_session(server);
	REQUIRE(net.server().listen(0));

	Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	ClientSession client(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });

	vb::script::UiRuntime ui_runtime;
	ui_runtime.attach_session(client);
	REQUIRE(ui_runtime.load_pack_file(R"(
		ui.define("test_ui", function(ctx)
			return {
				widgets = {
					{ id = "close_btn", type = "button", x = 0, y = 0, w = 10, h = 10,
					  text = "Close",
					  on_click = function() ui.send_event("clicked", true) end },
				}
			}
		end)
	)"));

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			client.tick(0.05);
			if (auto opened = client.take_open_ui()) {
				ui_runtime.open(opened->ui_name, opened->ctx_json);
			}
		}
	};

	pump(20);
	REQUIRE(client.joined());
	const NetId a_id = client.join_accept()->your_net_id;

	const IVec3 target = surface_voxel(world, 4, 4);
	server.set_player_state(
			a_id, Vec3d{ target.x + 0.5, target.y + 2.0, target.z + 0.5 });
	pump(6);
	REQUIRE(client.chunk_store().solid_at(target));

	vb::protocol::C2SBlockEdit e;
	e.predicted_seq = 1;
	e.action = vb::protocol::BlockEditAction::kBreak;
	e.pos = target;
	client.push_block_edit(e);
	pump(6);

	REQUIRE(ui_runtime.is_open());
	CHECK(ui_runtime.current_name() == "test_ui");
	REQUIRE(ui_runtime.widgets().size() == 1);

	ui_runtime.report_click("close_btn");
	pump(4);

	REQUIRE(rt.load_pack_file(R"(
		assert(seen_ui_name == "test_ui")
		assert(seen_widget_id == "close_btn")
		assert(seen_kind == "clicked")
		assert(seen_value == true)
	)"));
}

TEST_CASE("pack script vetoes chat from a specific player") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("chat_veto"));
	REQUIRE(rt.load_pack_file(
			R"(vb.on("chat", function(player, text) return player:get_name() ~= "Blocked" end))"));
	rt.freeze();

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	rt.attach_session(server);
	REQUIRE(net.server().listen(0));

	Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	ClientSession allowed(ta, *ida, HandshakeClientConfig{ "Allowed", "", "v", 1 });

	Transport &tb = net.create_client();
	auto idb = tb.connect("x", 0);
	REQUIRE(idb);
	ClientSession blocked(tb, *idb, HandshakeClientConfig{ "Blocked", "", "v", 2 });

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			allowed.tick(0.05);
			blocked.tick(0.05);
		}
	};
	pump(16);
	REQUIRE(allowed.joined());
	REQUIRE(blocked.joined());
	// Drain the join system line(s) both clients may have picked up while
	// joining near-simultaneously -- not what this test asserts on.
	allowed.take_chat_messages();
	blocked.take_chat_messages();

	allowed.send_chat("hi everyone");
	blocked.send_chat("i should not be heard");
	pump(4);

	const auto seen = allowed.take_chat_messages();
	REQUIRE(seen.size() == 1);
	CHECK(seen[0] == "Allowed: hi everyone");
	CHECK(blocked.take_chat_messages() == seen); // same broadcast, both see it
}

TEST_CASE("player:give() pushes a live S2C_Inventory to the client") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("inventory"));
	// No player-handle-bearing event fires at join time (spec §4.2's own gap
	// note: player_join only hands a name), so drive give() from the chat
	// veto seam instead -- it already hands a real PlayerHandle.
	REQUIRE(rt.load_pack_file(R"(
		vb.on("chat", function(player, text)
			player:give({ item = 2, count = 5 })
			return true
		end)
	)"));
	rt.freeze();

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	rt.attach_session(server);
	REQUIRE(net.server().listen(0));

	Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	ClientSession client(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			client.tick(0.05);
			rt.dispatch_tick(0.05);
		}
	};
	pump(16);
	REQUIRE(client.joined());
	CHECK(client.inventory().empty()); // nothing given yet

	client.send_chat("give me stone");
	pump(6);

	const auto &inv = client.inventory();
	REQUIRE(inv.size() == 1);
	CHECK(static_cast<int>(inv[0].item) == 2);
	CHECK(inv[0].count == 5);
}

TEST_CASE("vb.world.spawn_item_drop replicates to a client and is picked up on approach") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("item_drop"));
	REQUIRE(rt.load_pack_file(R"(
		vb.on("chat", function(player, text)
			vb.world.spawn_item_drop({ x = 5, y = 5, z = 5 }, 3, 2)
			return true
		end)
	)"));
	rt.freeze();

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	rt.attach_session(server);
	REQUIRE(net.server().listen(0));

	Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	ClientSession client(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			client.tick(0.05);
			rt.dispatch_tick(0.05);
		}
	};
	pump(16);
	REQUIRE(client.joined());
	const NetId a_id = client.join_accept()->your_net_id;

	// Far from the drop's spawn position: no interest yet.
	server.set_player_state(a_id, Vec3d{ 100, 100, 100 });
	client.send_chat("drop it");
	pump(6);
	CHECK(client.remote_entities().empty());
	CHECK(client.inventory().empty());

	// Move within interest range (default radius) and the drop should now
	// replicate as a remote entity -- but not be picked up yet (still 2m
	// away from its exact position).
	server.set_player_state(a_id, Vec3d{ 5, 5, 7 });
	pump(6);
	CHECK_FALSE(client.remote_entities().empty());
	CHECK(client.inventory().empty());

	// Walk directly onto it: picked up, credited to inventory, and removed
	// from replication.
	server.set_player_state(a_id, Vec3d{ 5, 5, 5 });
	pump(6);
	CHECK(client.remote_entities().empty());
	const auto &inv = client.inventory();
	REQUIRE(inv.size() == 1);
	CHECK(static_cast<int>(inv[0].item) == 3);
	CHECK(inv[0].count == 2);
}

#endif // VB_WITH_LUA
