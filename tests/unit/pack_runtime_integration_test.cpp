#include <doctest/doctest.h>

#include <ostream>

#include <algorithm>
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

// Phase 6.17: block breaking is no longer an engine default (the old
// hardcoded client-side hold-to-break timer is gone). The client only ever
// reports raw input (InputCmd::buttons's kInputPrimary bit, "LMB held");
// this proves (a) holding it does nothing at all without any pack policy,
// and (b) a minimal vb.on("player_input", ...) handler calling the new
// player:break_block() is enough for a pack to implement breaking itself,
// through the exact same validated pipeline a real C2S_BlockEdit uses.
TEST_CASE("block breaking is opt-in content, not an engine default: "
		  "buttons.primary alone does nothing until a pack calls "
		  "player:break_block()") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("break_block"));
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

	const IVec3 target = surface_voxel(world, 4, 4);
	REQUIRE(target.y > 0);
	pump(6);
	REQUIRE(world.solid_at(target));

	// Holding "primary" every cmd, with no pack handler registered at all,
	// must not break anything -- breaking has to be something a pack opts
	// into, not a side effect the engine produces on its own. Note this
	// deliberately does *not* call server.set_player_state() to line the
	// player up with `target` first: sending a real InputCmd (unlike
	// pack_runtime_integration_test.cpp's veto test above, which only ever
	// sends a single C2S_BlockEdit) marks the connection input-driven, and
	// handle_input_batch's post-loop interest_.upsert() then overwrites the
	// interest-grid position with the ECS-authoritative one every tick
	// regardless -- there'd be nothing left to assert about reach.
	vb::protocol::InputCmd held;
	held.buttons = vb::protocol::kInputPrimary;
	for (std::uint32_t i = 1; i <= 5; ++i) {
		held.seq = i;
		client.push_input(held);
		pump(1);
	}
	CHECK(world.solid_at(target)); // untouched: no pack policy at all

	// Now install a minimal player_input handler that breaks the exact
	// target the instant it sees buttons.primary held. ServerSession only
	// wires its input handler at attach_session() time, gated on whether a
	// "player_input" handler was registered *by then* (Phase 6.3's "packs
	// that never use this channel pay zero extra cost" posture) -- since
	// this test registers one only now, well after the first attach_session()
	// call, it must call attach_session() again to actually pick it up.
	REQUIRE(rt.load_pack_file(
			"vb.on('player_input', function(player, input) "
			"if input.buttons.primary then player:break_block(" +
			std::to_string(target.x) + ", " + std::to_string(target.y) + ", " +
			std::to_string(target.z) + ") end end)"));
	rt.attach_session(server);

	// set_player_state() right before the triggering cmd, not earlier: the
	// player_input hook fires *inside* handle_input_batch's per-cmd loop,
	// before that same call's post-loop interest_.upsert() overwrite (see
	// the comment above) -- so this position is exactly what the hook (and
	// therefore break_block's reach check) sees for this one cmd.
	server.set_player_state(
			a_id, Vec3d{ target.x + 0.5, target.y + 2.0, target.z + 0.5 });
	held.seq = 100;
	client.push_input(held);
	pump(3);

	CHECK_FALSE(world.solid_at(target));
	CHECK_FALSE(client.chunk_store().solid_at(target));
}

