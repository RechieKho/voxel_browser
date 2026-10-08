#include <doctest/doctest.h>

#include <ostream>

#include <filesystem>
#include <fstream>
#include <string>

#include "vb/world/block.hpp"
#include "vb/world/world_registry.hpp"

using vb::world::BlockRegistry;
using vb::world::BlockType;
using vb::world::check_world_registry;

namespace {

std::filesystem::path fresh_world(const char *name) {
	auto p = std::filesystem::temp_directory_path() / (std::string("vb_world_registry_") + name);
	std::filesystem::remove_all(p);
	return p;
}

BlockRegistry with_blocks(std::initializer_list<const char *> extra) {
	BlockRegistry r = BlockRegistry::base();
	for (const char *name : extra) {
		BlockType t;
		t.name = name;
		r.add_or_get(name, t);
	}
	return r;
}

} // namespace

// Saved chunks are raw block ids: hosting a different pack on the same world
// folder used to load the old pack's terrain with the new pack's blocks.
TEST_CASE("world registry: a world saved by another pack's blocks is refused") {
	const auto dir = fresh_world("other_pack");
	const BlockRegistry base = with_blocks({ "base:planks", "base:sticks" });
	REQUIRE(check_world_registry(dir, base, "base").ok);
	CHECK(std::filesystem::exists(dir / vb::world::kWorldRegistryFile));
	CHECK(check_world_registry(dir, base, "base").ok); // same blocks: fine

	const BlockRegistry mine = with_blocks({ "my:ore", "my:crystal" });
	const auto r = check_world_registry(dir, mine, "my_pack");
	CHECK_FALSE(r.ok);
	CHECK(r.message.find(dir.string()) != std::string::npos);
	CHECK(r.message.find("'base'") != std::string::npos);
	CHECK(r.message.find("'base:planks'") != std::string::npos);
	CHECK(r.message.find("'my:ore'") != std::string::npos);
	// Refusing doesn't overwrite the record: the right pack still loads it.
	CHECK(check_world_registry(dir, base, "base").ok);
	std::filesystem::remove_all(dir);
}

TEST_CASE("world registry: a pack that only adds blocks keeps its world") {
	const auto dir = fresh_world("added");
	REQUIRE(check_world_registry(dir, with_blocks({ "base:planks" }), "base").ok);
	CHECK(check_world_registry(dir, with_blocks({ "base:planks", "base:new" }), "base").ok);
	// The record now includes the new block, so removing it is a mismatch.
	CHECK_FALSE(check_world_registry(dir, with_blocks({ "base:planks" }), "base").ok);
	std::filesystem::remove_all(dir);
}

TEST_CASE("world registry: an existing world without a record is accepted and recorded") {
	const auto dir = fresh_world("legacy");
	std::filesystem::create_directories(dir);
	std::ofstream(dir / "r.0.0.vbr") << "region";
	const auto r = check_world_registry(dir, with_blocks({}), "base");
	CHECK(r.ok);
	CHECK(r.message.find("older version") != std::string::npos);
	CHECK(std::filesystem::exists(dir / vb::world::kWorldRegistryFile));

	const auto fresh = fresh_world("empty");
	const auto r2 = check_world_registry(fresh, with_blocks({}), "base");
	CHECK(r2.ok);
	CHECK(r2.message.empty()); // a brand-new world needs no warning
	std::filesystem::remove_all(dir);
	std::filesystem::remove_all(fresh);
}
