#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "vb/net/loopback.hpp"
#include "vb/script/pack_runtime.hpp"
#include "vb/script/ui_runtime.hpp"
#include <sol/sol.hpp>

#include "vb/world/block.hpp"

// Drift test (architecture_spec/dev-experience.md §3.2): the live `vb`/`ui`/
// `client` tables and usertype methods must match sdk/lua/api_index.txt (the
// sorted list of dotted names the stubs in sdk/lua/library/ declare, written
// by scripts/gen_lua_docs.py). sdk/lua/api_ignore.txt lists internals left
// out on purpose. Set VB_DUMP_API=<file> to write the current surface there
// (how a new binding's name is found); a stub-less binding fails this test.

#if VB_WITH_LUA

namespace {

std::set<std::string> read_lines(const std::filesystem::path &p) {
	std::set<std::string> out;
	std::ifstream in(p);
	std::string line;
	while (std::getline(in, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
			line.pop_back();
		}
		if (!line.empty() && line[0] != '#') {
			out.insert(line);
		}
	}
	return out;
}

} // namespace

TEST_CASE("Lua API surface matches sdk/lua/api_index.txt") {
	vb::net::LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	const auto storage = std::filesystem::temp_directory_path() / "vb_api_surface_storage.json";
	std::filesystem::remove(storage);
	vb::script::PackRuntime pack(net.server(), registry, storage);
	vb::script::UiRuntime ui;

	std::set<std::string> live;
	for (auto &n : pack.describe_api()) {
		live.insert(n);
	}
	for (auto &n : ui.describe_api()) {
		live.insert(n);
	}
	REQUIRE_FALSE(live.empty());

	if (const char *dump = std::getenv("VB_DUMP_API")) {
		std::ofstream out(dump);
		for (auto &n : live) {
			out << n << '\n';
		}
	}

	const std::filesystem::path sdk = std::filesystem::path(VB_PROJECT_SOURCE_DIR) / "sdk" / "lua";
	const auto documented = read_lines(sdk / "api_index.txt");
	const auto ignored = read_lines(sdk / "api_ignore.txt");
	REQUIRE_FALSE(documented.empty());

	std::string undocumented;
	for (auto &n : live) {
		if (!documented.count(n) && !ignored.count(n)) {
			undocumented += "\n  " + n;
		}
	}
	std::string unbound;
	for (auto &n : documented) {
		if (!live.count(n)) {
			unbound += "\n  " + n;
		}
	}
	INFO("bound but undocumented (add a stub in sdk/lua/library/ and run scripts/gen_lua_docs.py,"
		 " or list it in sdk/lua/api_ignore.txt):" << undocumented);
	CHECK(undocumented.empty());
	INFO("documented but not bound:" << unbound);
	CHECK(unbound.empty());
}

// Every ```lua example in the generated reference must at least compile
// (docs/lua-reference/ is generated from the stubs by scripts/gen_lua_docs.py).
TEST_CASE("Lua reference examples are syntactically valid") {
	const std::filesystem::path dir = std::filesystem::path(VB_PROJECT_SOURCE_DIR) / "docs" / "lua-reference";
	REQUIRE(std::filesystem::is_directory(dir));
	sol::state lua;
	int checked = 0;
	for (const auto &entry : std::filesystem::directory_iterator(dir)) {
		if (entry.path().extension() != ".md") {
			continue;
		}
		std::ifstream in(entry.path());
		std::string line;
		std::string code;
		bool in_code = false;
		int block_start = 0;
		int lineno = 0;
		while (std::getline(in, line)) {
			++lineno;
			if (!in_code && line == "```lua") {
				in_code = true;
				code.clear();
				block_start = lineno;
			} else if (in_code && line == "```") {
				in_code = false;
				const auto loaded = lua.load(code, "=" + entry.path().filename().string());
				INFO(entry.path().filename().string() << ":" << block_start << ": " << (loaded.valid() ? "" : sol::error(loaded).what()));
				CHECK(loaded.valid());
				++checked;
			} else if (in_code) {
				code += line + "\n";
			}
		}
	}
	CHECK(checked > 40);
}

#endif
