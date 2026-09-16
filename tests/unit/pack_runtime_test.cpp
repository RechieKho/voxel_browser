#include <doctest/doctest.h>

#include <ostream>

#include <filesystem>
#include <string>

#include "vb/net/loopback.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/world/block.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/worker_pool.hpp"

#if !VB_WITH_LUA

TEST_CASE("PackRuntime reports kDisabled when built without VB_WITH_LUA") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, "vb_disabled_storage.json");
	const auto r = rt.load_pack_file("return 1");
	CHECK_FALSE(r);
	CHECK(r.error == vb::core::ScriptError::kDisabled);
}

#else

namespace {

std::filesystem::path temp_storage(const char *name) {
	auto p = std::filesystem::temp_directory_path() /
			(std::string("vb_pack_runtime_test_") + name + ".json");
	std::filesystem::remove(p);
	return p;
}

// World + WorldReplicator fixture for the vb.world.* tests (attach_world()
// needs a real WorldReplicator).
struct Fixture {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt;
	vb::world::World world;
	vb::worldgen::WorldGenerator gen;
	vb::worldgen::WorldGenWorkerPool pool;
	vb::net::WorldReplicator replicator;

	explicit Fixture(const std::filesystem::path &storage_path)
			: rt(net.server(), registry, storage_path),
			  world(registry),
			  gen(vb::worldgen::WorldGenParams{}, registry),
			  pool(gen, vb::worldgen::WorldGenWorkerPool::kSynchronous),
			  replicator(world, pool, registry, /*view*/ 1, /*vview*/ 1) {
		rt.attach_world(replicator);
	}
};

} // namespace

TEST_CASE("vb.register_block is idempotent and rejected after freeze") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("register"));

	const auto r1 = rt.load_pack_file(R"(
		id1 = vb.register_block({ name = "test:glow", light = 15 })
		id2 = vb.register_block({ name = "test:glow" })
		assert(id1 == id2)
	)");
	REQUIRE(r1);
	CHECK(registry.find("test:glow") != vb::core::BlockId::kAir);

	rt.freeze();

	const auto r2 = rt.load_pack_file(R"(vb.register_block({ name = "test:late" }))");
	CHECK_FALSE(r2);
}

TEST_CASE("vb.register_entity captures its callback table without dispatching") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("entity"));

	const auto r = rt.load_pack_file(R"(
		id = vb.register_entity({ name = "test:slime",
			on_spawn = function() end, on_tick = function() end,
			on_hit = function() end, on_death = function() end })
		assert(id == 1)
		id2 = vb.register_entity({ name = "test:slime" })
		assert(id == id2)
	)");
	REQUIRE(r);
}

TEST_CASE("vb.register_biome / vb.register_craft accept arbitrary def tables") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("biome"));

	const auto r = rt.load_pack_file(R"(
		vb.register_biome({ name = "test:plains", surface = "test:grass" })
		vb.register_craft({ inputs = { "test:wood" }, output = "test:planks" })
	)");
	REQUIRE(r);
}

TEST_CASE("player:take() removes items across slots, all-or-nothing") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("take"));

	// dispatch_chat() fires vb.on("chat", ...) directly, no ServerSession/
	// attach_session() needed -- give()/take()/get_inventory() only touch
	// PackRuntime::Impl's own inventory map, not the network layer (only
	// send_message/open_ui/get_name need a real session). The handler
	// returns the outcome directly (rather than `assert`ing internally and
	// always returning true) so a wrong result fails the CHECK below instead
	// of being swallowed as a warned Lua error by run_veto's error handling.
	REQUIRE(rt.load_pack_file(R"(
		vb.on("chat", function(player, text)
			if text == "setup" then
				-- Two separate slots holding the same item, like give()
				-- naturally produces when called more than once.
				player:give({ item = 5, count = 3 })
				player:give({ item = 5, count = 4 })
				return true
			elseif text == "take-too-many" then
				return player:take({ item = 5, count = 100 })
			elseif text == "take-other-item" then
				return player:take({ item = 9, count = 1 })
			elseif text == "take-some" then
				return player:take({ item = 5, count = 5 })
			elseif text == "check-remaining" then
				local total = 0
				for _, s in ipairs(player:get_inventory()) do
					if s.item == 5 then total = total + s.count end
				end
				return total == 2
			end
			return true
		end)
	)"));
	rt.freeze();

	const vb::core::NetId id{ 1 };
	CHECK(rt.dispatch_chat(id, "setup"));
	CHECK_FALSE(rt.dispatch_chat(id, "take-too-many")); // over-request: false, no change
	CHECK_FALSE(rt.dispatch_chat(id, "take-other-item")); // wrong item: false, no change
	CHECK(rt.dispatch_chat(id, "take-some")); // 5 of 7 total across 2 slots
	CHECK(rt.dispatch_chat(id, "check-remaining")); // 2 left
}

