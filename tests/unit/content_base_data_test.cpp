// Phase C0/C1 (docs/content-base-testing.md): Layer 1 coverage of
// content/base's declarative surface -- block properties, textures,
// biomes, and entity registrations -- read back from the frozen registry
// after a real pack load, via BasePackFixture (content_base_fixture.hpp).

#include <doctest/doctest.h>

#include "content_base_fixture.hpp"

#if VB_WITH_LUA

#include <filesystem>

using vb::test::BasePackFixture;

TEST_CASE("content/base fixture: smoke -- pack loads, player joins") {
	BasePackFixture fx("c0_smoke");
	CHECK(fx.player_id() != vb::core::NetId::kInvalid);
	CHECK(fx.client().joined());
	CHECK(fx.registry().find("base:stone") != vb::world::base_block::air);
}

namespace {

struct ExpectedBlock {
	const char *name;
	bool solid;
	bool opaque;
	bool liquid;
};

} // namespace

TEST_CASE("content/base block properties match the declared table") {
	BasePackFixture fx("c1_block_props");
	auto &reg = fx.registry();

	// Every block content/base registers, plus the two deliberate oddities
	// called out in docs/content-base-testing.md: base:leaves is solid but
	// NOT opaque, base:sticks is neither, base:water is liquid/non-solid/
	// non-opaque.
	const ExpectedBlock table[] = {
		{ "base:dirt", true, true, false },
		{ "base:grass", true, true, false },
		{ "base:stone", true, true, false },
		{ "base:sand", true, true, false },
		{ "base:wood", true, true, false },
		{ "base:leaves", true, false, false },
		{ "base:planks", true, true, false },
		{ "base:sticks", false, false, false },
		{ "base:water", false, false, true },
	};

	for (const auto &b : table) {
		const auto id = reg.find(b.name);
		REQUIRE_MESSAGE(id != vb::world::base_block::air, b.name);
		const auto &type = reg.get(id);
		CHECK_MESSAGE(type.solid == b.solid, b.name << " solid");
		CHECK_MESSAGE(type.opaque == b.opaque, b.name << " opaque");
		CHECK_MESSAGE(type.liquid == b.liquid, b.name << " liquid");
	}
}

TEST_CASE("content/base base:water keeps region = true") {
	BasePackFixture fx("c1_water_region");
	auto &reg = fx.registry();
	const auto id = reg.find("base:water");
	REQUIRE(id != vb::world::base_block::air);
	CHECK(reg.get(id).region);

	// No other block opts into region tracking.
	for (const char *name : { "base:dirt", "base:grass", "base:stone",
				 "base:sand", "base:wood", "base:leaves", "base:planks",
				 "base:sticks" }) {
		CHECK_FALSE(reg.get(reg.find(name)).region);
	}
}

TEST_CASE("content/base block textures resolve to real files under content/base/") {
	BasePackFixture fx("c1_textures");
	auto &reg = fx.registry();

	const char *textured[] = { "base:stone", "base:water" };
	for (const char *name : textured) {
		const auto id = reg.find(name);
		REQUIRE(id != vb::world::base_block::air);
		const std::string &texture = reg.get(id).texture;
		REQUIRE_MESSAGE(!texture.empty(), name);
		CHECK_FALSE(texture.starts_with('/'));
		const auto path = vb::test::content_base_dir() / texture;
		CHECK_MESSAGE(std::filesystem::exists(path), path.string());
	}

	// Every other block carries no texture (flat-color fallback).
	for (const char *name : { "base:dirt", "base:grass", "base:sand",
				 "base:wood", "base:leaves", "base:planks", "base:sticks" }) {
		CHECK(reg.get(reg.find(name)).texture.empty());
	}
}

TEST_CASE("content/base entity textures resolve to real files under content/base/") {
	for (const char *texture : { "textures/player.png", "textures/dropped_item.png" }) {
		const auto path = vb::test::content_base_dir() / texture;
		CHECK_MESSAGE(std::filesystem::exists(path), path.string());
	}
}

