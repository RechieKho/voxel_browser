#include <doctest/doctest.h>

#if VB_WITH_LUA

#include <filesystem>
#include <fstream>
#include <string>

#include "vb/net/loopback.hpp"
#include "vb/script/block_def.hpp"
#include "vb/script/data_script.hpp"
#include "vb/script/pack_loader.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/world/block.hpp"

namespace {

std::filesystem::path scratch_dir(const char *name) {
	auto dir = std::filesystem::temp_directory_path() / (std::string("vb_data_script_") + name);
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	return dir;
}

void write(const std::filesystem::path &p, const std::string &text) {
	std::filesystem::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

const char *kFullBlock = R"({
	name = "test:lamp", solid = false, opaque = false, liquid = true,
	region = false, light = 9, texture = "textures/lamp.png", max_damage = 7,
	crack_texture = "textures/lamp_crack.png", max_stack = 16,
	pickup_radius = 2.5, item_lifetime_seconds = 30.0, replaceable = true,
})";

void check_same(const vb::world::BlockType &a, const vb::world::BlockType &b) {
	CHECK(a.name == b.name);
	CHECK(a.solid == b.solid);
	CHECK(a.opaque == b.opaque);
	CHECK(a.liquid == b.liquid);
	CHECK(a.region == b.region);
	CHECK(a.light_emission == b.light_emission);
	CHECK(a.texture == b.texture);
	CHECK(a.max_damage == b.max_damage);
	CHECK(a.crack_texture == b.crack_texture);
	CHECK(a.max_stack == b.max_stack);
	CHECK(a.pickup_radius == b.pickup_radius);
	CHECK(a.drop_lifetime_seconds == b.drop_lifetime_seconds);
	CHECK(a.replaceable == b.replaceable);
}

} // namespace

TEST_CASE("parse_block_type gives the same BlockType through vb.register_block and eval_data_script") {
	// Through vb.register_block.
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry,
			std::filesystem::temp_directory_path() / "vb_data_script_register.json");
	REQUIRE(rt.load_pack_file(std::string("vb.register_block(") + kFullBlock + ")"));
	const vb::world::BlockType &registered = registry.get(registry.find("test:lamp"));

	// Through a data script.
	const auto root = scratch_dir("parity");
	write(root / "data/blocks.lua", std::string("return { ") + kFullBlock + " }");
	auto script = vb::script::eval_data_script(root / "data/blocks.lua", root);
	REQUIRE_MESSAGE(script.ok, script.error);
	// Tables borrow the script's Lua state: parse before `script` is reassigned.
	check_same(vb::script::parse_block_type(script.value[1]), registered);
	CHECK(registered.replaceable);

	// Defaults match too.
	REQUIRE(rt.load_pack_file(R"(vb.register_block({ name = "test:plain" }))"));
	write(root / "data/plain.lua", R"(return { { name = "test:plain" } })");
	script = vb::script::eval_data_script(root / "data/plain.lua", root);
	REQUIRE_MESSAGE(script.ok, script.error);
	check_same(vb::script::parse_block_type(script.value[1]), registry.get(registry.find("test:plain")));
}

TEST_CASE("parse_block_type rejects a table with no name") {
	const auto root = scratch_dir("noname");
	write(root / "blocks.lua", "return { { solid = true } }");
	auto script = vb::script::eval_data_script(root / "blocks.lua", root);
	REQUIRE(script.ok);
	CHECK_THROWS_AS(vb::script::parse_block_type(script.value[1]), sol::error);
}

TEST_CASE("a data script that touches vb.* fails with a clear message naming the file") {
	const auto root = scratch_dir("vbcall");
	write(root / "calls.lua", R"(vb.register_block({ name = "x:y" }) return {})");
	auto script = vb::script::eval_data_script(root / "calls.lua", root);
	CHECK_FALSE(script.ok);
	CHECK(script.error.find(vb::script::kDataScriptVbError) != std::string::npos);
	CHECK(script.error.find("calls.lua") != std::string::npos);

	write(root / "reads.lua", R"(local _ = vb.storage return {})");
	script = vb::script::eval_data_script(root / "reads.lua", root);
	CHECK_FALSE(script.ok);
	CHECK(script.error.find(vb::script::kDataScriptVbError) != std::string::npos);
}

