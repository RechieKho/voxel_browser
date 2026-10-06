#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "vb/world/block.hpp"
#include "vb/worldgen/structure.hpp"

#if VB_WITH_LUA
#include "vb/net/loopback.hpp"
#include "vb/script/data_script.hpp"
#include "vb/script/pack_loader.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/script/structure_def.hpp"
#include "vb/worldgen/generator.hpp"
#endif

namespace {

using namespace vb;

// A 3x2x3 structure with an asymmetric footprint, one keep cell, one explicit
// air cell, and two variants.
const char *kTree = R"(return {
	name = "test:tree",
	size = { x = 3, y = 2, z = 3 },
	anchor = { x = 1, y = 0, z = 1 },
	palette = {
		["."] = false,
		["_"] = "base:air",
		["W"] = "base:wood",
		["L"] = "base:leaves",
	},
	variants = {
		{ weight = 2, layers = {
			{ "...", ".W.", "..." },
			{ "LLL", "L_L", "LLL" },
		} },
		{ weight = 1, layers = {
			{ "...", ".W.", "..." },
			{ ".L.", "LLL", ".L." },
		} },
	},
	placement = { on = { "base:grass", "base:dirt" }, rotate = true, min_spacing = 5, cluster = 0.25 },
})";

std::filesystem::path scratch(const char *name) {
	auto dir = std::filesystem::temp_directory_path() / (std::string("vb_structure_") + name);
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	return dir;
}

void write(const std::filesystem::path &p, const std::string &text) {
	std::filesystem::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

worldgen::StructureSpec simple_spec() {
	worldgen::StructureSpec spec;
	spec.name = "test:rock";
	spec.size = { 2, 1, 2 };
	spec.palette['S'] = std::string("base:stone");
	spec.palette['.'] = std::nullopt;
	worldgen::StructureVariantSpec v;
	v.layers = { { "SS", "S." } };
	spec.variants.push_back(v);
	return spec;
}

} // namespace

TEST_CASE("validate_structure_spec accepts a good spec and names the structure in errors") {
	CHECK(worldgen::validate_structure_spec(simple_spec()).empty());

	auto bad_row = simple_spec();
	bad_row.variants[0].layers[0][1] = "S";
	auto msg = worldgen::validate_structure_spec(bad_row);
	CHECK(msg.find("test:rock") != std::string::npos);
	CHECK(msg.find("characters long, expected 2") != std::string::npos);

	auto bad_key = simple_spec();
	bad_key.variants[0].layers[0][0] = "SQ";
	CHECK(worldgen::validate_structure_spec(bad_key).find("'Q'") != std::string::npos);

	auto too_big = simple_spec();
	too_big.size.x = worldgen::kMaxStructureDim + 1;
	CHECK(worldgen::validate_structure_spec(too_big).find("size") != std::string::npos);

	auto bad_anchor = simple_spec();
	bad_anchor.anchor = { 2, 0, 0 };
	CHECK(worldgen::validate_structure_spec(bad_anchor).find("anchor") != std::string::npos);

	auto bad_layers = simple_spec();
	bad_layers.variants[0].layers.push_back({ "SS", "SS" });
	CHECK(worldgen::validate_structure_spec(bad_layers).find("layers") != std::string::npos);
}

TEST_CASE("resolve_structure maps names to ids, keeps and air, and rejects unknown blocks") {
	const world::BlockRegistry registry = world::BlockRegistry::base();
	auto spec = simple_spec();
	spec.palette['.'] = std::string("base:air");
	worldgen::StructureDef def;
	std::string error;
	REQUIRE_MESSAGE(worldgen::resolve_structure(spec, registry, def, error), error);
	REQUIRE(def.variants.size() == 1);
	const auto &cells = def.variants[0].cells;
	CHECK(cells.size() == 4);
	CHECK(cells[0] == registry.find("base:stone"));
	CHECK(cells[3] == core::BlockId::kAir);
	CHECK(def.radius_xz == 1);

	spec.palette['.'] = std::nullopt;
	REQUIRE(worldgen::resolve_structure(spec, registry, def, error));
	CHECK(def.variants[0].cells[3] == worldgen::kKeepCell);

	spec.palette['S'] = std::string("nope:missing");
	CHECK_FALSE(worldgen::resolve_structure(spec, registry, def, error));
	CHECK(error.find("test:rock") != std::string::npos);
	CHECK(error.find("nope:missing") != std::string::npos);
}

