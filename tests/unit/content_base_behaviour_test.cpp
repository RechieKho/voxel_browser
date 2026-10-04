// Phase C2 (docs/content-base-testing.md): Layer 1 coverage of
// content/base's server-side *behaviour* -- block drops, crafting,
// mechanics.lua's rising-edge punch/place, fall damage, keybinds, and
// vb.storage persistence -- via BasePackFixture
// (content_base_fixture.hpp).

#include <doctest/doctest.h>

#include "content_base_fixture.hpp"

#if VB_WITH_LUA

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using vb::test::BasePackFixture;
using vb::core::IVec3;
using vb::protocol::kInputPrimary;
using vb::protocol::kInputSecondary;

TEST_CASE("content/base every block with on_break drops itself") {
	// base:grass (drops dirt) and base:water (no on_break at all) are
	// deliberate exceptions, covered by their own test cases below.
	const char *self_dropping[] = { "base:dirt", "base:stone", "base:sand",
		"base:wood", "base:leaves", "base:planks", "base:sticks" };

	for (const char *name : self_dropping) {
		BasePackFixture fx("c2_drop");
		fx.with_world();
		const IVec3 pos{ 0, fx.surface_y(0, 0) + 1, 0 };
		REQUIRE_MESSAGE(fx.place(pos, name), name);
		REQUIRE_MESSAGE(fx.break_block(pos), name);
		// The player is standing right on top of the broken block (place()/
		// break_block() both set feet to pos+0.5), well inside
		// ItemDropSystem's default pickup radius -- the drop is auto-
		// collected within a couple of ticks.
		CHECK_MESSAGE(fx.count_of(name) == 1, name);
	}
}

TEST_CASE("content/base base:grass drops base:dirt, not itself") {
	BasePackFixture fx("c2_grass_drops_dirt");
	fx.with_world();
	const IVec3 pos{ 0, fx.surface_y(0, 0) + 1, 0 };
	REQUIRE(fx.place(pos, "base:grass"));
	REQUIRE(fx.break_block(pos));
	CHECK(fx.count_of("base:dirt") == 1);
	CHECK(fx.count_of("base:grass") == 0);
}

TEST_CASE("content/base base:water has no on_break: breaking it drops nothing") {
	BasePackFixture fx("c2_water_no_drop");
	fx.with_world();
	const IVec3 pos{ 0, fx.surface_y(0, 0) + 1, 0 };
	REQUIRE(fx.place(pos, "base:water"));
	REQUIRE(fx.break_block(pos));
	CHECK(fx.count_of("base:water") == 0);
	CHECK(fx.client().inventory().empty());
}

TEST_CASE("content/base crafting: /craft prefix is vetoed even when the recipe fails, "
		"but a bare /craft with no argument passes through") {
	BasePackFixture fx("c2_craft_veto");

	fx.chat("/craft base:does-not-exist");
	{
		const auto msgs = fx.drain_messages();
		REQUIRE_FALSE(msgs.empty());
		// Vetoed: the raw command text itself must never appear as a
		// broadcast chat line (only crafting.lua's own feedback message).
		for (const auto &m : msgs) {
			CHECK(m != "A: /craft base:does-not-exist");
		}
		CHECK(msgs.back() == "Unknown recipe: base:does-not-exist");
	}

	// The pattern requires an argument ("^/craft%s+(%S+)$") -- a bare
	// "/craft" doesn't match, so crafting.lua returns `true` and the raw
	// text broadcasts normally, same as any other chat line.
	fx.chat("/craft");
	{
		const auto msgs = fx.drain_messages();
		REQUIRE_FALSE(msgs.empty());
		CHECK(msgs.back() == "A: /craft");
	}
}

TEST_CASE("content/base crafting is all-or-nothing: insufficient ingredients take nothing") {
	BasePackFixture fx("c2_craft_all_or_nothing");
	// base:sticks needs 2 planks; give exactly 1.
	fx.give("base:planks", 1);
	REQUIRE(fx.count_of("base:planks") == 1);

	fx.chat("/craft base:sticks");
	fx.drain_messages();

	CHECK(fx.count_of("base:planks") == 1); // untouched, not partially spent
	CHECK(fx.count_of("base:sticks") == 0);
}

