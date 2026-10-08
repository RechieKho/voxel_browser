// Phase 5.1: loads the real `content/base` files (not inline Lua strings
// like pack_runtime_test.cpp) through the same vb::script::load_content_pack
// path src/server/main.cpp uses. Guards against a future content edit
// breaking the shipped pack -- nothing else in the test suite parses these
// files.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/protocol/input.hpp"
#include "vb/script/pack_loader.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/world/block.hpp"

#if VB_WITH_LUA
#include "content_base_fixture.hpp"
#endif

#if VB_WITH_COMPRESSION
#include <vector>

#include "vb/assetsync/manifest.hpp"
#endif

namespace {

#if !VB_WITH_LUA
std::filesystem::path base_pack_dir() {
	return std::filesystem::path(VB_PROJECT_SOURCE_DIR) / "content" / "base";
}
#endif

std::filesystem::path temp_storage(const char *name) {
	auto p = std::filesystem::temp_directory_path() /
			(std::string("vb_content_pack_test_") + name + ".json");
	std::filesystem::remove(p);
	return p;
}

} // namespace

#if !VB_WITH_LUA

TEST_CASE("load_content_pack degrades gracefully without VB_WITH_LUA") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("disabled"));
	CHECK(vb::script::load_content_pack(rt, base_pack_dir()));
}

#else

TEST_CASE("content/base loads cleanly and re-declares the base blocks by name") {
	// The base() set's own size, computed before BasePackFixture loads
	// content/base into its own registry instance.
	const std::size_t base_size = vb::world::BlockRegistry::base().size();

	vb::test::BasePackFixture fixture("loads_cleanly");

	// blocks/*.lua re-declare exactly the Phase 2 base() set by name (no new
	// ids from those -- add_or_get is idempotent), plus two genuinely new
	// crafted-only blocks (planks, sticks; see crafting.lua) that don't
	// exist in BlockRegistry::base() at all.
	CHECK(fixture.registry().size() == base_size + 2);
	for (const char *name : { "base:dirt", "base:grass", "base:stone",
				 "base:sand", "base:wood", "base:leaves", "base:planks",
				 "base:sticks" }) {
		const vb::core::BlockId id = fixture.registry().find(name);
		CHECK(id != vb::world::base_block::air);
	}
}

TEST_CASE("content/base crafting: wood -> planks -> sticks via /craft chat") {
	vb::test::BasePackFixture fixture("craft");

	fixture.give("base:wood", 2);
	REQUIRE(fixture.count_of("base:wood") == 2);

	// Missing ingredients: crafting sticks needs planks, which we don't have
	// yet -- should fail cleanly (no inventory change) with a feedback line.
	fixture.chat("/craft base:sticks");
	CHECK(fixture.count_of("base:planks") == 0);
	CHECK(fixture.count_of("base:sticks") == 0);
	CHECK(fixture.last_message() == "Missing ingredients for base:sticks");

	// Craft planks from wood: 1 wood in, 4 planks out.
	fixture.chat("/craft base:planks");
	CHECK(fixture.count_of("base:wood") == 1);
	CHECK(fixture.count_of("base:planks") == 4);
	CHECK(fixture.last_message() == "Crafted base:planks");

	// Craft sticks from planks: 2 planks in, 4 sticks out.
	fixture.chat("/craft base:sticks");
	CHECK(fixture.count_of("base:planks") == 2);
	CHECK(fixture.count_of("base:sticks") == 4);
	CHECK(fixture.last_message() == "Crafted base:sticks");

	// Unknown recipe name: rejected, no side effects.
	fixture.chat("/craft base:does-not-exist");
	CHECK(fixture.count_of("base:planks") == 2);
	CHECK(fixture.count_of("base:sticks") == 4);
	CHECK(fixture.last_message() == "Unknown recipe: base:does-not-exist");

	// A normal chat message (not a /craft command) still broadcasts as chat.
	fixture.chat("hello");
	CHECK(fixture.last_message() == "A: hello");
}

