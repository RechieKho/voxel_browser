#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "vb/cli/commands.hpp"
#include "vb/cli/layout.hpp"
#include "vb/cli/store.hpp"
#include "vb/cli/version.hpp"
#include "vb/core/paths.hpp"

namespace fs = std::filesystem;

namespace {

// Fresh temp dir removed on scope exit.
struct TempDir {
	fs::path path;
	TempDir() {
		std::random_device rd;
		path = fs::temp_directory_path() / ("vb_cli_test_" + std::to_string(rd()));
		fs::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

vb::cli::Layout test_layout(const fs::path &root) {
	return vb::cli::Layout(root / "data", root / "config", root / "cache");
}

void touch(const fs::path &p) {
	fs::create_directories(p.parent_path());
	std::ofstream(p) << "x";
}

struct Run {
	int code;
	std::string out;
	std::string err;
};

Run run_vb(const vb::cli::Layout &layout, std::vector<std::string> args) {
	std::ostringstream out, err;
	const int code = vb::cli::run_cli(args, layout, out, err);
	return { code, out.str(), err.str() };
}

void fake_install(const vb::cli::Layout &l, const std::string &tag) {
	touch(l.version_dir(tag) / vb::cli::binary_file_name(vb::cli::Binary::Client));
	touch(l.version_dir(tag) / vb::cli::binary_file_name(vb::cli::Binary::Server));
}

} // namespace

TEST_CASE("parse_version accepts clean tags only") {
	using vb::cli::parse_version;
	CHECK(parse_version("v1.2.3").value() == vb::cli::Version{ 1, 2, 3 });
	CHECK(parse_version("0.10.0").value() == vb::cli::Version{ 0, 10, 0 });
	CHECK_FALSE(parse_version("v1.2").has_value());
	CHECK_FALSE(parse_version("v1.2.3-4-gabc").has_value());
	CHECK_FALSE(parse_version("dev").has_value());
	CHECK_FALSE(parse_version("").has_value());
	CHECK(vb::cli::to_tag({ 1, 2, 3 }) == "v1.2.3");
	CHECK(parse_version("v1.10.0").value() > parse_version("v1.9.0").value());
}

TEST_CASE("link names") {
	using vb::cli::is_valid_link_name;
	CHECK(is_valid_link_name("dev"));
	CHECK(is_valid_link_name("my-build_2"));
	CHECK_FALSE(is_valid_link_name("latest"));
	CHECK_FALSE(is_valid_link_name("default"));
	CHECK_FALSE(is_valid_link_name("v1.2.3"));
	CHECK_FALSE(is_valid_link_name("1abc"));
	CHECK_FALSE(is_valid_link_name("Dev"));
	CHECK_FALSE(is_valid_link_name("../x"));
	CHECK_FALSE(is_valid_link_name(""));
}

TEST_CASE("VB_HOME overrides all user roots") {
	TempDir tmp;
#if defined(_WIN32)
	_putenv_s("VB_HOME", tmp.path.string().c_str());
#else
	setenv("VB_HOME", tmp.path.c_str(), 1);
#endif
	CHECK(vb::core::user_data_dir() == tmp.path);
	CHECK(vb::core::user_config_dir() == tmp.path / "config");
	CHECK(vb::core::user_cache_dir() == tmp.path / "cache");
#if defined(_WIN32)
	_putenv_s("VB_HOME", "");
#else
	unsetenv("VB_HOME");
#endif
	CHECK(vb::core::user_data_dir() != tmp.path);
}

TEST_CASE("list/use/which over installed releases and links") {
	TempDir tmp;
	const auto l = test_layout(tmp.path);

	CHECK(run_vb(l, { "list" }).out.find("nothing installed") != std::string::npos);
	CHECK(run_vb(l, { "which" }).code == vb::cli::kExitFailure);

	fake_install(l, "v0.9.0");
	fake_install(l, "v0.10.0");
	fs::create_directories(l.versions_dir() / ".staging-v0.11.0-abc"); // ignored

	const auto entries = vb::cli::list_entries(l);
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].name == "v0.10.0"); // numeric, not lexicographic, order
	CHECK(entries[1].name == "v0.9.0");

	CHECK(run_vb(l, { "which" }).code == vb::cli::kExitFailure); // no default yet
	CHECK(run_vb(l, { "use", "v0.9.0" }).code == 0);
	CHECK(run_vb(l, { "use", "0.10.0" }).code == 0); // bare number accepted
	CHECK(vb::cli::read_default_version(l).value() == "v0.10.0");

	const Run which = run_vb(l, { "which", "server" });
	CHECK(which.code == 0);
	CHECK(which.out.find("v0.10.0") != std::string::npos);
	CHECK(run_vb(l, { "which", "--version", "v0.9.0" }).out.find("v0.9.0") != std::string::npos);
	CHECK(run_vb(l, { "use", "v9.9.9" }).code == vb::cli::kExitFailure);

	const Run list = run_vb(l, { "list" });
	CHECK(list.out.find("* v0.10.0") != std::string::npos);
	CHECK(list.out.find("  v0.9.0") != std::string::npos);
}