TEST_CASE("content/base mechanics.lua: punch fires on the rising edge only") {
	BasePackFixture fx("c2_punch_edge");

	// A block with max_damage > 1 so repeated hits are observable --
	// content/base's own blocks all break on the first punch
	// (max_damage == 0, see mechanics.lua's header comment), which can't
	// distinguish "one punch per press" from "one punch per tick held".
	// Added directly through the shared BlockRegistry (bypasses Lua/
	// PackRuntime's own frozen-after-load guard entirely -- this is a pure
	// C++ BlockType, no on_break hook, not meant to be pack-visible). Must
	// land in the registry *before* with_world(), which copies it into the
	// World it constructs (see World's constructor) -- added afterward
	// would leave the World's own copy unaware of this id.
	vb::world::BlockType tough;
	tough.name = "test:tough";
	tough.solid = true;
	tough.opaque = true;
	tough.max_damage = 3;
	const auto tough_id = fx.registry().add(tough);
	fx.with_world();

	const IVec3 pos = fx.dry_target();
	REQUIRE(fx.place(pos, tough_id));
	const vb::core::Vec3d target{ pos.x + 0.5, pos.y + 0.5, pos.z + 0.5 };
	// Stand 3m back so the block is comfortably in reach (default 5.5).
	fx.server().set_player_state(fx.player_id(), { pos.x + 0.5, pos.y + 0.5, pos.z + 3.5 });
	fx.aim_at(target);

	// Edge 1: held 10 ticks == exactly one punch (1 < max_damage == 3).
	fx.press_aimed(target, kInputPrimary, 10);
	fx.release_aimed(target);
	CHECK(fx.registry().get(tough_id).solid); // world_.get_block below is the real check
	CHECK(fx.world_solid_at(pos));

	// Edge 2: another held press == a second punch (2 < 3).
	fx.press_aimed(target, kInputPrimary, 10);
	fx.release_aimed(target);
	CHECK(fx.world_solid_at(pos));

	// Edge 3: the third punch reaches max_damage and breaks it.
	fx.press_aimed(target, kInputPrimary, 10);
	fx.release_aimed(target);
	CHECK_FALSE(fx.world_solid_at(pos));
}

TEST_CASE("content/base mechanics.lua: placing spends exactly one unit of the held item") {
	BasePackFixture fx("c2_place_spend");
	fx.with_world();
	fx.give("base:planks", 3);
	REQUIRE(fx.count_of("base:planks") == 3);

	const IVec3 target_cell = fx.dry_target();
	// Looking straight down from 2m above the floor, same button, held 10
	// ticks -- place_block() only ever resolves against a raycast hit on a
	// *solid* surface (the floor), never the empty target cell directly.
	fx.stand_above_and_aim_down(target_cell, 2.0);
	const vb::core::Vec3d down{ target_cell.x + 0.5, target_cell.y - 0.5, target_cell.z + 0.5 };

	fx.press_aimed(down, kInputSecondary, 10); // held -- still one logical press
	fx.release_aimed(down);

	CHECK(fx.world_solid_at(target_cell));
	CHECK(fx.count_of("base:planks") == 2);
}

TEST_CASE("content/base mechanics.lua: an empty selected slot places nothing") {
	BasePackFixture fx("c2_place_empty_slot");
	fx.with_world();
	// No give() at all -- the player's inventory (and therefore
	// get_held_item()) is empty.
	const IVec3 target_cell = fx.dry_target();
	fx.stand_above_and_aim_down(target_cell, 2.0);
	const vb::core::Vec3d down{ target_cell.x + 0.5, target_cell.y - 0.5, target_cell.z + 0.5 };

	fx.press_aimed(down, kInputSecondary, 1);
	fx.release_aimed(down);

	CHECK_FALSE(fx.world_solid_at(target_cell));
}

TEST_CASE("content/base mechanics.lua: placing respects the engine's effective reach") {
	BasePackFixture fx("c2_place_reach");
	fx.with_world();
	fx.give("base:planks", 1);

	const IVec3 target_cell = fx.dry_target();
	// 4m above the floor: inside the default 5.5-block reach.
	fx.stand_above_and_aim_down(target_cell, 4.0);
	const vb::core::Vec3d down{ target_cell.x + 0.5, target_cell.y - 0.5, target_cell.z + 0.5 };

	// Narrow reach to 2m -- the same placement that would have succeeded
	// before now must be refused (mechanics.lua reads vb.action.get_params
	// ().reach, the exact value this overrides).
	REQUIRE(fx.server().world_replicator() != nullptr);
	fx.server().world_replicator()->set_reach(2.0);

	fx.press_aimed(down, kInputSecondary, 1);
	fx.release_aimed(down);

	CHECK_FALSE(fx.world_solid_at(target_cell));
	CHECK(fx.count_of("base:planks") == 1); // nothing spent on the refused attempt
}

