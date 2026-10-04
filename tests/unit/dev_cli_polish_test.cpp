// Phase 8.5: polish -- remote listing, --json, self update, shims, server
// status file, server config, log rotation, completions, pack watching.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <thread>

#include <miniz.h>
#include <nlohmann/json.hpp>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include "vb/cli/commands.hpp"
#include "vb/cli/completions.hpp"
#include "vb/cli/installer.hpp"
#include "vb/cli/instance.hpp"
#include "vb/cli/layout.hpp"
#include "vb/cli/portcheck.hpp"
#include "vb/cli/process.hpp"
#include "vb/cli/self_update.hpp"
#include "vb/cli/server_config.hpp"
#include "vb/cli/shim.hpp"
#include "vb/cli/source.hpp"
#include "vb/cli/store.hpp"
#include "vb/cli/watch.hpp"
#include "vb/core/sha256.hpp"

namespace fs = std::filesystem;
using namespace vb::cli;
using json = nlohmann::json;

namespace {

struct TempDir {
	fs::path path;
	TempDir() {
		std::random_device rd;
		path = fs::temp_directory_path() / ("vb_polish_test_" + std::to_string(rd()));
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

std::string slurp(const fs::path &p) {
	std::ifstream f(p, std::ios::binary);
	std::ostringstream os;
	os << f.rdbuf();
	return os.str();
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

std::int64_t now_unix() {
	return std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count();
}

std::string vb_name() {
#if defined(_WIN32)
	return "vb.exe";
#else
	return "vb";
#endif
}

// A one-file zip (miniz's own writer is fine here: names are benign).
void make_zip(const fs::path &zip, const std::string &name, const std::string &payload) {
	REQUIRE(mz_zip_add_mem_to_archive_file_in_place(zip.string().c_str(), name.c_str(),
					payload.data(), payload.size(), nullptr, 0, MZ_BEST_SPEED) != 0);
}

// A release directory serving `version`'s vb (CLI) archive and, optionally, a
// tampered manifest hash.
fs::path make_cli_release(const fs::path &root, const std::string &version,
		const std::string &new_vb_bytes, bool bad_hash = false) {
	const fs::path dir = root / ("rel-" + version);
	fs::create_directories(dir);
	const std::string file = "vb-" + version + "-linux-x86_64.zip";
	make_zip(dir / file, vb_name(), new_vb_bytes);
	const std::string bytes = slurp(dir / file);
	std::ofstream m(dir / "release.toml");
	m << "schema = 1\nversion = \"" << version << "\"\ncommit = \"abc\"\n"
	  << "date = \"2026-10-04T00:00:00Z\"\nengine_protocol_version = 26\n\n"
	  << "[[artifact]]\nkind = \"cli\"\nplatform = \"linux-x86_64\"\nbuild = \"release\"\nfile = \""
	  << file << "\"\nsize = " << bytes.size() << "\nsha256 = \""
	  << (bad_hash ? std::string(64, '0') : vb::core::sha256_hex(bytes)) << "\"\n";
	return dir;
}

} // namespace

// ---- remote listing --------------------------------------------------------

TEST_CASE("parse_release_list keeps published release tags, newest first") {
	std::vector<std::string> out;
	const std::string body = R"([
		{"tag_name": "v0.2.0"},
		{"tag_name": "v0.10.0"},
		{"tag_name": "v0.3.0-rc1", "prerelease": true},
		{"tag_name": "v0.4.0", "draft": true},
		{"tag_name": "nightly"},
		{"tag_name": "v0.2.0"},
		{"tag_name": "v0.9.1"},
		{"name": "no tag at all"},
		42
	])";
	REQUIRE(parse_release_list(body, out));
	CHECK(out == std::vector<std::string>{ "v0.10.0", "v0.9.1", "v0.2.0" });

	out.clear();
	REQUIRE(parse_release_list("[]", out));
	CHECK(out.empty());
	CHECK_FALSE(parse_release_list("{\"message\": \"rate limited\"}", out));
	CHECK_FALSE(parse_release_list("<html>", out));
}

TEST_CASE("list --remote and --json") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const fs::path rel = make_cli_release(t.path, "v0.2.0", "binary");
	write(l.cli_toml(), "source = \"dir:" + rel.generic_string() + "\"\n");

	const Run text = run_vb(l, { "list", "--remote" });
	CHECK(text.code == kExitOk);
	CHECK(text.out.find("v0.2.0  (latest)") != std::string::npos);
	CHECK(text.out.find("installed") == std::string::npos);

