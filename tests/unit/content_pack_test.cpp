// Phase 5.1: loads the real `content/base` files (not inline Lua strings
// like pack_runtime_test.cpp) through the same vb::script::load_content_pack
// path src/server/main.cpp uses. Guards against a future content edit
// breaking the shipped pack -- nothing else in the test suite parses these
// files.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "vb/net/loopback.hpp"
#include "vb/script/pack_loader.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/world/block.hpp"

namespace {

std::filesystem::path base_pack_dir() {
	return std::filesystem::path(VB_PROJECT_SOURCE_DIR) / "content" / "base";
}

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
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	const std::size_t base_size = registry.size();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("ok"));

	REQUIRE(vb::script::load_content_pack(rt, base_pack_dir()));
	rt.freeze();

	// blocks/*.lua re-declare exactly the Phase 2 base() set by name --
	// add_or_get is idempotent, so no new ids should have been created.
	CHECK(registry.size() == base_size);
	for (const char *name : { "base:dirt", "base:grass", "base:stone",
				 "base:sand", "base:wood", "base:leaves" }) {
		const vb::core::BlockId id = registry.find(name);
		CHECK(id != vb::world::base_block::air);
	}
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

#endif