// REMAINING_TASKS.md's "No mob damage" gap: entities/zombie.lua is the
// first real content/base user of entity:damage()/player:damage() beyond
// fall damage/PvP -- a hostile mob that chases down and bites whoever
// spawned it. No WorldReplicator needed at all: spawn_script_entity()
// (src/net/session.cpp) has no world dependency, matching the crafting
// test just above's own minimal setup.
//
// A real, reproducible engine bug was found while writing this (see
// STATE.md's own entry): a PlayerHandle stored across calls and read back
// from inside vb.on("tick", ...) or a script entity's own on_tick returns
// garbage (wrong session pointer, wrong net_id) -- calling a method on the
// *freshly passed-in* PlayerHandle argument from within the SAME handler
// call is fine (every existing content/base script already does exactly
// that), only *storing it for later, cross-call use from that specific
// dispatch path* is broken. zombie.lua's chase/attack logic is therefore
// driven from vb.on("player_input", ...) instead of the zombie's own
// on_tick -- that handler already hands over a fresh PlayerHandle every
// call (net::ServerSession::system_network_io's real per-input dispatch,
// same path chat/crafting already use safely), used immediately, never
// stored. Only the zombie's own entity handle (self:get_pos()/set_pos(),
// which never touches a PlayerHandle at all) is stored across calls.
TEST_CASE("content/base zombie: /zombie spawns a hostile mob that chases "
		"down and kills the caller") {
	using vb::core::NetId;
	using vb::core::Vec3d;
	using vb::protocol::InputCmd;

	vb::test::BasePackFixture fixture("zombie");

	std::optional<std::string> death_cause;
	fixture.server().set_respawn_handler([&](NetId id, std::string_view cause, float) {
		death_cause = std::string(cause);
		return vb::net::ServerSession::RespawnDecision{
			20.0f, fixture.server().spawn_point(id), "" };
	});

	// Fly mode disables gravity (same reason netcode_test.cpp's own void-kill
	// test enables it before a long pump window) -- otherwise the player
	// would free-fall past void_kill_y_ (-64 by default) well before the
	// zombie's bites add up.
	vb::physics::MoveParams fly;
	fly.fly = true;
	fixture.server().set_move_params(fly);
	fixture.server().set_player_state(fixture.player_id(), Vec3d{ 0, 64, 0 });
	fixture.pump(2);
	fixture.chat("/zombie");
	CHECK(fixture.last_message() == "[base] a zombie is hunting you");

	// The chase/attack logic only runs on a real InputCmd (vb.on
	// ("player_input", ...) needs one to fire), so this pushes a real
	// (zero-move -- fly mode holds the player still regardless) idle
	// command every tick, the same way a real client continuously streams
	// input even while standing still. The zombie closes a 3m gap at 2m/s
	// (~1.5s) then bites for 2 dmg every 1s -- killing a full-health (20
	// hp) player takes ~11.5s of real time in the worst case; 300 ticks
	// (15s) is comfortable headroom, still instant in test time (no real
	// sleeps).
	InputCmd idle;
	idle.dt = 0.05f;
	std::uint32_t seq = 1;
	for (int i = 0; i < 300; ++i) {
		idle.seq = seq++;
		fixture.send_input(idle);
		fixture.pump(1);
	}

	REQUIRE(death_cause.has_value());
	CHECK(*death_cause == "zombie");
}

TEST_CASE("load_content_pack wires up require() over the pack's own directory tree") {
	const std::filesystem::path pack =
			std::filesystem::temp_directory_path() / "vb_content_pack_test_require";
	std::error_code ec;
	std::filesystem::remove_all(pack, ec);
	std::filesystem::create_directories(pack / "lib");
	{
		std::ofstream out(pack / "lib" / "util.lua");
		out << "return { double = function(x) return x * 2 end }";
	}
	{
		std::ofstream out(pack / "init.lua");
		out << "local util = require('lib.util')\n"
			   "vb.storage.doubled = util.double(21)\n";
	}

	{
		vb::net::LoopbackNetwork net;
		vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
		vb::script::PackRuntime rt(net.server(), registry, pack / "storage.json");
		REQUIRE(vb::script::load_content_pack(rt, pack));
		rt.freeze();
		REQUIRE(rt.storage_dirty());
		rt.flush_storage();
	}

	std::ifstream f(pack / "storage.json");
	REQUIRE(f);
	std::ostringstream ss;
	ss << f.rdbuf();
	CHECK(ss.str().find("42") != std::string::npos);

	std::filesystem::remove_all(pack, ec);
}