TEST_CASE("merge_placement lets overrides win and resolve_placement applies defaults") {
	const world::BlockRegistry registry = world::BlockRegistry::base();
	worldgen::PlacementSpec base;
	base.rotate = true;
	base.min_spacing = 7;
	base.on = std::vector<std::string>{ "base:grass" };
	worldgen::PlacementSpec over;
	over.min_spacing = 9;
	over.replace = worldgen::ReplacePolicy::kAll;
	const auto merged = worldgen::merge_placement(base, over);
	CHECK(merged.rotate == true);
	CHECK(merged.min_spacing == 9);

	worldgen::PlacementRule rule;
	std::string error;
	REQUIRE(worldgen::resolve_placement(3, 0.5, merged, registry, "ctx", rule, error));
	CHECK(rule.structure == 3);
	CHECK(rule.spawn_rate == 0.5);
	CHECK(rule.rotate);
	CHECK_FALSE(rule.mirror);
	CHECK(rule.min_spacing == 9);
	CHECK(rule.max_slope == worldgen::kDefaultMaxSlope);
	CHECK(rule.replace == worldgen::ReplacePolicy::kAll);
	REQUIRE(rule.on.size() == 1);
	CHECK(rule.on[0] == registry.find("base:grass"));

	base.on = std::vector<std::string>{ "no:such" };
	CHECK_FALSE(worldgen::resolve_placement(0, 1.0, base, registry, "biome 'x'", rule, error));
	CHECK(error.find("no:such") != std::string::npos);

	worldgen::PlacementSpec bad;
	bad.cluster = 2.0;
	CHECK_FALSE(worldgen::resolve_placement(0, 1.0, bad, registry, "ctx", rule, error));
}

TEST_CASE("transform_offset rotates quarter turns and mirrors x") {
	int x = 0;
	int z = 0;
	worldgen::transform_offset(2, 1, 0, false, x, z);
	CHECK((x == 2 && z == 1));
	worldgen::transform_offset(2, 1, 1, false, x, z);
	CHECK((x == -1 && z == 2));
	worldgen::transform_offset(2, 1, 2, false, x, z);
	CHECK((x == -2 && z == -1));
	worldgen::transform_offset(2, 1, 3, false, x, z);
	CHECK((x == 1 && z == -2));
	worldgen::transform_offset(2, 1, 0, true, x, z);
	CHECK((x == -2 && z == 1));
	// Four quarter turns come back to the start.
	int rx = 3;
	int rz = -2;
	for (int i = 0; i < 4; ++i) {
		worldgen::transform_offset(rx, rz, 1, false, rx, rz);
	}
	CHECK((rx == 3 && rz == -2));
}

#if VB_WITH_LUA

namespace {

std::filesystem::path temp_storage(const char *name) {
	return std::filesystem::temp_directory_path() /
			(std::string("vb_structure_store_") + name + ".json");
}

} // namespace

TEST_CASE("parse_structure reads a data script into the expected spec") {
	const auto root = scratch("parse");
	write(root / "structures/tree.lua", kTree);
	auto script = script::eval_data_script(root / "structures/tree.lua", root);
	REQUIRE_MESSAGE(script.ok, script.error);
	const worldgen::StructureSpec spec = script::parse_structure(script.value);
	CHECK(spec.name == "test:tree");
	CHECK(spec.size == core::IVec3{ 3, 2, 3 });
	CHECK(spec.anchor == core::IVec3{ 1, 0, 1 });
	REQUIRE(spec.variants.size() == 2);
	CHECK(spec.variants[0].weight == 2.0);
	CHECK(spec.variants[0].layers[1][1] == "L_L");
	CHECK_FALSE(spec.palette.at('.').has_value());
	CHECK(spec.palette.at('W') == std::optional<std::string>("base:wood"));
	CHECK(spec.placement.rotate == true);
	CHECK(spec.placement.min_spacing == 5);
	CHECK(spec.placement.cluster == 0.25);
	REQUIRE(spec.placement.on.has_value());
	CHECK(spec.placement.on->size() == 2);
}