TEST_CASE("eval_data_script reports missing files, syntax errors and non-table results") {
	const auto root = scratch_dir("errors");
	auto script = vb::script::eval_data_script(root / "missing.lua", root);
	CHECK_FALSE(script.ok);
	CHECK(script.error.find("missing.lua") != std::string::npos);

	write(root / "syntax.lua", "return {");
	script = vb::script::eval_data_script(root / "syntax.lua", root);
	CHECK_FALSE(script.ok);
	CHECK(script.error.find("syntax.lua") != std::string::npos);

	write(root / "number.lua", "return 3");
	script = vb::script::eval_data_script(root / "number.lua", root);
	CHECK_FALSE(script.ok);
	CHECK(script.error.find("must return a table") != std::string::npos);
}

TEST_CASE("a data script may require the pack's own .lua files but nothing else") {
	const auto root = scratch_dir("require");
	write(root / "lib/consts.lua", "return { stack = 8 }");
	write(root / "data/blocks.lua",
			R"(local c = require("lib.consts") return { { name = "t:a", max_stack = c.stack } })");
	auto script = vb::script::eval_data_script(root / "data/blocks.lua", root);
	REQUIRE_MESSAGE(script.ok, script.error);
	CHECK(vb::script::parse_block_type(script.value[1]).max_stack == 8);

	write(root / "data/bad.lua", R"(require("os") return {})");
	script = vb::script::eval_data_script(root / "data/bad.lua", root);
	CHECK_FALSE(script.ok);
	write(root / "data/escape.lua", R"(require("../x") return {})");
	script = vb::script::eval_data_script(root / "data/escape.lua", root);
	CHECK_FALSE(script.ok);
}

TEST_CASE("vb.register_block{replaceable=true} on a built-in block lands without resetting other fields") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry,
			std::filesystem::temp_directory_path() / "vb_data_script_replaceable.json");
	CHECK_FALSE(registry.get(vb::world::base_block::leaves).replaceable);
	REQUIRE(rt.load_pack_file(R"(vb.register_block({ name = "base:leaves", replaceable = true }))"));
	const auto &leaves = registry.get(vb::world::base_block::leaves);
	CHECK(leaves.replaceable);
	CHECK(leaves.solid);
	CHECK_FALSE(leaves.opaque); // base()'s own flags, not the table's defaults
}

TEST_CASE("content/base registers the same blocks, in the same order, as before the data-script move") {
	const auto base_dir = std::filesystem::path(VB_PROJECT_SOURCE_DIR) / "content" / "base";

	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry,
			std::filesystem::temp_directory_path() / "vb_data_script_content_base.json");
	REQUIRE(vb::script::load_content_pack(rt, base_dir));

	// Ids are stored in region files, so this order is a compatibility contract.
	const char *expected[] = { "base:air", "base:stone", "base:dirt", "base:grass",
		"base:sand", "base:water", "base:wood", "base:leaves", "base:planks",
		"base:sticks" };
	REQUIRE(registry.size() == std::size(expected));
	for (std::size_t i = 0; i < std::size(expected); ++i) {
		CHECK(registry.get(static_cast<vb::core::BlockId>(i)).name == expected[i]);
	}
	CHECK(registry.get(registry.find("base:stone")).texture == "textures/stone.png");
	CHECK(registry.get(registry.find("base:water")).texture == "textures/water.png");
	CHECK(registry.get(registry.find("base:water")).liquid);
	CHECK(registry.get(registry.find("base:leaves")).replaceable);
	CHECK_FALSE(registry.get(registry.find("base:leaves")).opaque);
	CHECK_FALSE(registry.get(registry.find("base:sticks")).solid);
	CHECK_FALSE(registry.get(registry.find("base:stone")).replaceable);

	// The editor reads the same file without loading the pack.
	auto script = vb::script::eval_data_script(base_dir / "data" / "blocks.lua", base_dir);
	REQUIRE_MESSAGE(script.ok, script.error);
	std::size_t count = 0;
	for (const auto &kv : script.value) {
		const auto type = vb::script::parse_block_type(kv.second.as<sol::table>());
		const auto &registered = registry.get(registry.find(type.name));
		CHECK(registered.name == type.name);
		CHECK(registered.texture == type.texture);
		CHECK(registered.replaceable == type.replaceable);
		++count;
	}
	CHECK(count == 9);
}

#endif // VB_WITH_LUA