TEST_CASE("content/base fall_damage.lua: the SAFE_SPEED curve") {
	// ServerSession::player_health() is a VB_WITH_AUTOMATION-only primitive
	// (docs/e2e-automation.md) -- there's no other way to read a player's
	// current HP from outside Lua (PlayerHandle has no get_health(), unlike
	// script entities' entity:get_health()). CI's dedicated Lua+automation
	// leg (.github/workflows/build_linux.yml) exercises this; a plain
	// VB_WITH_LUA build without automation skips it, same posture as
	// net_test.cpp's own VB_WITH_AUTOMATION-gated cases.
#if defined(VB_WITH_AUTOMATION)
	{
		BasePackFixture fx("c2_fall_safe_79");
		fx.with_world();
		fx.land(7.9);
		CHECK(fx.server().player_health(fx.player_id())->first == doctest::Approx(20.0f));
	}
	{
		// excess > 0 is strict: exactly SAFE_SPEED takes no damage either.
		BasePackFixture fx("c2_fall_safe_80");
		fx.with_world();
		fx.land(8.0);
		CHECK(fx.server().player_health(fx.player_id())->first == doctest::Approx(20.0f));
	}
	{
		BasePackFixture fx("c2_fall_12");
		fx.with_world();
		fx.land(12.0);
		// 1 HP per m/s above SAFE_SPEED (8.0): 12.0 - 8.0 == 4 HP.
		CHECK(fx.server().player_health(fx.player_id())->first == doctest::Approx(16.0f));
	}
#endif
}

TEST_CASE("content/base keybinds.lua: both keybinds open on the rising edge only") {
	BasePackFixture fx("c2_keybinds");
	const auto pause_bit = fx.keybind_bit("base:pause");
	const auto inv_bit = fx.keybind_bit("base:inventory");

	fx.press_keybind(pause_bit, 5); // held 5 ticks -- still one logical press
	auto open = fx.client().take_open_ui();
	REQUIRE(open.has_value());
	CHECK(open->ui_name == "base:pause");
	CHECK(open->ctx_json == "{}");
	// Still held: must not reopen (no second S2COpenUi queued).
	fx.press_keybind(pause_bit, 3);
	CHECK_FALSE(fx.client().take_open_ui().has_value());
	fx.release_keybind();

	fx.press_keybind(inv_bit, 1);
	open = fx.client().take_open_ui();
	REQUIRE(open.has_value());
	CHECK(open->ui_name == "base:inventory");
	fx.release_keybind();
}

TEST_CASE("content/base init.lua: vb.storage.boot_count persists across a restart") {
	const auto storage_path = vb::test::content_base_storage("c2_boot_count");
	std::filesystem::remove(storage_path);

	// load_pack_file() has no return-value channel back to C++, so read the
	// persisted value the same way the storage-race regression tests in
	// content_pack_test.cpp do: boot straight off the real on-disk
	// storage.json bytes after flush_storage().
	auto boot_once = [&]() {
		vb::net::LoopbackNetwork net;
		vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
		vb::script::PackRuntime rt(net.server(), registry, storage_path);
		REQUIRE(vb::script::load_content_pack(rt, vb::test::content_base_dir()));
		rt.freeze();
		// init.lua's boot_count formats as an integer via string.format("%d",
		// ...) in its own print, but vb.storage itself always round-trips a
		// Lua float through JSON (src/script/pack_runtime.cpp's json_to_lua)
		// -- assert that explicitly too, matching the file's own comment.
		REQUIRE(rt.load_pack_file(
				R"(assert(math.type(vb.storage.boot_count) == "float"))"));
		rt.flush_storage();
	};
	auto read_boot_count = [&]() -> double {
		std::ifstream f(storage_path);
		REQUIRE(f);
		std::ostringstream ss;
		ss << f.rdbuf();
		const std::string json = ss.str();
		const auto key = json.find("\"boot_count\"");
		REQUIRE(key != std::string::npos);
		const auto colon = json.find(':', key);
		REQUIRE(colon != std::string::npos);
		return std::stod(json.substr(colon + 1));
	};

	boot_once();
	const double first = read_boot_count();
	boot_once();
	const double second = read_boot_count();
	REQUIRE(first >= 1.0);
	CHECK(second == doctest::Approx(first + 1.0));
}

#endif // VB_WITH_LUA