	write(l.version_dir("v0.2.0") / binary_file_name(Binary::Server), "x");
	const json arr = json::parse(run_vb(l, { "list", "--remote", "--json" }).out);
	REQUIRE(arr.size() == 1);
	CHECK(arr[0]["version"] == "v0.2.0");
	CHECK(arr[0]["installed"] == true);

	write(l.cli_toml(), "source = \"dir:" + (t.path / "nowhere").generic_string() + "\"\n");
	CHECK(run_vb(l, { "list", "--remote" }).code == kExitFailure);
}

// ---- --json ----------------------------------------------------------------

TEST_CASE("--json output is valid and complete") {
	TempDir t;
	const Layout l = test_layout(t.path);
	write(l.version_dir("v0.1.0") / binary_file_name(Binary::Client), "x");
	write(l.version_dir("v0.1.0") / binary_file_name(Binary::Server), "x");
	REQUIRE(write_default_version(l, "v0.1.0"));

	const json paths = json::parse(run_vb(l, { "paths", "--json" }).out);
	CHECK(paths["data"] == l.data().string());
	CHECK(paths["config"] == l.config().string());
	CHECK(paths["cache"] == l.cache().string());

	const json list = json::parse(run_vb(l, { "list", "--json" }).out);
	REQUIRE(list.size() == 1);
	CHECK(list[0]["name"] == "v0.1.0");
	CHECK(list[0]["kind"] == "release");
	CHECK(list[0]["default"] == true);

	const json which = json::parse(run_vb(l, { "which", "server", "--json" }).out);
	CHECK(which["kind"] == "server");
	CHECK(which["version"] == "v0.1.0");
	CHECK(which["path"] == (l.version_dir("v0.1.0") / binary_file_name(Binary::Server)).string());

	CHECK(json::parse(run_vb(l, { "list", "--json" }).out).is_array());
	CHECK(json::parse(run_vb(l, { "server", "list", "--json" }).out).empty()); // no servers

	REQUIRE(run_vb(l, { "server", "new", "s", "--port", "27999" }).code == kExitOk);
	const json one = json::parse(run_vb(l, { "server", "status", "s", "--json" }).out);
	CHECK(one["name"] == "s");
	CHECK(one["running"] == false);
	CHECK(one["port"] == 27999);
	CHECK(one["version"] == "default");
	CHECK_FALSE(one.contains("pid"));
	const json all = json::parse(run_vb(l, { "server", "status", "--json" }).out);
	REQUIRE(all.size() == 1);
	CHECK(all[0]["name"] == "s");

	CHECK(run_vb(l, { "paths", "extra" }).code == kExitUsage);
}

// ---- server status file ----------------------------------------------------

TEST_CASE("read_server_status parses what the server writes") {
	TempDir t;
	const Layout l = test_layout(t.path);
	REQUIRE(create_instance(l, "s", "default", "builtin:base", std::nullopt));
	const Instance inst = *load_instance(l, "s");
	CHECK_FALSE(read_server_status(inst)); // nothing yet

	write(instance_status_file(inst),
			"running = true\nupdated = 1000\nuptime_seconds = 90\ntick = 1800\n"
			"target_tick_rate = 20\ntick_rate = 19.8\nmax_players = 16\n"
			"seed = \"15435051815007170829\"\nmotd = \"hi \\\"there\\\"\"\n"
			"players = [\"Ann\", \"b\\\\ob\"]\n");
	const auto st = read_server_status(inst);
	REQUIRE(st);
	CHECK(st->running);
	CHECK(st->uptime_seconds == 90);
	CHECK(st->target_tick_rate == 20);
	CHECK(st->tick_rate == doctest::Approx(19.8));
	CHECK(st->seed == "15435051815007170829"); // above int64: stored as a string
	CHECK(st->motd == "hi \"there\"");
	CHECK(st->players == std::vector<std::string>{ "Ann", "b\\ob" });

	CHECK(status_is_fresh(*st, 1010));
	CHECK_FALSE(status_is_fresh(*st, 1100)); // stale: hung server or old binary
	ServerStatus stopped = *st;
	stopped.running = false;
	CHECK_FALSE(status_is_fresh(stopped, 1001)); // clean shutdown record

	write(instance_status_file(inst), "running = [oops");
	CHECK_FALSE(read_server_status(inst));
}