TEST_CASE("vb.register_structure and eval_data_script give equal StructureDefs") {
	const auto root = scratch("equal");
	write(root / "structures/tree.lua", kTree);

	net::LoopbackNetwork network;
	world::BlockRegistry registry2 = world::BlockRegistry::base();
	script::PackRuntime rt2(network.server(), registry2, temp_storage("equal2"));
	rt2.set_pack_modules(script::collect_requirable_modules(root));
	REQUIRE(rt2.load_pack_file(R"(
		vb.register_structure(require("structures.tree"))
		vb.register_biome({ name = "t:b", surface = "base:grass", filler = "base:dirt", stone = "base:stone",
			decoration = { { structure = "test:tree", spawn_rate = 0.5, min_spacing = 9 } } })
		vb.worldgen.set_pipeline({ height = vb.noise.value() })
	)"));
	rt2.freeze();
	const auto pipeline = rt2.build_worldgen_pipeline(worldgen::WorldGenParams{});
	REQUIRE(pipeline);
	REQUIRE(pipeline->structures.size() == 1);

	auto script = script::eval_data_script(root / "structures/tree.lua", root);
	REQUIRE(script.ok);
	worldgen::StructureDef direct;
	std::string error;
	REQUIRE_MESSAGE(worldgen::resolve_structure(script::parse_structure(script.value), registry2,
							direct, error),
			error);
	CHECK(pipeline->structures[0] == direct);

	// The biome entry resolved against the structure's placement defaults,
	// with its own min_spacing override.
	REQUIRE(pipeline->decoration.size() == 1);
	REQUIRE(pipeline->decoration[0].size() == 1);
	const worldgen::PlacementRule &rule = pipeline->decoration[0][0];
	CHECK(rule.structure == 0);
	CHECK(rule.spawn_rate == 0.5);
	CHECK(rule.min_spacing == 9);
	CHECK(rule.rotate);
	CHECK(rule.cluster == 0.25);
	CHECK(rule.on.size() == 2);
	CHECK(pipeline->is_replaceable(registry2.find("base:leaves")) ==
			registry2.get(registry2.find("base:leaves")).replaceable);
}

TEST_CASE("malformed structure tables give errors naming the structure") {
	const auto root = scratch("malformed");
	const auto parse_error = [&](const std::string &body) -> std::string {
		write(root / "s.lua", body);
		auto script = script::eval_data_script(root / "s.lua", root);
		REQUIRE_MESSAGE(script.ok, script.error);
		try {
			(void)script::parse_structure(script.value);
		} catch (const sol::error &e) {
			return e.what();
		}
		return {};
	};
	const std::string head = R"(name = "t:s", size = {x=2,y=1,z=1}, palette = {A="base:stone"}, )";

	CHECK(parse_error("return { " + head + R"(variants = {{ layers = {{ "A" }} }} })")
					.find("characters long, expected 2") != std::string::npos);
	CHECK(parse_error("return { " + head + R"(variants = {{ layers = {{ "AB" }} }} })")
					.find("'B'") != std::string::npos);
	CHECK(parse_error("return { " + head + R"(variants = {{ layers = {{ "AA" }} }}, bogus = 1 })")
					.find("unknown key 'bogus'") != std::string::npos);
	CHECK(parse_error(R"(return { name = "t:s", size = {x=65,y=1,z=1}, palette = {A="base:stone"},
			variants = {{ layers = {{ "A" }} }} })")
					.find("size") != std::string::npos);
	CHECK(parse_error("return { " + head + R"(variants = {{ layers = {{ "AA" }} }},
			placement = { cluster = 3 } })")
					.find("cluster") != std::string::npos);
	CHECK(parse_error("return { " + head + R"(variants = {{ layers = {{ "AA" }} }},
			placement = { rotat = true } })")
					.find("unknown key 'rotat'") != std::string::npos);
	CHECK(parse_error(R"(return { size = {x=1,y=1,z=1} })").find("'name' is required") !=
			std::string::npos);
	CHECK(parse_error("return { " + head + R"(variants = {{ layers = {{ "AA" }} }} })").empty());
}

