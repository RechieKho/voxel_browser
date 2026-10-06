// Phase 10.F: `vb help`, generated docs/cli.md, the error/--json contract, `vb docs`.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

#include "vb/cli/commands.hpp"
#include "vb/cli/layout.hpp"

namespace fs = std::filesystem;
using namespace vb::cli;

namespace {

struct TempDir {
	fs::path path;
	TempDir() {
		std::random_device rd;
		path = fs::temp_directory_path() / ("vb_help_test_" + std::to_string(rd()));
		fs::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

struct Run {
	int code;
	std::string out;
	std::string err;
};

Run run_vb(const Layout &layout, std::vector<std::string> args) {
	std::ostringstream out, err;
	const int code = run_cli(args, layout, out, err);
	return { code, out.str(), err.str() };
}

void write(const fs::path &p, const std::string &text) {
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

} // namespace

TEST_CASE("docs/cli.md is exactly `vb help --markdown`") {
	TempDir t;
	const Layout layout(t.path / "d", t.path / "c", t.path / "k");
	const Run r = run_vb(layout, { "help", "--markdown" });
	REQUIRE(r.code == 0);
	std::ifstream in(fs::path(VB_PROJECT_SOURCE_DIR) / "docs" / "cli.md", std::ios::binary);
	std::ostringstream committed;
	committed << in.rdbuf();
	// A Windows checkout may have turned LF into CRLF (.gitattributes pins this file to LF,
	// but don't make the freshness check depend on git's line-ending settings).
	std::string text = committed.str();
	text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
	CHECK_MESSAGE(text == r.out, "docs/cli.md is stale: run `vb help --markdown > docs/cli.md`");
}

TEST_CASE("every command has long-form docs and answers --help") {
	TempDir t;
	const Layout layout(t.path / "d", t.path / "c", t.path / "k");
	const Run listing = run_vb(layout, { "help", "--json" });
	REQUIRE(listing.code == 0);
	const auto j = nlohmann::json::parse(listing.out);
	REQUIRE(j["commands"].size() > 15);
	for (const auto &c : j["commands"]) {
		const std::string name = c["name"];
		INFO(name);
		CHECK_FALSE(c["details"].get<std::string>().empty());
		CHECK_FALSE(c["examples"].empty());
		const Run h = run_vb(layout, { name, "--help" });
		CHECK(h.code == 0);
		CHECK(h.out.rfind("vb " + name, 0) == 0);
		CHECK(run_vb(layout, { "help", name }).out == h.out);
	}
}

TEST_CASE("errors follow the error:/hint: contract and unknown input exits 2") {
	TempDir t;
	const Layout layout(t.path / "d", t.path / "c", t.path / "k");
	const Run bad = run_vb(layout, { "frobnicate" });
	CHECK(bad.code == 2);
	CHECK(bad.err.rfind("error: unknown command 'frobnicate'", 0) == 0);
	CHECK(bad.err.find("\nhint: ") != std::string::npos);
	CHECK(run_vb(layout, { "help", "frobnicate" }).code == 2);
	CHECK(run_vb(layout, { "which", "--version" }).code == 2);
}

TEST_CASE("doctor and install report as JSON") {
	TempDir t;
	const Layout layout(t.path / "d", t.path / "c", t.path / "k");
	const Run d = run_vb(layout, { "doctor", "--json" });
	const auto j = nlohmann::json::parse(d.out);
	CHECK(j.contains("ok"));
	CHECK(j["checks"].is_array());
	CHECK_FALSE(j["checks"].empty());
	CHECK(d.code == (j["ok"].get<bool>() ? 0 : 1));
}

TEST_CASE("vb docs lists topics, prints a file, a Lua function, and the directory") {
	TempDir t;
	const Layout layout(t.path / "d", t.path / "c", t.path / "k");
	write(layout.version_dir("v0.1.0") / "voxel_browser_server", "x");
	write(layout.version_dir("v0.1.0") / "docs" / "cli.md", "# cli\n");
	write(layout.version_dir("v0.1.0") / "docs" / "lua-reference" / "vb.world.md",
			"# `vb.world`\n\n## vb.world.get_block\n\nblock text\n\n## vb.world.raycast\n\nray text\n");
	REQUIRE(run_vb(layout, { "use", "v0.1.0" }).code == 0);

	const Run list = run_vb(layout, { "docs" });
	CHECK(list.code == 0);
	CHECK(list.out.find("cli.md") != std::string::npos);
	CHECK(list.out.find("lua-reference/vb.world.md") != std::string::npos);
	CHECK(run_vb(layout, { "docs", "cli.md" }).out == "# cli\n");
	CHECK(run_vb(layout, { "docs", "cli" }).out == "# cli\n");
	const Run fn = run_vb(layout, { "docs", "vb.world.raycast" });
	CHECK(fn.code == 0);
	CHECK(fn.out.find("ray text") != std::string::npos);
	CHECK(fn.out.find("block text") == std::string::npos);
	CHECK(run_vb(layout, { "docs", "vb.world.get_block" }).out.find("block text") != std::string::npos);
	CHECK(run_vb(layout, { "docs", "--path" }).out == (layout.version_dir("v0.1.0") / "docs").string() + "\n");
	const Run missing = run_vb(layout, { "docs", "nope" });
	CHECK(missing.code == 1);
	CHECK(missing.err.rfind("error:", 0) == 0);
}