// ---- log rotation ----------------------------------------------------------

TEST_CASE("rotate_log rotates only oversized logs and keeps a bounded history") {
	TempDir t;
	const Layout l = test_layout(t.path);
	REQUIRE(create_instance(l, "s", "default", "builtin:base", std::nullopt));
	const Instance inst = *load_instance(l, "s");
	const fs::path log = instance_log_file(inst);
	const auto numbered = [&](int n) { return fs::path(log.string() + "." + std::to_string(n)); };

	write(log, "small");
	rotate_log(inst, 100, 3);
	CHECK(slurp(log) == "small"); // under the limit: untouched

	for (int generation = 1; generation <= 5; ++generation) {
		write(log, "gen" + std::to_string(generation) + std::string(200, 'x'));
		rotate_log(inst, 100, 3);
		CHECK_FALSE(fs::exists(log)); // the server will start a fresh one
	}
	CHECK(slurp(numbered(1)).rfind("gen5", 0) == 0);
	CHECK(slurp(numbered(2)).rfind("gen4", 0) == 0);
	CHECK(slurp(numbered(3)).rfind("gen3", 0) == 0);
	CHECK_FALSE(fs::exists(numbered(4))); // oldest dropped
	rotate_log(inst, 100, 3); // nothing to rotate when there is no log
}

// ---- server config ---------------------------------------------------------

TEST_CASE("server config get / set / unset") {
	TempDir t;
	const fs::path f = t.path / "server.toml";
	write(f, "# my server\nport = 27015\n\nmax_players = 16   # people\n");

	CHECK(get_server_config_value(f, "port") == "27015");
	CHECK(get_server_config_value(f, "view_distance") == "4"); // engine default
	std::string why;
	CHECK_FALSE(get_server_config_value(f, "nope", &why));
	CHECK(why.find("unknown key") != std::string::npos);

	REQUIRE(set_server_config_value(f, "max_players", "32"));
	REQUIRE(set_server_config_value(f, "motd", "Welcome \"home\""));
	REQUIRE(set_server_config_value(f, "persist_world", "false"));
	REQUIRE(set_server_config_value(f, "gravity", "-9.5"));
	REQUIRE(set_server_config_value(f, "auth_mode", "token"));
	CHECK(get_server_config_value(f, "max_players") == "32");
	CHECK(get_server_config_value(f, "motd") == "Welcome \"home\"");
	CHECK(get_server_config_value(f, "persist_world") == "false");
	CHECK(get_server_config_value(f, "gravity") == "-9.5");
	CHECK(get_server_config_value(f, "auth_mode") == "token");
	const std::string text = slurp(f);
	CHECK(text.find("# my server") != std::string::npos); // comments survive
	CHECK(text.find("port = 27015") != std::string::npos);

	// Rejected values leave the file byte-for-byte alone.
	const std::string before = slurp(f);
	CHECK_FALSE(set_server_config_value(f, "port", "eighty"));
	CHECK_FALSE(set_server_config_value(f, "port", "-1"));
	CHECK_FALSE(set_server_config_value(f, "persist_world", "maybe"));
	CHECK_FALSE(set_server_config_value(f, "gravity", "heavy"));
	CHECK_FALSE(set_server_config_value(f, "auth_mode", "magic"));
	CHECK_FALSE(set_server_config_value(f, "bogus", "1"));
	CHECK(slurp(f) == before);
	CHECK_FALSE(fs::exists(fs::path(f.string() + ".check")));

	REQUIRE(unset_server_config_key(f, "max_players"));
	CHECK(get_server_config_value(f, "max_players") == "16"); // back to the default
	CHECK_FALSE(unset_server_config_key(f, "bogus"));

	// Setting a key in a file that does not exist yet creates it.
	const fs::path fresh = t.path / "sub" / "new.toml";
	REQUIRE(set_server_config_value(fresh, "port", "28000"));
	CHECK(get_server_config_value(fresh, "port") == "28000");

	const auto dump = dump_server_config(f);
	REQUIRE(dump);
	CHECK(dump->find("motd = \"Welcome \\\"home\\\"\"") != std::string::npos);
	CHECK(dump->find("persist_world = false") != std::string::npos);
}