TEST_CASE("content/base biomes register with the right block names") {
	// vb.register_biome has no PackRuntime read-back accessor of its own, so
	// this observes each registration call by wrapping vb.register_biome in a
	// prelude loaded before content/base's own biomes/*.lua files (same Lua
	// global, so they see the wrapped version), recording every call's table
	// into a Lua global, and asserting on it after load_content_pack().
	// (content/base's pipeline, covered by the next test case, is the other
	// way to see them.)
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(
			net.server(), registry, vb::test::content_base_storage("c1_biomes"));
	REQUIRE(rt.load_pack_file(R"(
		local real_register_biome = vb.register_biome
		local captured = {}
		vb.register_biome = function(def)
			table.insert(captured, {
				name = def.name, surface = def.surface, filler = def.filler,
				stone = def.stone, decoration = def.decoration,
			})
			return real_register_biome(def)
		end
		_G.__captured_biomes = captured
	)"));
	REQUIRE(vb::script::load_content_pack(rt, vb::test::content_base_dir()));
	REQUIRE(rt.load_pack_file(R"(
		local by_name = {}
		for _, b in ipairs(__captured_biomes) do
			by_name[b.name] = b
		end
		local plains = by_name["base:plains"]
		assert(plains, "base:plains never registered")
		assert(plains.surface == "base:grass")
		assert(plains.filler == "base:dirt")
		assert(plains.stone == "base:stone")

		local forest = by_name["base:forest"]
		assert(forest, "base:forest never registered")
		assert(forest.surface == "base:grass")
		assert(forest.filler == "base:dirt")
		assert(forest.stone == "base:stone")
		assert(forest.decoration == "trees")
	)"));
}

TEST_CASE("content/base base:dropped_item's represents tags a real drop's visual kind") {
	BasePackFixture fx("c1_visual_kind_drop");
	// The one thing entities/dropped_item.lua exists for: `represents =
	// "item_drop"` reaching ServerSession::set_item_drop_visual_kind() via
	// PackRuntime::attach_session(), so every real spawn_item_drop() tags
	// its EntityRecord::kind with base:dropped_item's registered kind
	// rather than the hardcoded world::kItemDropKind sentinel.
	//
	// A few blocks away (inside the default interest radius, but outside
	// ItemDropSystem's ~1.5m pickup radius) so the drop shows up as a
	// replicated entity instead of being auto-collected before the first
	// snapshot ever reports it.
	auto feet = fx.server().player_move_state(fx.player_id())->position;
	feet.x += 5.0;
	fx.server().spawn_item_drop(feet, fx.registry().find("base:stone"), 1);
	fx.pump(4);

	bool found = false;
	for (const auto &[id, rec] : fx.client().remote_entities()) {
		(void)id;
		const auto *kind = fx.client().entity_kind(rec.kind);
		if (kind != nullptr && kind->name == "base:dropped_item") {
			found = true;
			break;
		}
	}
	CHECK(found);
}

TEST_CASE("content/base base:player's represents tags a joined player's visual kind") {
	BasePackFixture fx("c1_visual_kind_player");
	// Mirror of the drop test above, for entities/player.lua's
	// `represents = "player"` -> ServerSession::set_player_visual_kind().
	// Needs a second client to observe the first player's own replicated
	// EntityRecord (a client never sees itself in remote_entities()).
	auto &second = fx.add_client("B");
	fx.pump(8);

	bool found = false;
	for (const auto &[id, rec] : second.remote_entities()) {
		if (id != fx.player_id()) {
			continue;
		}
		const auto *kind = second.entity_kind(rec.kind);
		if (kind != nullptr && kind->name == "base:player") {
			found = true;
		}
	}
	CHECK(found);
}

#endif // VB_WITH_LUA

TEST_CASE("content/base's worldgen pipeline: two biomes, sand beaches, and a dry-land spawn") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, vb::test::content_base_storage("worldgen_pipeline"));
	REQUIRE(vb::script::load_content_pack(rt, vb::test::content_base_dir()));
	rt.freeze();
	const auto valid = rt.validate_worldgen();
	REQUIRE_MESSAGE(valid, valid.message);

	vb::worldgen::WorldGenParams params;
	params.seed = 20260705;
	const auto pipeline = rt.build_worldgen_pipeline(params);
	REQUIRE(pipeline != nullptr);
	CHECK(pipeline->biomes.biome_count() == 2);
	// biomes/*.lua load in sorted order: forest, then plains.
	CHECK(pipeline->biomes.biome(0).name == "base:forest");
	CHECK(pipeline->biomes.biome(1).name == "base:plains");
	CHECK(pipeline->sea_level == 62);
	CHECK(pipeline->soil_depth == 4);
	CHECK(pipeline->beach == registry.find("base:sand"));

	// Terrain keeps the fixed default's shape: surface heights in a similar
	// range, grass above the beach line and sand at it.
	const vb::worldgen::WorldGenerator gen(params, registry, pipeline);
	int lowest = 1000;
	int highest = -1000;
	bool saw_grass = false;
	bool saw_sand = false;
	for (int x = -512; x < 512; x += 16) {
		for (int z = -512; z < 512; z += 16) {
			const int h = gen.surface_height(x, z);
			lowest = std::min(lowest, h);
			highest = std::max(highest, h);
			const auto top = gen.block_at_pregen(x, h, z);
			saw_grass = saw_grass || top == registry.find("base:grass");
			saw_sand = saw_sand || top == registry.find("base:sand");
			if (h > pipeline->sea_level + 1) {
				CHECK(top != registry.find("base:sand"));
			}
		}
	}
	CHECK(lowest >= 64 - 29);
	CHECK(highest <= 64 + 29);
	CHECK(saw_grass);
	CHECK(saw_sand);

	// The player spawns on dry land.
	const auto spawn = vb::worldgen::default_spawn_position(gen);
	CHECK(gen.surface_height(static_cast<int>(std::floor(spawn.x)), static_cast<int>(std::floor(spawn.z))) >
			pipeline->sea_level);
}
