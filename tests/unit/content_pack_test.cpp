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
#include "vb/net/session.hpp"
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

	// blocks/*.lua re-declare exactly the Phase 2 base() set by name (no new
	// ids from those -- add_or_get is idempotent), plus two genuinely new
	// crafted-only blocks (planks, sticks; see crafting.lua) that don't
	// exist in BlockRegistry::base() at all.
	CHECK(registry.size() == base_size + 2);
	for (const char *name : { "base:dirt", "base:grass", "base:stone",
				 "base:sand", "base:wood", "base:leaves", "base:planks",
				 "base:sticks" }) {
		const vb::core::BlockId id = registry.find(name);
		CHECK(id != vb::world::base_block::air);
	}
}

TEST_CASE("content/base crafting: wood -> planks -> sticks via /craft chat") {
	using namespace vb::net;
	using vb::core::NetId;

	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	vb::script::PackRuntime rt(net.server(), registry, temp_storage("craft"));
	REQUIRE(vb::script::load_content_pack(rt, base_pack_dir()));

	// Test-only seam to get wood into the test player's hands: bypasses real
	// block-breaking/item-drop mechanics (already covered end-to-end by
	// pack_runtime_integration_test.cpp's own item-drop test) since this
	// test is specifically about crafting.lua's recipe logic, not
	// acquisition. Registered after the real pack, so it runs as an
	// additional "chat" handler alongside crafting.lua's own.
	REQUIRE(rt.load_pack_file(R"(
		vb.on("chat", function(player, text)
			if text == "/testgive-wood" then
				player:give({ item = base_wood_id, count = 2 })
				return false
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

	const vb::core::BlockId wood_id = registry.find("base:wood");
	const vb::core::BlockId planks_id = registry.find("base:planks");
	const vb::core::BlockId sticks_id = registry.find("base:sticks");

	auto count_of = [&](vb::core::BlockId item) -> int {
		for (const auto &slot : client.inventory()) {
			if (slot.item == item) {
				return slot.count;
			}
		}
		return 0;
	};

	client.send_chat("/testgive-wood");
	pump(4);
	REQUIRE(count_of(wood_id) == 2);

	// Missing ingredients: crafting sticks needs planks, which we don't have
	// yet -- should fail cleanly (no inventory change) with a feedback line.
	client.send_chat("/craft base:sticks");
	pump(4);
	CHECK(count_of(planks_id) == 0);
	CHECK(count_of(sticks_id) == 0);
	{
		const auto msgs = client.take_chat_messages();
		REQUIRE_FALSE(msgs.empty());
		CHECK(msgs.back() == "Missing ingredients for base:sticks");
	}

	// Craft planks from wood: 1 wood in, 4 planks out.
	client.send_chat("/craft base:planks");
	pump(4);
	CHECK(count_of(wood_id) == 1);
	CHECK(count_of(planks_id) == 4);
	{
		const auto msgs = client.take_chat_messages();
		REQUIRE_FALSE(msgs.empty());
		CHECK(msgs.back() == "Crafted base:planks");
	}

	// Craft sticks from planks: 2 planks in, 4 sticks out.
	client.send_chat("/craft base:sticks");
	pump(4);
	CHECK(count_of(planks_id) == 2);
	CHECK(count_of(sticks_id) == 4);
	{
		const auto msgs = client.take_chat_messages();
		REQUIRE_FALSE(msgs.empty());
		CHECK(msgs.back() == "Crafted base:sticks");
	}

	// Unknown recipe name: rejected, no side effects.
	client.send_chat("/craft base:does-not-exist");
	pump(4);
	CHECK(count_of(planks_id) == 2);
	CHECK(count_of(sticks_id) == 4);
	{
		const auto msgs = client.take_chat_messages();
		REQUIRE_FALSE(msgs.empty());
		CHECK(msgs.back() == "Unknown recipe: base:does-not-exist");
	}

	// A normal chat message (not a /craft command) still broadcasts as chat.
	client.send_chat("hello");
	pump(4);
	{
		const auto msgs = client.take_chat_messages();
		REQUIRE_FALSE(msgs.empty());
		CHECK(msgs.back() == "A: hello");
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