TEST_CASE("vb server config command") {
	TempDir t;
	const Layout l = test_layout(t.path);
	REQUIRE(run_vb(l, { "server", "new", "s", "--port", "27500" }).code == kExitOk);
	CHECK(run_vb(l, { "server", "config", "s", "get", "port" }).out == "27500\n");
	CHECK(run_vb(l, { "server", "config", "s", "set", "gravity", "-5" }).code == kExitOk);
	CHECK(run_vb(l, { "server", "config", "s", "get", "gravity" }).out == "-5\n");
	CHECK(run_vb(l, { "server", "config", "s" }).out.find("port = 27500") != std::string::npos);
	CHECK(run_vb(l, { "server", "config", "s", "set", "port", "x" }).code == kExitFailure);
	CHECK(run_vb(l, { "server", "config", "s", "unset", "gravity" }).code == kExitOk);
	CHECK(run_vb(l, { "server", "config", "s", "frob" }).code == kExitUsage);
	CHECK(run_vb(l, { "server", "config" }).code == kExitUsage);
	CHECK(run_vb(l, { "server", "config", "missing", "get" }).code == kExitFailure);
}

// ---- pack watching ---------------------------------------------------------

TEST_CASE("snapshot_pack ignores runtime state and detects real changes") {
	TempDir t;
	const fs::path pack = t.path / "pack";
	write(pack / "pack.toml", "name = \"x\"");
	write(pack / "blocks" / "dirt.lua", "-- dirt");
	write(pack / "storage.json", "{}"); // written by the pack itself
	write(pack / "db" / "data.bin", "1"); // ditto
	write(pack / ".git" / "HEAD", "ref"); // not content
	write(pack / "blocks" / ".dirt.lua.swp", "swap");

	const PackSnapshot a = snapshot_pack(pack);
	CHECK(a.size() == 2);
	CHECK(a.count("pack.toml") == 1);
	CHECK(a.count("blocks/dirt.lua") == 1);
	CHECK(describe_changes(a, snapshot_pack(pack)).empty());

	write(pack / "storage.json", "{\"boot\": 2}");
	write(pack / "db" / "data.bin", "22");
	CHECK(snapshot_pack(pack) == a); // state-file churn is invisible

	write(pack / "blocks" / "dirt.lua", "-- dirt, edited");
	write(pack / "blocks" / "stone.lua", "-- new");
	fs::remove(pack / "pack.toml");
	const std::string diff = describe_changes(a, snapshot_pack(pack));
	CHECK(diff.find("changed blocks/dirt.lua") != std::string::npos);
	CHECK(diff.find("added blocks/stone.lua") != std::string::npos);
	CHECK(diff.find("removed pack.toml") != std::string::npos);
	CHECK(snapshot_pack(t.path / "missing").empty());
}

// ---- completions -----------------------------------------------------------

TEST_CASE("completion scripts are generated from the command table") {
	CompletionSpec spec;
	spec.commands = { { "install", "download a release" }, { "server", "manage servers" } };
	spec.server_subcommands = { "start", "stop" };
	spec.config_subcommands = { "get", "set" };
	for (const char *shell : { "bash", "zsh", "fish", "powershell" }) {
		const std::string text = generate_completions(shell, spec);
		INFO(shell);
		CHECK_FALSE(text.empty());
		CHECK(text.find("install") != std::string::npos);
		CHECK(text.find("stop") != std::string::npos);
		CHECK(text.find("__complete") != std::string::npos); // dynamic names
	}
	CHECK(generate_completions("pwsh", spec) == generate_completions("powershell", spec));
	CHECK(generate_completions("tcsh", spec).empty());

	// An apostrophe in a summary must not break the quoting of zsh / fish.
	spec.commands = { { "x", "it's quoted" } };
	CHECK(generate_completions("zsh", spec).find("it''s quoted") != std::string::npos);
	CHECK(generate_completions("fish", spec).find("it''s quoted") != std::string::npos);

	TempDir t;
	const Layout l = test_layout(t.path);
	const Run real = run_vb(l, { "completions", "bash" });
	CHECK(real.code == kExitOk);
	for (const char *cmd : { "host", "server", "self", "shim", "completions", "list", "install" }) {
		CHECK(real.out.find(cmd) != std::string::npos);
	}
	CHECK(real.out.find("config") != std::string::npos); // server subcommand
	CHECK(run_vb(l, { "completions", "tcsh" }).code == kExitUsage);
	CHECK(run_vb(l, { "completions" }).code == kExitUsage);

	// The hidden helper feeds the scripts live names.
	REQUIRE(run_vb(l, { "server", "new", "alpha" }).code == kExitOk);
	write(l.version_dir("v0.1.0") / binary_file_name(Binary::Server), "x");
	CHECK(run_vb(l, { "__complete", "instances" }).out == "alpha\n");
	CHECK(run_vb(l, { "__complete", "versions" }).out == "v0.1.0\n");
	CHECK(run_vb(l, { "__complete", "bogus" }).code == kExitUsage);
}