TEST_CASE("link registers a build dir; uninstall of a link keeps the build") {
	TempDir tmp;
	const auto l = test_layout(tmp.path);
	const fs::path build = tmp.path / "build";
	touch(build / vb::cli::binary_file_name(vb::cli::Binary::Client));

	CHECK(run_vb(l, { "link", "dev", build.string() }).code == 0);
	CHECK(run_vb(l, { "link", "v1.0.0", build.string() }).code == vb::cli::kExitFailure);
	CHECK(run_vb(l, { "link", "dev", (tmp.path / "nope").string() }).code ==
			vb::cli::kExitFailure);
	CHECK(run_vb(l, { "use", "dev" }).code == 0);

	const Run which = run_vb(l, { "which" });
	CHECK(which.code == 0);
	CHECK(fs::path(which.out.substr(0, which.out.find('\n'))).filename() ==
			vb::cli::binary_file_name(vb::cli::Binary::Client));
	// No server binary in that build dir -> clear failure, not a crash.
	CHECK(run_vb(l, { "which", "server" }).code == vb::cli::kExitFailure);

	CHECK(run_vb(l, { "uninstall", "dev" }).code == 0);
	CHECK(fs::exists(build)); // the build directory is never touched
	CHECK(vb::cli::list_entries(l).empty());
	CHECK_FALSE(vb::cli::read_default_version(l).has_value()); // default cleared
}

TEST_CASE("uninstall removes a release and clears a matching default") {
	TempDir tmp;
	const auto l = test_layout(tmp.path);
	fake_install(l, "v1.0.0");
	fake_install(l, "v1.1.0");
	REQUIRE(run_vb(l, { "use", "v1.0.0" }).code == 0);

	CHECK(run_vb(l, { "uninstall", "v1.1.0" }).code == 0);
	CHECK(vb::cli::read_default_version(l).value() == "v1.0.0"); // untouched
	CHECK(run_vb(l, { "uninstall", "v1.0.0" }).code == 0);
	CHECK_FALSE(fs::exists(l.version_dir("v1.0.0")));
	CHECK_FALSE(vb::cli::read_default_version(l).has_value());
	CHECK(run_vb(l, { "uninstall", "v1.0.0" }).code == vb::cli::kExitFailure);
}

TEST_CASE("a link whose build dir vanished is reported, not resolved") {
	TempDir tmp;
	const auto l = test_layout(tmp.path);
	const fs::path build = tmp.path / "build";
	fs::create_directories(build);
	REQUIRE(run_vb(l, { "link", "dev", build.string() }).code == 0);
	fs::remove_all(build);
	CHECK(run_vb(l, { "list" }).out.find("(missing)") != std::string::npos);
	const Run r = run_vb(l, { "which", "--version", "dev" });
	CHECK(r.code == vb::cli::kExitFailure);
	CHECK(r.err.find("no longer exists") != std::string::npos);
}

TEST_CASE("usage errors exit 2") {
	TempDir tmp;
	const auto l = test_layout(tmp.path);
	CHECK(run_vb(l, {}).code == vb::cli::kExitUsage);
	CHECK(run_vb(l, { "bogus" }).code == vb::cli::kExitUsage);
	CHECK(run_vb(l, { "use" }).code == vb::cli::kExitUsage);
	CHECK(run_vb(l, { "link", "x" }).code == vb::cli::kExitUsage);
	CHECK(run_vb(l, { "which", "--version" }).code == vb::cli::kExitUsage);
	CHECK(run_vb(l, { "--help" }).code == 0);
	CHECK(run_vb(l, { "--version" }).out.find("voxel_browser") != std::string::npos);
}

#if !defined(_WIN32)
TEST_CASE("launch runs the client with the shared client.toml and passthrough args") {
	TempDir tmp;
	const auto l = test_layout(tmp.path);
	// Fake "client": records its argv, exits with code 7.
	const fs::path build = tmp.path / "build";
	fs::create_directories(build);
	const fs::path exe = build / vb::cli::binary_file_name(vb::cli::Binary::Client);
	const fs::path record = tmp.path / "argv.txt";
	{
		std::ofstream(exe) << "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" << record.string()
						   << "'\nexit 7\n";
	}
	fs::permissions(exe, fs::perms::owner_all);
	REQUIRE(run_vb(l, { "link", "dev", build.string() }).code == 0);
	REQUIRE(run_vb(l, { "use", "dev" }).code == 0);

	CHECK(run_vb(l, { "launch", "--", "--headless", "--frames", "2" }).code == 7);
	std::ifstream in(record);
	std::stringstream got;
	got << in.rdbuf();
	// (the linked build dir has no content/ beside it, so no --content-pack here)
	CHECK(got.str() == "--config\n" + l.client_toml().string() + "\n--world-dir\n" + l.singleplayer_world_dir().string() + "\n--headless\n--frames\n2\n");

	// Explicit --config wins; a missing binary is a clean failure.
	CHECK(run_vb(l, { "launch", "--", "--config", "mine.toml" }).code == 7);
	std::ifstream in2(record);
	std::stringstream got2;
	got2 << in2.rdbuf();
	CHECK(got2.str() == "--world-dir\n" + l.singleplayer_world_dir().string() + "\n--config\nmine.toml\n");
	CHECK(run_vb(l, { "launch", "stray" }).code == vb::cli::kExitUsage);
}
#endif
