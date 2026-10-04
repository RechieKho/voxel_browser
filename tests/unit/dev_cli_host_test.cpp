// Phase 8.4: hosting -- server instances, process tracking, graceful stop.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "vb/cli/commands.hpp"
#include "vb/cli/instance.hpp"
#include "vb/cli/layout.hpp"
#include "vb/cli/portcheck.hpp"
#include "vb/cli/process.hpp"
#include "vb/cli/store.hpp"
#include "vb/core/paths.hpp"

namespace fs = std::filesystem;
using namespace vb::cli;

namespace {

struct TempDir {
	fs::path path;
	TempDir() {
		std::random_device rd;
		path = fs::temp_directory_path() / ("vb_host_test_" + std::to_string(rd()));
		fs::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

Layout test_layout(const fs::path &root) {
	return Layout(root / "data", root / "config", root / "cache");
}

void write(const fs::path &p, const std::string &text) {
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

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

// A fake "installed version": placeholder binaries plus a content pack.
void fake_install(const Layout &l, const std::string &tag) {
	write(l.version_dir(tag) / binary_file_name(Binary::Client), "x");
	write(l.version_dir(tag) / binary_file_name(Binary::Server), "x");
	write(l.version_dir(tag) / "content" / "base" / "pack.toml", "name = \"base\"\n");
}

Pid own_pid() {
#if defined(_WIN32)
	return static_cast<Pid>(_getpid());
#else
	return static_cast<Pid>(getpid());
#endif
}

std::uint16_t free_udp_port() {
	std::mt19937 rng(std::random_device{}());
	for (int i = 0; i < 200; ++i) {
		const auto port = static_cast<std::uint16_t>(20000 + rng() % 30000);
		if (udp_port_available(port)) {
			return port;
		}
	}
	return 0;
}

bool wait_for(const std::function<bool()> &done, double seconds) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
	while (!done()) {
		if (std::chrono::steady_clock::now() > deadline) {
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return true;
}

std::string slurp(const fs::path &p) {
	std::ifstream f(p, std::ios::binary);
	std::ostringstream os;
	os << f.rdbuf();
	return os.str();
}

} // namespace

TEST_CASE("instance names are validated") {
	CHECK(validate_instance_name("survival").empty());
	CHECK(validate_instance_name("My-Server_2.0").empty());
	CHECK_FALSE(validate_instance_name("").empty());
	CHECK_FALSE(validate_instance_name("../escape").empty());
	CHECK_FALSE(validate_instance_name("a/b").empty());
	CHECK_FALSE(validate_instance_name("a\\b").empty());
	CHECK_FALSE(validate_instance_name(".hidden").empty());
	CHECK_FALSE(validate_instance_name("-flag").empty());
	CHECK_FALSE(validate_instance_name(std::string(65, 'a')).empty());
}

TEST_CASE("create / load / list / remove an instance") {
	TempDir t;
	const Layout l = test_layout(t.path);

	CHECK(create_instance(l, "beta", "0.2.0", "builtin:base", std::uint16_t{ 27111 }));
	CHECK(create_instance(l, "alpha", "", "builtin:base", std::nullopt));
	CHECK_FALSE(create_instance(l, "alpha", "", "builtin:base", std::nullopt)); // exists
	CHECK_FALSE(create_instance(l, "bad/name", "", "builtin:base", std::nullopt));
	CHECK_FALSE(create_instance(l, "relpack", "", "some/relative/dir", std::nullopt));
	CHECK_FALSE(create_instance(l, "badbuiltin", "", "builtin:../x", std::nullopt));

	std::string why;
	const auto beta = load_instance(l, "beta", &why);
	REQUIRE(beta);
	CHECK(beta->version == "v0.2.0"); // canonicalised
	CHECK(beta->pack == "builtin:base");
	CHECK(slurp(instance_server_toml(*beta)).find("port = 27111") != std::string::npos);
	CHECK(load_instance(l, "alpha")->version == "default");

	CHECK_FALSE(load_instance(l, "nope", &why));
	CHECK(why.find("no server named 'nope'") != std::string::npos);

	const auto all = list_instances(l);
	REQUIRE(all.size() == 2);
	CHECK(all[0].name == "alpha");
	CHECK(all[1].name == "beta");

	// --keep-world: everything but world/ goes, and the name can be re-created.
	write(instance_world_dir(*beta) / "r.0.0.vbr", "data");
	CHECK(remove_instance(l, "beta", true));
	CHECK(fs::exists(instance_world_dir(*beta) / "r.0.0.vbr"));
	CHECK_FALSE(load_instance(l, "beta"));
	CHECK(create_instance(l, "beta", "", "builtin:base", std::nullopt));
	CHECK(fs::exists(instance_world_dir(*beta) / "r.0.0.vbr"));

	CHECK(remove_instance(l, "alpha", false));
	CHECK_FALSE(fs::exists(l.servers_dir() / "alpha"));
	CHECK_FALSE(remove_instance(l, "alpha", false)); // gone
}

TEST_CASE("resolve_pack finds builtin packs, including beside a linked build dir") {
	TempDir t;
	std::string why;

	// Release layout: <root>/content/<name>
	write(t.path / "rel" / "content" / "base" / "pack.toml", "x");
	const auto rel = resolve_pack("builtin:base", t.path / "rel", &why);
	REQUIRE(rel);
	CHECK(*rel == fs::absolute(t.path / "rel" / "content" / "base").lexically_normal());

	// In-tree build dir: content lives in the source tree one level up.
	write(t.path / "src" / "content" / "base" / "pack.toml", "x");
	fs::create_directories(t.path / "src" / "build");
	CHECK(resolve_pack("builtin:base", t.path / "src" / "build", &why));

	// Absolute directory.
	write(t.path / "mine" / "pack.toml", "x");
	CHECK(resolve_pack((t.path / "mine").string(), t.path / "rel", &why));

	CHECK_FALSE(resolve_pack("builtin:missing", t.path / "rel", &why));
	CHECK(why.find("not found") != std::string::npos);
	CHECK_FALSE(resolve_pack((t.path / "no_pack_toml").string(), t.path / "rel", &why));
	CHECK_FALSE(resolve_pack("builtin:../../etc", t.path / "rel", &why));
}

TEST_CASE("plan_launch builds the server command line") {
	TempDir t;
	const Layout l = test_layout(t.path);
	fake_install(l, "v0.2.0");
	REQUIRE(write_default_version(l, "v0.2.0"));
	REQUIRE(create_instance(l, "s", "default", "builtin:base", std::uint16_t{ 27222 }));
	const Instance inst = *load_instance(l, "s");

	LaunchPlan plan;
	REQUIRE(plan_launch(l, inst, {}, plan));
	CHECK(plan.version_name == "v0.2.0");
	CHECK(plan.port == 27222);
	CHECK(plan.cwd == inst.dir); // world_dir/"world" resolves inside the instance
	CHECK(plan.exe == l.version_dir("v0.2.0") / binary_file_name(Binary::Server));
	REQUIRE(plan.args.size() >= 6);
	CHECK(plan.args[0] == "--config");
	CHECK(plan.args[1] == instance_server_toml(inst).string());
	CHECK(plan.args[2] == "--content-pack");
	CHECK(fs::path(plan.args[3]).is_absolute());
	CHECK(plan.args[4] == "--stop-file");
	CHECK(plan.args.back() != "--port"); // no override -> the config's port is used as is

	SUBCASE("overrides apply to this run only") {
		write(t.path / "mypack" / "pack.toml", "x");
		Overrides ov;
		ov.port = 27333;
		ov.pack = (t.path / "mypack").string();
		ov.extra_args = { "--seed", "42" };
		LaunchPlan p2;
		REQUIRE(plan_launch(l, inst, ov, p2));
		CHECK(p2.port == 27333);
		const auto has = [&](const std::string &a) {
			return std::find(p2.args.begin(), p2.args.end(), a) != p2.args.end();
		};
		CHECK(has("--port"));
		CHECK(has("27333"));
		CHECK(has((t.path / "mypack").string()));
		CHECK(p2.args[p2.args.size() - 2] == "--seed");
		CHECK(p2.args.back() == "42");
		CHECK(load_instance(l, "s")->pack == "builtin:base"); // unchanged
	}

	SUBCASE("missing version suggests installing it") {
		Overrides ov;
		ov.version = "v9.9.9";
		LaunchPlan p2;
		const Status s = plan_launch(l, inst, ov, p2);
		CHECK_FALSE(s);
		CHECK(s.error.find("vb install v9.9.9") != std::string::npos);
	}

	SUBCASE("a malformed server.toml fails before launch") {
		write(instance_server_toml(inst), "port = [this is not toml\n");
		LaunchPlan p2;
		const Status s = plan_launch(l, inst, {}, p2);
		CHECK_FALSE(s);
		CHECK(s.error.find("server.toml") != std::string::npos);
	}

	SUBCASE("no default version installed") {
		fs::remove(l.cli_toml());
		LaunchPlan p2;
		CHECK_FALSE(plan_launch(l, inst, {}, p2));
	}
}

TEST_CASE("running_record discards stale and reused-pid records") {
	TempDir t;
	const Layout l = test_layout(t.path);
	REQUIRE(create_instance(l, "s", "default", "builtin:base", std::nullopt));
	const Instance inst = *load_instance(l, "s");
	const fs::path rec = instance_run_dir(inst) / "server.pid";

	const auto token = process_start_token(own_pid());
	REQUIRE(token);

	// Our own pid with the right start token: a live record.
	write(rec, "pid = " + std::to_string(own_pid()) + "\nstart_token = " + std::to_string(*token) + "\nstarted = 1\nversion = \"v1.0.0\"\n");
	write(instance_run_dir(inst) / "stop", "stop");
	const auto live = running_record(inst);
	REQUIRE(live);
	CHECK(live->pid == own_pid());
	CHECK(live->version == "v1.0.0");
	CHECK(fs::exists(rec));

	// Same pid, different start token == the pid was reused: stale, cleaned up
	// (stop file included, so it can't end the next run instantly).
	write(rec, "pid = " + std::to_string(own_pid()) + "\nstart_token = " + std::to_string(*token + 12345) + "\nstarted = 1\nversion = \"v1.0.0\"\n");
	CHECK_FALSE(running_record(inst));
	CHECK_FALSE(fs::exists(rec));
	CHECK_FALSE(fs::exists(instance_run_dir(inst) / "stop"));

	// Garbage record.
	write(rec, "not = [valid");
	CHECK_FALSE(running_record(inst));
	CHECK_FALSE(fs::exists(rec));

	// remove_instance refuses while running.
	write(rec, "pid = " + std::to_string(own_pid()) + "\nstart_token = " + std::to_string(*token) + "\nstarted = 1\nversion = \"v1.0.0\"\n");
	const Status s = remove_instance(l, "s", false);
	CHECK_FALSE(s);
	CHECK(s.error.find("running") != std::string::npos);
}

TEST_CASE("stopping an instance that is not running is a no-op") {
	TempDir t;
	const Layout l = test_layout(t.path);
	REQUIRE(create_instance(l, "s", "default", "builtin:base", std::nullopt));
	const Run r = run_vb(l, { "server", "stop", "s" });
	CHECK(r.code == kExitOk);
	CHECK(r.out.find("not running") != std::string::npos);
	CHECK(run_vb(l, { "server", "stop", "missing" }).code == kExitFailure);
}

#if !defined(_WIN32)
TEST_CASE("udp_port_available sees a bound port") {
	const int s = socket(AF_INET, SOCK_DGRAM, 0);
	REQUIRE(s >= 0);
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = 0;
	REQUIRE(bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);
	socklen_t len = sizeof(addr);
	REQUIRE(getsockname(s, reinterpret_cast<sockaddr *>(&addr), &len) == 0);
	const auto port = ntohs(addr.sin_port);
	CHECK_FALSE(udp_port_available(port));
	close(s);
	CHECK(udp_port_available(port));
	CHECK(udp_port_available(0));
}

TEST_CASE("launch passes the per-user world dir, content pack and shared config") {
	TempDir t;
	const Layout l = test_layout(t.path);
	fake_install(l, "v0.2.0");
	REQUIRE(write_default_version(l, "v0.2.0"));
	// A stand-in client that records its arguments.
	const fs::path client = l.version_dir("v0.2.0") / binary_file_name(Binary::Client);
	write(client, "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$0.args\"\n");
	chmod(client.c_str(), 0755);

	REQUIRE(run_vb(l, { "launch", "--", "--singleplayer" }).code == 0);
	const std::string args = slurp(client.string() + ".args");
	CHECK(args.find("--config\n" + l.client_toml().string()) != std::string::npos);
	CHECK(args.find("--world-dir\n" + l.singleplayer_world_dir().string()) != std::string::npos);
	CHECK(args.find("--content-pack\n" +
				  fs::absolute(l.version_dir("v0.2.0") / "content" / "base").lexically_normal().string()) !=
			std::string::npos);
	CHECK(args.find("--singleplayer") != std::string::npos);

	// An explicit flag from the user wins.
	REQUIRE(run_vb(l, { "launch", "--", "--world-dir", "/elsewhere" }).code == 0);
	const std::string args2 = slurp(client.string() + ".args");
	CHECK(args2.find("/elsewhere") != std::string::npos);
	CHECK(args2.find(l.singleplayer_world_dir().string()) == std::string::npos);
}
#endif

TEST_CASE("uninstall and prune respect server instances") {
	TempDir t;
	const Layout l = test_layout(t.path);
	for (const char *v : { "v0.1.0", "v0.2.0", "v0.3.0" }) {
		fake_install(l, v);
	}
	REQUIRE(write_default_version(l, "v0.3.0"));
	REQUIRE(create_instance(l, "old", "v0.1.0", "builtin:base", std::nullopt));
	REQUIRE(create_instance(l, "follower", "default", "builtin:base", std::nullopt));

	const auto users = instance_version_users(l);
	REQUIRE(users.count("v0.1.0") == 1);
	CHECK(users.at("v0.1.0") == std::vector<std::string>{ "old" });
	CHECK(users.count("v0.3.0") == 0); // "default" followers pin nothing by themselves

	const Run denied = run_vb(l, { "uninstall", "0.1.0" });
	CHECK(denied.code == kExitFailure);
	CHECK(denied.err.find("old") != std::string::npos);
	CHECK(denied.err.find("--force") != std::string::npos);
	CHECK(fs::exists(l.version_dir("v0.1.0")));

	const Run pruned = run_vb(l, { "prune", "--keep", "1" });
	CHECK(pruned.code == kExitOk);
	CHECK(pruned.out.find("removed v0.2.0") != std::string::npos);
	CHECK(pruned.out.find("kept v0.1.0") != std::string::npos);
	CHECK(fs::exists(l.version_dir("v0.1.0")));
	CHECK_FALSE(fs::exists(l.version_dir("v0.2.0")));

	CHECK(run_vb(l, { "uninstall", "v0.1.0", "--force" }).code == kExitOk);
	CHECK_FALSE(fs::exists(l.version_dir("v0.1.0")));
}

TEST_CASE("server subcommands report usage errors") {
	TempDir t;
	const Layout l = test_layout(t.path);
	CHECK(run_vb(l, { "server" }).code == kExitUsage);
	CHECK(run_vb(l, { "server", "frobnicate" }).code == kExitUsage);
	CHECK(run_vb(l, { "server", "new" }).code == kExitUsage);
	CHECK(run_vb(l, { "server", "new", "a", "--port", "99999" }).code == kExitUsage);
	CHECK(run_vb(l, { "server", "new", "a", "--bogus" }).code == kExitUsage);
	CHECK(run_vb(l, { "server", "stop", "a", "--timeout", "soon" }).code == kExitUsage);
	CHECK(run_vb(l, { "host", "--port", "0" }).code == kExitUsage);
	CHECK(run_vb(l, { "host", "stray" }).code == kExitUsage);
	CHECK(run_vb(l, { "server", "list" }).code == kExitOk);
}

TEST_CASE("server rm protects a saved world") {
	TempDir t;
	const Layout l = test_layout(t.path);
	REQUIRE(run_vb(l, { "server", "new", "w" }).code == kExitOk);
	const Instance inst = *load_instance(l, "w");
	write(instance_world_dir(inst) / "r.0.0.vbr", "precious");

	const Run refused = run_vb(l, { "server", "rm", "w" });
	CHECK(refused.code == kExitFailure);
	CHECK(refused.err.find("--yes") != std::string::npos);
	CHECK(fs::exists(instance_world_dir(inst) / "r.0.0.vbr"));

	CHECK(run_vb(l, { "server", "rm", "w", "--keep-world" }).code == kExitOk);
	CHECK(fs::exists(instance_world_dir(inst) / "r.0.0.vbr"));
	CHECK(run_vb(l, { "server", "new", "w" }).code == kExitOk);
	CHECK(run_vb(l, { "server", "rm", "w", "--yes" }).code == kExitOk);
	CHECK_FALSE(fs::exists(inst.dir));
}

TEST_CASE("tail_log returns the last lines") {
	TempDir t;
	const Layout l = test_layout(t.path);
	REQUIRE(create_instance(l, "s", "default", "builtin:base", std::nullopt));
	const Instance inst = *load_instance(l, "s");
	CHECK(tail_log(inst, 3).empty()); // no log yet
	write(instance_log_file(inst), "one\ntwo\nthree\nfour\nfive\n");
	CHECK(tail_log(inst, 2) == "four\nfive\n");
	CHECK(tail_log(inst, 99) == "one\ntwo\nthree\nfour\nfive\n");
	write(instance_log_file(inst), "no trailing newline");
	CHECK(tail_log(inst, 1) == "no trailing newline\n");
}

// ---- integration: the real voxel_browser_server -------------------------------
#if defined(VB_TEST_SERVER_EXE)

namespace {

struct RealServer {
	TempDir t;
	Layout layout = test_layout(t.path);
	fs::path exe = fs::path(VB_TEST_SERVER_EXE);
	std::uint16_t port = free_udp_port();

	RealServer() {
		REQUIRE(port != 0);
		// Both binaries of this build tree are what a `vb link`ed version offers.
		REQUIRE(add_link(layout, "dev", exe.parent_path()));
		REQUIRE(write_default_version(layout, "dev"));
	}
};

} // namespace

TEST_CASE("integration: --stop-file stops a detached server cleanly") {
	RealServer rs;
	const fs::path log = rs.t.path / "logs" / "server.log";
	const fs::path stop = rs.t.path / "stop";

	const SpawnResult sp = spawn_detached(rs.exe,
			{ "--port", std::to_string(rs.port), "--content-pack",
					std::string(VB_PROJECT_SOURCE_DIR) + "/content/base", "--stop-file",
					stop.string() },
			rs.t.path, log);
	REQUIRE(sp.error.empty());
	REQUIRE(sp.pid != 0);
	REQUIRE(process_start_token(sp.pid));
	REQUIRE(wait_for([&] { return slurp(log).find("server: bind") != std::string::npos; }, 20));

	write(stop, "stop"); // no signal: exactly what a Windows `vb server stop` has
	CHECK(wait_for([&] { return !process_start_token(sp.pid); }, 15));
	const std::string text = slurp(log);
	CHECK(text.find("stop file") != std::string::npos);
	// Printed after the final autosave sweep, i.e. the save-on-shutdown path ran.
	CHECK(text.find("server: stopped after") != std::string::npos);
}

TEST_CASE("integration: vb server new/start/stop") {
	RealServer rs;
	const std::string pack = std::string(VB_PROJECT_SOURCE_DIR) + "/content/base";
	REQUIRE(run_vb(rs.layout, { "server", "new", "it", "--pack", pack, "--port", std::to_string(rs.port) })
					.code == kExitOk);

	const Run started = run_vb(rs.layout, { "server", "start", "it" });
	INFO(started.out << started.err);
	REQUIRE(started.code == kExitOk);
	CHECK(started.out.find("started 'it'") != std::string::npos);

	const Instance inst = *load_instance(rs.layout, "it");
	const auto rec = running_record(inst);
	REQUIRE(rec);
	CHECK(rec->version == "dev");

	CHECK(run_vb(rs.layout, { "server", "status", "it" }).out.find("running") != std::string::npos);
	const Run again = run_vb(rs.layout, { "server", "start", "it" });
	CHECK(again.code == kExitFailure);
	CHECK(again.err.find("already running") != std::string::npos);
	// Pinned/running instance blocks removal of its version.
	CHECK(run_vb(rs.layout, { "uninstall", "dev" }).code == kExitFailure);

	REQUIRE(wait_for([&] { return slurp(instance_log_file(inst)).find("server: bind") != std::string::npos; }, 20));
	const Run stopped = run_vb(rs.layout, { "server", "stop", "it" });
	INFO(stopped.out << stopped.err);
	CHECK(stopped.code == kExitOk);
	CHECK_FALSE(process_start_token(rec->pid));
	CHECK_FALSE(running_record(inst));
	CHECK(slurp(instance_log_file(inst)).find("server: stopped after") != std::string::npos);
	CHECK(run_vb(rs.layout, { "server", "status", "it" }).out.find("stopped") != std::string::npos);

	// And it can be restarted on the same world dir.
	REQUIRE(run_vb(rs.layout, { "server", "restart", "it" }).code == kExitOk);
	CHECK(run_vb(rs.layout, { "server", "stop", "it", "--timeout", "30" }).code == kExitOk);
}

TEST_CASE("integration: vb host runs in the foreground and cleans up") {
	RealServer rs;
	const Run r = run_vb(rs.layout, { "host", "--port", std::to_string(rs.port), "--pack", std::string(VB_PROJECT_SOURCE_DIR) + "/content/base", "--", "--ticks", "10" });
	INFO(r.out << r.err);
	CHECK(r.code == kExitOk);
	CHECK(r.out.find("hosting 'default'") != std::string::npos);
	const Instance inst = *load_instance(rs.layout, "default");
	CHECK_FALSE(running_record(inst));
	CHECK(inst.pack == "builtin:base"); // --pack was per-run, not saved
}

#endif

TEST_CASE("resolve_beside_program falls back to the executable's directory") {
	TempDir t;
	write(t.path / "install" / "content" / "base" / "pack.toml", "x");
	fs::create_directories(t.path / "empty_cwd");
	const fs::path exe = t.path / "install" / "voxel_browser";

	// The result depends on the working directory (ctest runs from the build
	// dir, which may itself have a content/), so pin it for the whole test.
	const fs::path saved_cwd = fs::current_path();
	fs::current_path(t.path / "empty_cwd");

	// Found beside the executable, from a working directory that has no content/.
	CHECK(vb::core::resolve_beside_program("content/base", exe) ==
			fs::absolute(exe).parent_path() / "content/base");
	// Neither place has it: unchanged, so the later error names the expected path.
	CHECK(vb::core::resolve_beside_program("no/such/dir", exe) == fs::path("no/such/dir"));
	// No program path known: unchanged.
	CHECK(vb::core::resolve_beside_program("content/base", {}) == fs::path("content/base"));
	// Exists relative to the working directory: that wins over the exe's dir.
	fs::current_path(t.path / "install");
	CHECK(vb::core::resolve_beside_program("content/base", t.path / "elsewhere" / "exe") ==
			fs::path("content/base"));

	fs::current_path(saved_cwd); // before TempDir removes the directory we were in
}