TEST_CASE("vb.world.get_block/set_block operate on the attached world") {
	Fixture f(temp_storage("world_blocks"));

	const auto r = f.rt.load_pack_file(R"(
		assert(vb.world.get_block(0, 500, 0) == 0) -- unloaded -> air
		vb.world.set_block(0, 5, 0, 1) -- base:stone
		assert(vb.world.get_block(0, 5, 0) == 1)
	)");
	REQUIRE(r);

	const auto r2 = f.rt.load_pack_file("vb.world.set_block(0, 5, 0, 999)");
	CHECK_FALSE(r2);
}

TEST_CASE("vb.world.raycast finds the first solid voxel") {
	Fixture f(temp_storage("raycast"));
	f.world.set_block({ 0, 5, 0 }, vb::world::base_block::stone);

	const auto r = f.rt.load_pack_file(R"(
		local hit = vb.world.raycast({ x = 0.5, y = 10.5, z = 0.5 },
				{ x = 0, y = -1, z = 0 }, 20)
		assert(hit ~= nil)
		assert(hit.x == 0 and hit.y == 5 and hit.z == 0)
	)");
	REQUIRE(r);

	const auto miss = f.rt.load_pack_file(R"(
		local hit = vb.world.raycast({ x = 100.5, y = 10.5, z = 100.5 },
				{ x = 0, y = -1, z = 0 }, 20)
		assert(hit == nil)
	)");
	REQUIRE(miss);
}

TEST_CASE("vb.world.spawn on an unregistered kind is a no-op, returns nil") {
	Fixture f(temp_storage("spawn"));

	const auto r = f.rt.load_pack_file(R"(
		local result = vb.world.spawn("unknown_kind", { x = 0, y = 0, z = 0 })
		assert(result == nil)
	)");
	REQUIRE(r);
}

TEST_CASE("vb.on('tick') fires with dt; vb.after/vb.every fire on schedule") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("timers"));

	const auto r = rt.load_pack_file(R"(
		ticks = 0
		vb.on("tick", function(dt) ticks = ticks + 1 end)
		after_fired = 0
		vb.after(1.0, function() after_fired = after_fired + 1 end)
		every_fired = 0
		vb.every(0.5, function() every_fired = every_fired + 1 end)
	)");
	REQUIRE(r);

	rt.dispatch_tick(0.4);
	rt.dispatch_tick(0.4);
	rt.dispatch_tick(0.4); // 1.2s elapsed

	const auto r2 = rt.load_pack_file(R"(
		assert(ticks == 3)
		assert(after_fired == 1)
		assert(every_fired == 2)
	)");
	REQUIRE(r2);
}

TEST_CASE("a timer handler that errors doesn't crash dispatch_tick") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("timer_error"));

	const auto r = rt.load_pack_file(
			R"(vb.after(0.1, function() error("boom") end))");
	REQUIRE(r);
	rt.dispatch_tick(0.2); // must not throw / hang
}

TEST_CASE("vb.storage persists across PackRuntime instances") {
	const auto storage = temp_storage("storage_persist");
	{
		vb::net::LoopbackNetwork net;
		vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
		vb::script::PackRuntime rt(net.server(), registry, storage);
		const auto r = rt.load_pack_file(
				R"(vb.storage.count = 42; vb.storage.name = "hello")");
		REQUIRE(r);
		CHECK(rt.storage_dirty());
		rt.flush_storage();
		CHECK_FALSE(rt.storage_dirty());
	}
	{
		vb::net::LoopbackNetwork net;
		vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
		vb::script::PackRuntime rt(net.server(), registry, storage);
		const auto r = rt.load_pack_file(R"(
			assert(vb.storage.count == 42)
			assert(vb.storage.name == "hello")
		)");
		REQUIRE(r);
	}
	std::filesystem::remove(storage);
}

TEST_CASE("player_leave dispatch without an attached session doesn't crash "
		"a send_message call") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("leave_nosession"));

	const auto r = rt.load_pack_file(
			R"(vb.on("player_leave", function(p) p:send_message("bye") end))");
	REQUIRE(r);

	vb::net::SessionPlayerLeft left;
	left.net_id = vb::core::NetId{ 5 };
	rt.dispatch_player_leave(left); // session never attached -> graceful no-op
}

TEST_CASE("a busy-looping tick handler hits the instruction budget instead "
		"of hanging") {
	vb::script::VmLimits limits;
	limits.instruction_budget = 200'000;
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry,
			temp_storage("tick_budget"), limits);

	const auto r = rt.load_pack_file(
			R"(vb.on("tick", function() while true do end end))");
	REQUIRE(r);
	rt.dispatch_tick(0.016); // returns instead of hanging
}

#endif // VB_WITH_LUA