// ---- shims -----------------------------------------------------------------

TEST_CASE("shim scripts") {
	const fs::path vb = fs::path("/opt/it's here/vb");
	const std::string client = shim_script(vb, false, false);
	const std::string server = shim_script(vb, true, false);
	CHECK(client.rfind("#!/bin/sh", 0) == 0);
	CHECK(client.find("'/opt/it'\\''s here/vb' launch -- \"$@\"") != std::string::npos);
	CHECK(server.find("which server") != std::string::npos);
	CHECK(server.find("exec \"$srv\" \"$@\"") != std::string::npos);
	const std::string win = shim_script("C:\\Users\\me\\vb.exe", true, true);
	CHECK(win.find("@echo off") != std::string::npos);
	CHECK(win.find("\"C:\\Users\\me\\vb.exe\" which server") != std::string::npos);
	CHECK(win.find("%*") != std::string::npos);

	TempDir t;
	const Layout l = test_layout(t.path);
	std::vector<fs::path> files;
	CHECK_FALSE(install_shims(l, "relative/vb", files)); // needs an absolute path
	REQUIRE(install_shims(l, t.path / "vb", files));
	REQUIRE(files.size() == 2);
	for (const fs::path &f : files) {
		CHECK(fs::exists(f));
		CHECK(f.parent_path() == shim_dir(l));
	}
	write(shim_dir(l) / "unrelated", "keep me");
	std::vector<fs::path> removed;
	REQUIRE(remove_shims(l, removed));
	CHECK(removed.size() == 2);
	CHECK(fs::exists(shim_dir(l) / "unrelated")); // only our own files go
	removed.clear();
	REQUIRE(remove_shims(l, removed));
	CHECK(removed.empty());
}