TEST_CASE("load_content_pack fails on a pack directory with a broken Lua file") {
	const std::filesystem::path broken =
			std::filesystem::temp_directory_path() / "vb_content_pack_test_broken";
	std::filesystem::remove_all(broken);
	std::filesystem::create_directories(broken / "blocks");
	{
		std::ofstream out(broken / "blocks" / "bad.lua");
		out << "this is not valid lua {{{";
	}

	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("broken"));
	CHECK_FALSE(vb::script::load_content_pack(rt, broken));

	std::filesystem::remove_all(broken);
}

TEST_CASE("load_content_pack never runs the pack-root auth.lua as pack code") {
	const auto pack = std::filesystem::temp_directory_path() / "vb_content_pack_test_auth_lua";
	std::error_code ec;
	std::filesystem::remove_all(pack, ec);
	std::filesystem::create_directories(pack);
	{
		// Would fail the whole pack load if the walk executed it.
		std::ofstream f(pack / "auth.lua", std::ios::binary);
		f << "error('auth.lua must not run in the pack VM')";
	}
	{
		std::ofstream f(pack / "init.lua", std::ios::binary);
		f << "assert(not pcall(require, 'auth'))";
	}
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("auth_lua"));
	CHECK(vb::script::load_content_pack(rt, pack));
	std::filesystem::remove_all(pack, ec);
}

#if VB_WITH_COMPRESSION

// The 2026-09-18 bug: storage.json used to be in the asset manifest, so a
// vb.storage write that reached disk after the manifest was hashed (init.lua's
// write flushed on the first tick, or any later write) made every join fail
// its hash check; flush_storage() before build_manifest only covered the
// load-time half. storage.json is server runtime state now and never in the
// manifest at all, so no write order can make it stale.
TEST_CASE("vb.storage writes never change the asset manifest, before or after a flush") {
	const std::filesystem::path pack =
			std::filesystem::temp_directory_path() / "vb_content_pack_test_storage_race";
	std::error_code ec;
	std::filesystem::remove_all(pack, ec);
	std::filesystem::create_directories(pack);
	{
		std::ofstream out(pack / "init.lua");
		out << "vb.storage.boot_count = (vb.storage.boot_count or 0) + 1\n";
	}
	{
		// A previous boot's storage.json is already on disk.
		std::ofstream out(pack / "storage.json");
		out << "{\"boot_count\":1}";
	}

	{
		// Scoped so `rt` (and its open `vb.db`/storage handles) are torn down
		// before the trailing cleanup below tries to delete `pack` -- Windows
		// refuses to remove a directory containing a file another handle in
		// this same process still has open.
		vb::net::LoopbackNetwork net;
		vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
		vb::script::PackRuntime rt(net.server(), registry, pack / "storage.json");
		REQUIRE(vb::script::load_content_pack(rt, pack));
		rt.freeze();

		const auto before_flush = vb::assetsync::build_manifest(pack);
		REQUIRE(before_flush);
		CHECK(rt.storage_dirty()); // init.lua's write hasn't hit disk yet
		rt.flush_storage();
		const auto after_flush = vb::assetsync::build_manifest(pack);
		REQUIRE(after_flush);

		CHECK(before_flush->manifest_hash == after_flush->manifest_hash);
		for (const auto &e : after_flush->entries) {
			CHECK(e.path != "storage.json");
		}
	}

	std::filesystem::remove_all(pack, ec);
}

#endif // VB_WITH_COMPRESSION

#endif