TEST_CASE("shared block-damage breaking: begin -> tick -> completes the break "
		  "(Phase 6.5)") {
	// Registry must be frozen (pack loaded) *before* World copies it (matches
	// src/server/main.cpp's real construction order) -- otherwise the new
	// "test:crumbly" block and its max_damage never reach World's own
	// BlockRegistry copy, which is what apply_block_edit/handle_block_break_
	// begin actually query.
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("block_damage"));
	REQUIRE(rt.load_pack_file(R"(
		vb.register_block({ name = "test:crumbly", max_damage = 3 })
		vb.on("block_break_tick", function(player, pos) return 1 end)
	)"));
	rt.freeze();

	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

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

	const IVec3 target = surface_voxel(world, 4, 4);
	REQUIRE(target.y > 0);
	const vb::core::BlockId crumbly = registry.find("test:crumbly");
	REQUIRE(crumbly != vb::core::BlockId::kAir);
	world.set_block(target, crumbly);
	REQUIRE(world.solid_at(target));

	server.set_player_state(
			a_id, Vec3d{ target.x + 0.5, target.y + 2.0, target.z + 0.5 });
	pump(1);

	client.send_block_break_begin(target, { 0, 1, 0 });
	pump(1);
	CHECK(world.solid_at(target)); // one tick of damage (1/3), not broken yet

	pump(3); // two more ticks of damage_tick_fn -> 3 == max_damage
	// completed: the existing BlockEdit pipeline actually broke it
	CHECK_FALSE(world.solid_at(target));
}