#if defined(VB_TEST_VB_EXE) && !defined(_WIN32)
TEST_CASE("a shim runs the default version through vb") {
	TempDir t;
	// A VB_HOME-shaped layout, because the shim's vb finds its data via VB_HOME.
	const Layout l(t.path, t.path / "config", t.path / "cache");
	const fs::path build = t.path / "build";
	const fs::path client = build / binary_file_name(Binary::Client);
	const fs::path record = t.path / "argv.txt";
	write(client, "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + record.string() + "'\n");
	chmod(client.c_str(), 0755);
	REQUIRE(add_link(l, "dev", build));
	REQUIRE(write_default_version(l, "dev"));

	std::vector<fs::path> files;
	REQUIRE(install_shims(l, fs::path(VB_TEST_VB_EXE), files));

	const char *old = std::getenv("VB_HOME");
	const std::string saved = old != nullptr ? old : "";
	setenv("VB_HOME", t.path.c_str(), 1);
	const RunResult r = run_foreground(shim_dir(l) / "voxel_browser", { "--singleplayer", "--name", "Zed" });
	if (old != nullptr) {
		setenv("VB_HOME", saved.c_str(), 1);
	} else {
		unsetenv("VB_HOME");
	}
	CHECK(r.error.empty());
	CHECK(r.exit_code == 0);
	const std::string args = slurp(record);
	CHECK(args.find("--singleplayer\n--name\nZed") != std::string::npos);
	CHECK(args.find("--world-dir") != std::string::npos); // went through `vb launch`
}
#endif

// ---- self update -----------------------------------------------------------

TEST_CASE("self update replaces vb with a verified newer release") {
	TempDir t;
	const Layout l = test_layout(t.path);
	const fs::path target = t.path / "bin" / vb_name();
	write(target, "OLD vb");
	const fs::path rel = make_cli_release(t.path, "v0.2.0", "NEW vb");
	const auto source = make_dir_source(rel);

	SelfUpdateOptions opts;
	opts.target = target;
	opts.platform = "linux-x86_64";
	opts.run_version_check = false; // placeholder binary

	SUBCASE("newer release") {
		opts.current_version = "v0.1.0";
		opts.check_only = true;
		SelfUpdateResult r = self_update(l, *source, opts);
		REQUIRE(r.status);
		CHECK(r.latest == "v0.2.0");
		CHECK_FALSE(r.up_to_date);
		CHECK_FALSE(r.updated);
		CHECK(slurp(target) == "OLD vb"); // --check changes nothing

		opts.check_only = false;
		r = self_update(l, *source, opts);
		REQUIRE(r.status);
		CHECK(r.updated);
		CHECK(slurp(target) == "NEW vb");
		CHECK_FALSE(fs::exists(fs::path(target.string() + ".new")));
		CHECK(fs::is_empty(l.downloads_dir())); // archive and staging cleaned up
#if !defined(_WIN32)
		struct stat st {};
		REQUIRE(stat(target.c_str(), &st) == 0);
		CHECK((st.st_mode & 0100) != 0); // executable
#endif
	}

	SUBCASE("already current or newer: nothing happens") {
		for (const char *current : { "v0.2.0", "v0.3.0" }) {
			opts.current_version = current;
			const SelfUpdateResult r = self_update(l, *source, opts);
			REQUIRE(r.status);
			CHECK(r.up_to_date);
			CHECK_FALSE(r.updated);
			CHECK(slurp(target) == "OLD vb");
		}
		opts.force = true; // --force reinstalls
		opts.current_version = "v0.2.0";
		CHECK(self_update(l, *source, opts).updated);
		CHECK(slurp(target) == "NEW vb");
	}

	SUBCASE("a development build is not replaced without --force") {
		opts.current_version = "2655e1d-dirty";
		const SelfUpdateResult r = self_update(l, *source, opts);
		CHECK_FALSE(r.status);
		CHECK(r.status.error.find("--force") != std::string::npos);
		CHECK(slurp(target) == "OLD vb");
		opts.force = true;
		CHECK(self_update(l, *source, opts).updated);
	}

	SUBCASE("a tampered download is refused and the old vb stays") {
		const fs::path bad = make_cli_release(t.path / "bad", "v0.2.0", "EVIL", true);
		const auto bad_source = make_dir_source(bad);
		opts.current_version = "v0.1.0";
		const SelfUpdateResult r = self_update(l, *bad_source, opts);
		CHECK_FALSE(r.status);
		CHECK(r.status.error.find("SHA-256 mismatch") != std::string::npos);
		CHECK(slurp(target) == "OLD vb");
	}

	SUBCASE("no build for this platform") {
		opts.current_version = "v0.1.0";
		opts.platform = "windows-x86_64";
		const SelfUpdateResult r = self_update(l, *source, opts);
		CHECK_FALSE(r.status);
		CHECK(r.status.error.find("windows-x86_64") != std::string::npos);
		CHECK(slurp(target) == "OLD vb");
	}

	SUBCASE("an archive without vb inside") {
		const fs::path odd = t.path / "odd";
		fs::create_directories(odd);
		make_zip(odd / "vb-v0.2.0-linux-x86_64.zip", "something_else", "x");
		const std::string bytes = slurp(odd / "vb-v0.2.0-linux-x86_64.zip");
		std::ofstream(odd / "release.toml")
				<< "schema = 1\nversion = \"v0.2.0\"\ncommit = \"a\"\ndate = \"d\"\n"
				   "engine_protocol_version = 26\n\n[[artifact]]\nkind = \"cli\"\n"
				   "platform = \"linux-x86_64\"\nbuild = \"release\"\nfile = \"vb-v0.2.0-linux-x86_64.zip\"\n"
				<< "size = " << bytes.size() << "\nsha256 = \"" << vb::core::sha256_hex(bytes) << "\"\n";
		const auto odd_source = make_dir_source(odd);
		opts.current_version = "v0.1.0";
		CHECK_FALSE(self_update(l, *odd_source, opts).status);
		CHECK(slurp(target) == "OLD vb");
	}
}

TEST_CASE("current_executable_path points at a real file") {
	const fs::path self = current_executable_path();
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
	CHECK(self.is_absolute());
	CHECK(fs::is_regular_file(self));
#else
	(void)self;
#endif
}

// ---- integration: the real server ------------------------------------------
#if defined(VB_TEST_SERVER_EXE)

namespace {

struct RealServer {
	TempDir t;
	Layout layout = test_layout(t.path);
	fs::path exe = fs::path(VB_TEST_SERVER_EXE);
	std::uint16_t port = 0;
	std::string pack = std::string(VB_PROJECT_SOURCE_DIR) + "/content/base";

	RealServer() {
		std::mt19937 rng(std::random_device{}());
		for (int i = 0; i < 200 && port == 0; ++i) {
			const auto candidate = static_cast<std::uint16_t>(20000 + rng() % 30000);
			if (udp_port_available(candidate)) {
				port = candidate;
			}
		}
		REQUIRE(port != 0);
		REQUIRE(add_link(layout, "dev", exe.parent_path()));
		REQUIRE(write_default_version(layout, "dev"));
	}
};

} // namespace

TEST_CASE("integration: the server writes a status file that vb reads") {
	RealServer rs;
	REQUIRE(run_vb(rs.layout, { "server", "new", "st", "--pack", rs.pack, "--port", std::to_string(rs.port) })
					.code == kExitOk);
	REQUIRE(run_vb(rs.layout, { "server", "start", "st" }).code == kExitOk);
	const Instance inst = *load_instance(rs.layout, "st");

	REQUIRE(wait_for([&] {
		const auto st = read_server_status(inst);
		return st && status_is_fresh(*st, now_unix());
	},
			20));
	const auto st = *read_server_status(inst);
	CHECK(st.running);
	CHECK(st.target_tick_rate == 20);
	CHECK(st.max_players == 16);
	CHECK_FALSE(st.seed.empty());
	CHECK(st.players.empty());

	const Run text = run_vb(rs.layout, { "server", "status", "st" });
	CHECK(text.out.find("players: 0/16") != std::string::npos);
	CHECK(text.out.find("/ 20 Hz") != std::string::npos);
	const json j = json::parse(run_vb(rs.layout, { "server", "status", "st", "--json" }).out);
	CHECK(j["running"] == true);
	CHECK(j["max_players"] == 16);
	CHECK(j["players"].is_array());
	CHECK(run_vb(rs.layout, { "server", "list" }).out.find("0/16") != std::string::npos);

	REQUIRE(run_vb(rs.layout, { "server", "stop", "st" }).code == kExitOk);
	CHECK_FALSE(fs::exists(instance_status_file(inst))); // cleaned with the run files
	CHECK(run_vb(rs.layout, { "server", "status", "st", "--json" }).out.find("\"running\": false") !=
			std::string::npos);
}

TEST_CASE("integration: host --watch restarts the server when the pack changes") {
	RealServer rs;
	const fs::path pack = rs.t.path / "pack";
	fs::create_directories(pack);
	for (const auto &e : fs::recursive_directory_iterator(rs.pack)) {
		if (e.is_regular_file() && e.path().filename() != "storage.json") {
			const fs::path dest = pack / e.path().lexically_relative(rs.pack);
			fs::create_directories(dest.parent_path());
			fs::copy_file(e.path(), dest, fs::copy_options::overwrite_existing);
		}
	}

	Run result{ -1, "", "" };
	std::thread host([&] {
		result = run_vb(rs.layout, { "host", "--watch", "--pack", pack.string(), "--port", std::to_string(rs.port) });
	});
	const auto instance = [&]() -> std::optional<Instance> { return load_instance(rs.layout, "default"); };
	const auto pid_now = [&]() -> Pid {
		const auto inst = instance();
		const auto rec = inst ? running_record(*inst) : std::nullopt;
		return rec ? rec->pid : 0;
	};

	// "Ready" = the server has written its first status file (it is past setup).
	const auto ready = [&] {
		const auto inst = instance();
		return inst && pid_now() != 0 && fs::exists(instance_status_file(*inst));
	};
	REQUIRE(wait_for(ready, 30));
	const Pid first = pid_now();

	// State files the pack writes itself must not restart anything...
	write(pack / "storage.json", "{\"x\": 1}");
	std::this_thread::sleep_for(std::chrono::milliseconds(1800));
	CHECK(pid_now() == first);

	// ...but a content edit must.
	write(pack / "edited_by_test.lua", "-- changed");
	CHECK(wait_for([&] {
		const Pid p = pid_now();
		return p != 0 && p != first;
	},
			40));

	// Stopping the instance ends the watch loop (clean exit, no restart).
	REQUIRE(wait_for(ready, 30));
	const Run stopped = run_vb(rs.layout, { "server", "stop", "default" });
	CHECK(stopped.code == kExitOk);
	host.join();
	CHECK(result.code == kExitOk);
	CHECK(result.out.find("pack changed (added edited_by_test.lua)") != std::string::npos);
	CHECK_FALSE(running_record(*instance()));
}

#endif