TEST_CASE("an unknown block or structure name is a validation error naming the offender") {
	net::LoopbackNetwork network;
	world::BlockRegistry registry = world::BlockRegistry::base();
	script::PackRuntime rt(network.server(), registry, temp_storage("unknown"));
	REQUIRE(rt.load_pack_file(R"(vb.register_structure({
		name = "t:s", size = {x=1,y=1,z=1}, palette = {A="mod:nothing"},
		variants = {{ layers = {{ "A" }} }} }))"));
	rt.freeze();
	const auto res = rt.validate_worldgen();
	CHECK_FALSE(res);
	CHECK(res.message.find("t:s") != std::string::npos);
	CHECK(res.message.find("mod:nothing") != std::string::npos);

	world::BlockRegistry registry2 = world::BlockRegistry::base();
	script::PackRuntime rt2(network.server(), registry2, temp_storage("unknown2"));
	REQUIRE(rt2.load_pack_file(R"(vb.register_biome({ name = "t:b",
		decoration = { { structure = "t:ghost", spawn_rate = 1 } } }))"));
	rt2.freeze();
	const auto res2 = rt2.validate_worldgen();
	CHECK_FALSE(res2);
	CHECK(res2.message.find("t:ghost") != std::string::npos);
	CHECK(res2.message.find("t:b") != std::string::npos);

	// A duplicate name is rejected at registration.
	world::BlockRegistry registry3 = world::BlockRegistry::base();
	script::PackRuntime rt3(network.server(), registry3, temp_storage("dup"));
	const char *def = R"(vb.register_structure({ name = "t:s", size = {x=1,y=1,z=1},
		palette = {A="base:stone"}, variants = {{ layers = {{ "A" }} }} }))";
	REQUIRE(rt3.load_pack_file(def));
	CHECK_FALSE(rt3.load_pack_file(def));
}

TEST_CASE("an inline decoration entry becomes an anonymous one-variant structure") {
	net::LoopbackNetwork network;
	world::BlockRegistry registry = world::BlockRegistry::base();
	script::PackRuntime rt(network.server(), registry, temp_storage("inline"));
	REQUIRE(rt.load_pack_file(R"(
		vb.register_biome({ name = "t:b", surface = "base:grass", filler = "base:dirt", stone = "base:stone",
			decoration = { { spawn_rate = 0.25, blocks = {
				{ x = 0, y = 0, z = 0, block = "base:wood" },
				{ x = 0, y = 1, z = 0, block = "base:wood" },
				{ x = 1, y = 1, z = -1, block = "base:leaves" },
			} } } })
		vb.worldgen.set_pipeline({ height = vb.noise.value() })
	)"));
	rt.freeze();
	REQUIRE(rt.validate_worldgen());
	const auto pipeline = rt.build_worldgen_pipeline(worldgen::WorldGenParams{});
	REQUIRE(pipeline);
	REQUIRE(pipeline->structures.size() == 1);
	const worldgen::StructureDef &anon = pipeline->structures[0];
	REQUIRE(anon.variants.size() == 1);
	const auto &v = anon.variants[0];
	// x in [0,1], y in [0,1], z in [-1,0] -> 2x2x2, anchor at z offset 1.
	CHECK(v.size == core::IVec3{ 2, 2, 2 });
	CHECK(anon.anchor == core::IVec3{ 0, 0, 1 });
	CHECK(v.at(0, 0, 1) == registry.find("base:wood"));
	CHECK(v.at(0, 1, 1) == registry.find("base:wood"));
	CHECK(v.at(1, 1, 0) == registry.find("base:leaves"));
	CHECK(v.at(1, 0, 0) == worldgen::kKeepCell);

	REQUIRE(pipeline->decoration[0].size() == 1);
	const worldgen::PlacementRule &rule = pipeline->decoration[0][0];
	CHECK(rule.replace == worldgen::ReplacePolicy::kAll);
	CHECK_FALSE(rule.rotate);
	CHECK(rule.spawn_rate == 0.25);
	CHECK(rule.on.empty());
}

#endif // VB_WITH_LUA