TEST_CASE("a target with max_damage == 0 never reaches the damage system") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("block_damage_zero"));
	// Registers the hook, but the target block (base:stone) keeps its default
	// max_damage == 0 -- begin must be rejected outright regardless.
	REQUIRE(rt.load_pack_file(R"(
		vb.on("block_break_tick", function(player, pos) return 100 end)
	)"));
	rt.freeze();

	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	auto replicator = std::make_unique<WorldReplicator>(world, pool, registry, 1, 2);
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

	const IVec3 deep{ 20, -5, 20 };
	server.set_player_state(
			a_id, Vec3d{ deep.x + 0.5, deep.y + 2.0, deep.z + 0.5 });
	pump(6); // let the chunk around `deep` actually load
	REQUIRE(world.solid_at(deep)); // generator fills solid stone this deep

	client.send_block_break_begin(deep, { 0, 1, 0 });
	pump(20); // even a huge per-tick delta never accrues -- begin was rejected
	CHECK(world.solid_at(deep));
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

TEST_CASE("pack script vetoes and replaces player input via a handler chain") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("input"));
	REQUIRE(rt.load_pack_file(R"(
		vb.register_keybind("dash")
		seen_dash = nil
		second_saw_move_x = nil
		second_saw_move_z = nil
		vb.on("player_input", function(player, input)
			seen_dash = input.keybinds["dash"]
			if input.buttons.secondary then
				return false -- veto
			end
			if input.keybinds["dash"] then
				-- Only override x; y/z (and every other field) must pass
				-- through unchanged to the next handler in the chain.
				return { move = { x = 42.0 } }
			end
		end)
		vb.on("player_input", function(player, input)
			second_saw_move_x = input.move.x
			second_saw_move_z = input.move.z
		end)
	)"));
	rt.freeze();

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	HandshakeServerHost host;
	rt.install_keybind_registry(host);
	ServerSession server(net.server(), cfg, host);
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

	// The registered keybind reached the client as S2C_KeybindRegistry,
	// alongside the engine's 8 pre-registered move/action names (Phase
	// 6.19) that every PackRuntime now seeds before any pack script runs.
	REQUIRE(client.registered_keybinds().size() == 9);
	const auto &names = client.registered_keybinds();
	const auto dash_it = std::find(names.begin(), names.end(), "dash");
	REQUIRE(dash_it != names.end());
	const std::size_t dash_bit =
			static_cast<std::size_t>(dash_it - names.begin());

	const Vec3d pos_at_join = server.player_move_state(a_id)->position;

	// Cmd 1: secondary held -> vetoed. Its effect on movement is dropped
	// entirely (not even gravity), so the authoritative position must be
	// bit-for-bit unchanged.
	vb::protocol::InputCmd veto_cmd;
	veto_cmd.seq = 1;
	veto_cmd.dt = 0.05f;
	veto_cmd.buttons = vb::protocol::kInputSecondary;
	client.push_input(veto_cmd);
	pump(4);

	REQUIRE(rt.load_pack_file("assert(seen_dash == false)"));
	const Vec3d pos_after_veto = server.player_move_state(a_id)->position;
	CHECK(pos_after_veto.x == doctest::Approx(pos_at_join.x));
	CHECK(pos_after_veto.y == doctest::Approx(pos_at_join.y));
	CHECK(pos_after_veto.z == doctest::Approx(pos_at_join.z));

	// Cmd 2: dash held, not vetoed -> the first handler's { move = { x = 42
	// } } reaches the second handler with x replaced but z untouched, and
	// movement actually integrates this time (position changes).
	vb::protocol::InputCmd dash_cmd;
	dash_cmd.seq = 2;
	dash_cmd.dt = 0.05f;
	dash_cmd.move = { 1.0f, 0.0f, 7.0f };
	dash_cmd.keybinds = 1u << dash_bit;
	client.push_input(dash_cmd);
	pump(4);

	REQUIRE(rt.load_pack_file(R"(
		assert(seen_dash == true)
		assert(second_saw_move_x == 42.0)
		assert(second_saw_move_z == 7.0)
	)"));
	const Vec3d pos_after_dash = server.player_move_state(a_id)->position;
	CHECK((pos_after_dash.x != pos_after_veto.x ||
			pos_after_dash.y != pos_after_veto.y ||
			pos_after_dash.z != pos_after_veto.z));
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
		ui.define("test_ui", function(state)
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
			if (ui_runtime.is_open()) {
				ui_runtime.render_frame();
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

TEST_CASE("pack script rewrites chat text before it broadcasts (Phase 6.10)") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("chat_rewrite"));
	// Two handlers chained: the first uppercases, the second appends a tag --
	// proves each handler sees the prior one's replacement, not the original
	// C2S_Chat text, same chaining contract as run_player_input.
	REQUIRE(rt.load_pack_file(R"(
		vb.on("chat", function(player, text) return text:upper() end)
		vb.on("chat", function(player, text) return text .. " [mod]" end)
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
		}
	};
	pump(16);
	REQUIRE(client.joined());
	client.take_chat_messages(); // drain join system line(s)

	client.send_chat("hi everyone");
	pump(4);

	const auto seen = client.take_chat_messages();
	REQUIRE(seen.size() == 1);
	CHECK(seen[0] == "A: HI EVERYONE [mod]");
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

TEST_CASE("vb.register_block{pickup_radius=...} widens a dropped item's "
		  "pickup range beyond the engine default") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("item_drop_radius"));
	REQUIRE(rt.load_pack_file(R"(
		magnet_id = vb.register_block({ name = "test:magnet", pickup_radius = 10 })
		vb.on("chat", function(player, text)
			vb.world.spawn_item_drop({ x = 5, y = 5, z = 5 }, magnet_id, 1)
			return true
		end)
	)"));
	rt.freeze();

	// Constructed after freeze() so its registry copy includes "test:magnet"
	// (see STATE.md's construction-order note -- World copies BlockRegistry
	// by value at construction time).
	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	auto replicator = std::make_unique<WorldReplicator>(world, pool, registry, 1, 2);
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
	pump(16);
	REQUIRE(client.joined());
	const NetId a_id = client.join_accept()->your_net_id;

	client.send_chat("drop it");
	pump(6);

	// 8m away: well outside ItemDropSystem's 1.5m engine default, but inside
	// this block's own pickup_radius = 10 override.
	server.set_player_state(a_id, Vec3d{ 13, 5, 5 });
	pump(6);
	const auto &inv = client.inventory();
	REQUIRE(inv.size() == 1);
	CHECK(inv[0].count == 1);
}

TEST_CASE(
		"player:damage() + vb.on('player_death') drives a custom respawn "
		"(heal/pos/message/drop_inventory)") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("death"));
	REQUIRE(rt.load_pack_file(R"(
		seen_cause = nil
		seen_health_before = nil
		vb.on("player_death", function(player, cause, health_before)
			seen_cause = cause
			seen_health_before = health_before
			return { heal = 7, pos = { x = 1, y = 2, z = 3 },
				message = "* custom respawn", drop_inventory = true }
		end)
		vb.on("chat", function(player, text)
			player:give({ item = 2, count = 5 })
			player:damage(100, "test")
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
	client.take_chat_messages(); // drain join-system lines

	client.send_chat("hit me");
	pump(6);

	// The handler's chosen respawn: custom heal, custom position, custom
	// message, and the pre-existing inventory got dropped instead of kept.
	const auto srv = server.player_move_state(a_id);
	REQUIRE(srv.has_value());
	CHECK(srv->position.x == doctest::Approx(1.0));
	CHECK(srv->position.y == doctest::Approx(2.0));
	CHECK(srv->position.z == doctest::Approx(3.0));
	CHECK(client.inventory().empty()); // given 5 stone, then dropped on death

	bool saw_custom_msg = false;
	for (const auto &m : client.take_chat_messages()) {
		if (m == "* custom respawn") {
			saw_custom_msg = true;
		}
	}
	CHECK(saw_custom_msg);

	REQUIRE(rt.load_pack_file(R"(
		assert(seen_cause == "test")
		assert(seen_health_before == 20.0)
	)"));
}

TEST_CASE("vb.register_entity + vb.world.spawn: self persists across on_tick, "
		  "on_hit/on_death fire, and the instance replicates to a client") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();

	vb::script::PackRuntime rt(net.server(), registry, temp_storage("entity_kind"));
	REQUIRE(rt.load_pack_file(R"(
		spawn_count = 0
		hit_amount = nil
		hit_cause = nil
		death_cause = nil
		instance = nil

		vb.register_entity({
			name = "test:slime",
			on_spawn = function(self)
				self.hp = 10
				spawn_count = spawn_count + 1
			end,
			on_tick = function(self, dt)
				self.hp = self.hp + dt
			end,
			on_hit = function(self, amount, cause)
				hit_amount = amount
				hit_cause = cause
			end,
			on_death = function(self, cause)
				death_cause = cause
			end,
		})

		vb.on("chat", function(player, text)
			if text == "spawn" then
				instance = vb.world.spawn("test:slime", { x = 5, y = 5, z = 5 })
			elseif text == "hit" then
				instance:damage(3, "punch")
			elseif text == "kill" then
				instance:remove("script")
			end
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
	server.set_player_state(client.join_accept()->your_net_id, Vec3d{ 5, 5, 7 });

	client.send_chat("spawn");
	pump(6);
	REQUIRE(rt.load_pack_file(R"(
		assert(spawn_count == 1)
		assert(instance ~= nil)
		-- on_tick has already fired a few times by now (spawn happened partway
		-- through this pump), so hp is > 10, not exactly 10 -- remember it as
		-- the baseline for the persistence check below.
		assert(instance.hp > 10)
		hp_after_spawn = instance.hp
	)"));

	// Every subsequent pumped tick calls on_tick(self, dt) and self.hp keeps
	// accumulating past its own baseline -- proves `self` is the *same*
	// persistent table across calls, not a fresh one rebuilt each dispatch
	// (unlike PlayerHandle).
	pump(10);
	REQUIRE(rt.load_pack_file(R"( assert(instance.hp > hp_after_spawn) )"));

	// Replicated to the client through the same interest-grid path as a
	// dropped item -- no dedicated wire message.
	CHECK_FALSE(client.remote_entities().empty());

	client.send_chat("hit");
	pump(6);
	REQUIRE(rt.load_pack_file(R"(
		assert(hit_amount == 3)
		assert(hit_cause == "punch")
	)"));

	client.send_chat("kill");
	pump(6);
	REQUIRE(rt.load_pack_file(R"( assert(death_cause == "script") )"));
	CHECK(client.remote_entities().empty());
}

#endif // VB_WITH_LUA
