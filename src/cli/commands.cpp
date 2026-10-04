#include "vb/cli/commands.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <thread>

#include <nlohmann/json.hpp>
#if defined(_WIN32)
#include <io.h>
#define VB_ISATTY _isatty
#define VB_STDOUT_FD 1
#else
#include <unistd.h>
#define VB_ISATTY isatty
#define VB_STDOUT_FD STDOUT_FILENO
#endif

#include "vb/cli/compat.hpp"
#include "vb/cli/completions.hpp"
#include "vb/cli/installer.hpp"
#include "vb/cli/instance.hpp"
#include "vb/cli/process.hpp"
#include "vb/cli/release_manifest.hpp"
#include "vb/cli/self_update.hpp"
#include "vb/cli/server_config.hpp"
#include "vb/cli/service.hpp"
#include "vb/cli/shim.hpp"
#include "vb/cli/signature.hpp"
#include "vb/cli/source.hpp"
#include "vb/cli/store.hpp"
#include "vb/cli/version.hpp"
#include "vb/cli/watch.hpp"
#include "vb/core/build_info.hpp"
#include "vb/core/config.hpp"
#include "vb/core/version.hpp"

namespace vb::cli {

namespace {

struct Ctx {
	const Layout &layout;
	std::ostream &out;
	std::ostream &err;
};

using Handler = std::function<int(const Ctx &, const std::vector<std::string> &)>;

struct Command {
	const char *usage;
	const char *summary;
	Handler run;
};

int usage_error(const Ctx &c, const std::string &msg) {
	c.err << "vb: " << msg << "\nTry `vb --help`.\n";
	return kExitUsage;
}

int failure(const Ctx &c, const std::string &msg) {
	c.err << "vb: " << msg << "\n";
	return kExitFailure;
}

// Splits `args` at the first "--": {before, after}.
std::pair<std::vector<std::string>, std::vector<std::string>> split_passthrough(
		const std::vector<std::string> &args) {
	const auto it = std::find(args.begin(), args.end(), "--");
	if (it == args.end()) {
		return { args, {} };
	}
	return { { args.begin(), it }, { it + 1, args.end() } };
}

// Removes a bare `--flag` from `args`; true when it was present.
bool take_flag(std::vector<std::string> &args, const std::string &flag) {
	const auto it = std::find(args.begin(), args.end(), flag);
	if (it == args.end()) {
		return false;
	}
	args.erase(it);
	return true;
}

// Extracts `--version <v>` / `--version=<v>` from `args` (removing it).
// Returns false on a dangling `--version`.
bool take_version_flag(std::vector<std::string> &args, std::string &version) {
	for (std::size_t i = 0; i < args.size(); ++i) {
		if (args[i] == "--version") {
			if (i + 1 >= args.size()) {
				return false;
			}
			version = args[i + 1];
			args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
					args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
			return true;
		}
		if (args[i].rfind("--version=", 0) == 0) {
			version = args[i].substr(10);
			args.erase(args.begin() + static_cast<std::ptrdiff_t>(i));
			return true;
		}
	}
	return true;
}

using json = nlohmann::json;

void print_json(const Ctx &c, const json &j) {
	c.out << j.dump(2) << "\n";
}

std::int64_t unix_now() {
	return std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count();
}

int cmd_paths(const Ctx &c, std::vector<std::string> args) {
	const bool as_json = take_flag(args, "--json");
	if (!args.empty()) {
		return usage_error(c, "usage: vb paths [--json]");
	}
	if (as_json) {
		print_json(c, { { "data", c.layout.data().string() }, { "config", c.layout.config().string() }, { "cache", c.layout.cache().string() } });
		return kExitOk;
	}
	c.out << "data:   " << c.layout.data().string() << "\n"
		  << "config: " << c.layout.config().string() << "\n"
		  << "cache:  " << c.layout.cache().string() << "\n";
	return kExitOk;
}

int list_remote(const Ctx &c, bool as_json) {
	std::string why;
	const auto source = make_source(configured_source_spec(c.layout), &why);
	if (!source) {
		return failure(c, why);
	}
	std::vector<std::string> versions;
	if (const Status s = source->list_versions(versions); !s) {
		return failure(c, s.error);
	}
	std::set<std::string> installed;
	for (const Entry &e : list_entries(c.layout)) {
		installed.insert(e.name);
	}
	if (as_json) {
		json arr = json::array();
		for (const std::string &v : versions) {
			arr.push_back({ { "version", v }, { "installed", installed.count(v) != 0 } });
		}
		print_json(c, arr);
		return kExitOk;
	}
	if (versions.empty()) {
		c.out << "no releases published at " << source->describe() << "\n";
		return kExitOk;
	}
	for (std::size_t i = 0; i < versions.size(); ++i) {
		c.out << "  " << versions[i] << (i == 0 ? "  (latest)" : "")
			  << (installed.count(versions[i]) != 0 ? "  installed" : "") << "\n";
	}
	return kExitOk;
}

int cmd_list(const Ctx &c, std::vector<std::string> args) {
	const bool as_json = take_flag(args, "--json");
	const bool remote = take_flag(args, "--remote");
	if (!args.empty()) {
		return usage_error(c, "usage: vb list [--remote] [--json]");
	}
	if (remote) {
		return list_remote(c, as_json);
	}
	const auto entries = list_entries(c.layout);
	if (as_json) {
		json arr = json::array();
		for (const Entry &e : entries) {
			arr.push_back({ { "name", e.name },
					{ "kind", e.kind == EntryKind::Link ? "link" : "release" },
					{ "default", e.is_default },
					{ "path", e.root.string() },
					{ "exists", e.root_exists } });
		}
		print_json(c, arr);
		return kExitOk;
	}
	if (entries.empty()) {
		c.out << "nothing installed. `vb install` downloads the latest release, or register a "
				 "local build with `vb link dev <build-dir>`.\n";
		return kExitOk;
	}
	for (const Entry &e : entries) {
		c.out << (e.is_default ? "* " : "  ") << e.name;
		if (e.kind == EntryKind::Link) {
			c.out << "  -> " << e.root.string() << (e.root_exists ? "" : "  (missing)");
		}
		c.out << "\n";
	}
	return kExitOk;
}

int cmd_use(const Ctx &c, const std::vector<std::string> &args) {
	if (args.size() != 1) {
		return usage_error(c, "usage: vb use <version>");
	}
	std::string why;
	const auto e = resolve_entry(c.layout, args[0], &why);
	if (!e) {
		return failure(c, why);
	}
	if (const Status s = write_default_version(c.layout, e->name); !s) {
		return failure(c, s.error);
	}
	c.out << "default version is now " << e->name << "\n";
	return kExitOk;
}

int cmd_which(const Ctx &c, std::vector<std::string> args) {
	const bool as_json = take_flag(args, "--json");
	std::string version;
	if (!take_version_flag(args, version)) {
		return usage_error(c, "--version needs a value");
	}
	Binary which = Binary::Client;
	if (args.size() > 1) {
		return usage_error(c, "usage: vb which [client|server] [--version <v>] [--json]");
	}
	if (args.size() == 1) {
		if (args[0] == "server") {
			which = Binary::Server;
		} else if (args[0] != "client") {
			return usage_error(c, "usage: vb which [client|server] [--version <v>] [--json]");
		}
	}
	std::string why;
	const auto e = resolve_entry(c.layout, version, &why);
	if (!e) {
		return failure(c, why);
	}
	const auto bin = find_binary(*e, which);
	if (!bin) {
		return failure(c, e->name + " has no " + binary_file_name(which) + " in " +
				e->root.string());
	}
	if (as_json) {
		print_json(c, { { "version", e->name }, { "kind", which == Binary::Server ? "server" : "client" }, { "path", bin->string() } });
		return kExitOk;
	}
	c.out << bin->string() << "\n";
	return kExitOk;
}

// Refuses (returns a message) when a server instance pins or is running from
// `version` -- unless the caller passed --force.
std::string version_in_use_message(const Ctx &c, const std::string &version) {
	std::string wanted = version;
	if (const auto v = parse_version(wanted)) {
		wanted = to_tag(*v);
	}
	const auto users = instance_version_users(c.layout);
	const auto it = users.find(wanted);
	if (it == users.end() || it->second.empty()) {
		return {};
	}
	std::string names;
	for (const std::string &n : it->second) {
		names += (names.empty() ? "" : ", ") + n;
	}
	return "'" + wanted + "' is used by server instance(s): " + names +
			" (stop/repin them, or pass --force to remove it anyway)";
}

int cmd_uninstall(const Ctx &c, std::vector<std::string> args) {
	const bool force = take_flag(args, "--force");
	if (args.size() != 1) {
		return usage_error(c, "usage: vb uninstall <version> [--force]");
	}
	if (!force) {
		if (const std::string msg = version_in_use_message(c, args[0]); !msg.empty()) {
			return failure(c, msg);
		}
	}
	if (const Status s = uninstall(c.layout, args[0]); !s) {
		return failure(c, s.error);
	}
	c.out << "removed " << args[0] << "\n";
	return kExitOk;
}

int cmd_link(const Ctx &c, const std::vector<std::string> &args) {
	if (args.size() != 2) {
		return usage_error(c, "usage: vb link <name> <build-dir>");
	}
	if (const Status s = add_link(c.layout, args[0], args[1]); !s) {
		return failure(c, s.error);
	}
	c.out << "linked " << args[0] << " -> " << args[1] << "\n";
	return kExitOk;
}

int cmd_unlink(const Ctx &c, std::vector<std::string> args) {
	const bool force = take_flag(args, "--force");
	if (args.size() != 1) {
		return usage_error(c, "usage: vb unlink <name> [--force]");
	}
	if (!force) {
		if (const std::string msg = version_in_use_message(c, args[0]); !msg.empty()) {
			return failure(c, msg);
		}
	}
	if (const Status s = remove_link(c.layout, args[0]); !s) {
		return failure(c, s.error);
	}
	c.out << "unlinked " << args[0] << "\n";
	return kExitOk;
}

// Shared flags of install/update. Returns false (and a message) on bad usage.
struct InstallArgs {
	std::string version = "latest";
	InstallOptions opts;
};

bool parse_install_args(const std::vector<std::string> &args, bool allow_version,
		InstallArgs &out, std::string &msg) {
	bool have_version = false;
	for (std::size_t i = 0; i < args.size(); ++i) {
		const std::string &a = args[i];
		if (a == "--force") {
			out.opts.force = true;
		} else if (a == "--keep-download") {
			out.opts.keep_download = true;
		} else if (a == "--no-wait") {
			out.opts.wait_for_lock = false;
		} else if (a == "--build" && i + 1 < args.size()) {
			out.opts.build = args[++i];
		} else if (a.rfind("--build=", 0) == 0) {
			out.opts.build = a.substr(8);
		} else if (!a.empty() && a[0] == '-') {
			msg = "unknown option '" + a + "'";
			return false;
		} else if (allow_version && !have_version) {
			out.version = a;
			have_version = true;
		} else {
			msg = "unexpected argument '" + a + "'";
			return false;
		}
	}
	return true;
}

ProgressFn make_progress(const Ctx &c) {
	if (!VB_ISATTY(VB_STDOUT_FD)) {
		return {};
	}
	return [&c](std::uint64_t done, std::uint64_t total) {
		c.out << "\r  " << (done >> 10) << " KiB";
		if (total > 0) {
			c.out << " / " << (total >> 10) << " KiB (" << (done * 100 / total) << "%)";
		}
		c.out << "   " << std::flush;
	};
}

// Runs the install; prints the outcome. Returns the result for `update`.
InstallResult do_install(const Ctx &c, const std::string &version, InstallOptions opts) {
	const std::string spec = configured_source_spec(c.layout);
	std::string why;
	const auto source = make_source(spec, &why);
	if (!source) {
		InstallResult r;
		r.status = { why };
		return r;
	}
	opts.progress = make_progress(c);
	std::string trust_warning;
	opts.trust = load_trust_policy(c.layout, &trust_warning);
	if (!trust_warning.empty()) {
		c.err << "vb: warning: " << trust_warning << "\n";
	}
	c.out << "installing " << version << " from " << source->describe() << "\n";
	InstallResult r = install_release(c.layout, *source, version, opts);
	if (opts.progress) {
		c.out << "\n";
	}
	return r;
}

int cmd_install(const Ctx &c, const std::vector<std::string> &args) {
	InstallArgs ia;
	std::string msg;
	if (!parse_install_args(args, true, ia, msg)) {
		return usage_error(c, msg);
	}
	const InstallResult r = do_install(c, ia.version, ia.opts);
	if (!r.status) {
		return failure(c, r.status.error);
	}
	if (r.already_installed) {
		c.out << r.version << " is already installed (use --force to reinstall)\n";
		return kExitOk;
	}
	c.out << "installed " << r.version << "\n";
	if (!read_default_version(c.layout)) {
		if (write_default_version(c.layout, r.version)) {
			c.out << "default version is now " << r.version << "\n";
		}
	}
	return kExitOk;
}

int cmd_update(const Ctx &c, const std::vector<std::string> &args) {
	InstallArgs ia;
	std::string msg;
	if (!parse_install_args(args, false, ia, msg)) {
		return usage_error(c, msg);
	}
	std::string previous_newest;
	for (const Entry &e : list_entries(c.layout)) {
		if (e.kind == EntryKind::Release) {
			previous_newest = e.name;
			break;
		}
	}
	const InstallResult r = do_install(c, "latest", ia.opts);
	if (!r.status) {
		return failure(c, r.status.error);
	}
	if (r.already_installed) {
		c.out << "already up to date (" << r.version << ")\n";
		return kExitOk;
	}
	c.out << "installed " << r.version << "\n";
	// Follow "latest" only if the user was following it (or has no default).
	const auto def = read_default_version(c.layout);
	if (!def || *def == previous_newest) {
		if (write_default_version(c.layout, r.version)) {
			c.out << "default version is now " << r.version << "\n";
		}
	}
	return kExitOk;
}

int cmd_prune(const Ctx &c, const std::vector<std::string> &args) {
	int keep = 2;
	for (std::size_t i = 0; i < args.size(); ++i) {
		std::string val;
		if (args[i] == "--keep" && i + 1 < args.size()) {
			val = args[++i];
		} else if (args[i].rfind("--keep=", 0) == 0) {
			val = args[i].substr(7);
		} else {
			return usage_error(c, "usage: vb prune [--keep N]");
		}
		if (val.empty() || val.find_first_not_of("0123456789") != std::string::npos ||
				val.size() > 4) {
			return usage_error(c, "--keep needs a non-negative number");
		}
		keep = std::stoi(val);
	}
	std::set<std::string> in_use;
	const auto users = instance_version_users(c.layout);
	for (const auto &[version, who] : users) {
		if (!who.empty()) {
			in_use.insert(version);
		}
	}
	std::vector<std::string> removed;
	std::vector<std::string> skipped;
	if (const Status s = prune_releases(c.layout, keep, removed, in_use, &skipped); !s) {
		return failure(c, s.error);
	}
	for (const std::string &name : removed) {
		c.out << "removed " << name << "\n";
	}
	for (const std::string &name : skipped) {
		std::string names;
		for (const std::string &n : users.at(name)) {
			names += (names.empty() ? "" : ", ") + n;
		}
		c.out << "kept " << name << " (used by server instance(s): " << names << ")\n";
	}
	if (removed.empty()) {
		c.out << "nothing to prune\n";
	}
	return kExitOk;
}

int cmd_doctor(const Ctx &c, const std::vector<std::string> &args) {
	if (!args.empty()) {
		return usage_error(c, "`doctor` takes no arguments");
	}
	int problems = 0;
	const auto report = [&](bool ok, const std::string &text) {
		c.out << (ok ? "[ok]   " : "[FAIL] ") << text << "\n";
		if (!ok) {
			++problems;
		}
	};
	const std::string platform = current_platform();
	report(!platform.empty(), "platform: " + (platform.empty() ? std::string("unsupported") : platform));
	c.out << "[info] data: " << c.layout.data().string() << "\n"
		  << "[info] config: " << c.layout.config().string() << "\n"
		  << "[info] source: " << configured_source_spec(c.layout) << "\n";
	{
		std::string warning;
		const TrustPolicy policy = load_trust_policy(c.layout, &warning);
		if (policy.keys.empty()) {
			c.out << "[info] release signatures: not enforced (no trusted key; see release_keys.txt "
					 "and `trusted_keys` in cli.toml)\n";
		} else {
			c.out << "[info] release signatures: " << (policy.require ? "required" : "checked when present")
				  << ", " << policy.keys.size() << " trusted key(s)\n";
		}
		if (!warning.empty()) {
			c.out << "[warn] " << warning << "\n";
		}
	}
	std::string why;
	report(make_source(configured_source_spec(c.layout), &why) != nullptr,
			why.empty() ? "source spec is valid" : why);

	std::error_code ec;
	std::filesystem::create_directories(c.layout.data(), ec);
	const auto probe = c.layout.data() / ".doctor-probe";
	bool writable = false;
	if (!ec) {
		std::ofstream f(probe);
		writable = static_cast<bool>(f << "x");
		f.close();
		std::filesystem::remove(probe, ec);
	}
	report(writable, "data directory is writable");

	const auto entries = list_entries(c.layout);
	for (const Entry &e : entries) {
		if (!e.root_exists) {
			report(false, e.name + ": linked directory is missing (" + e.root.string() + ")");
			continue;
		}
		const bool client = find_binary(e, Binary::Client).has_value();
		const bool server = find_binary(e, Binary::Server).has_value();
		report(client || server, e.name + ": " + (client ? "client " : "") + (server ? "server" : "") +
				(client || server ? "" : "no binaries found"));
	}
	if (const auto def = read_default_version(c.layout)) {
		std::string w;
		report(resolve_entry(c.layout, *def, &w).has_value(),
				w.empty() ? "default version " + *def : "default version: " + w);
	} else {
		c.out << "[info] no default version set\n";
	}
	int stale = 0;
	for (std::filesystem::directory_iterator it(c.layout.versions_dir(), ec), end; !ec && it != end;
			it.increment(ec)) {
		stale += it->path().filename().string().rfind(".staging-", 0) == 0 ? 1 : 0;
	}
	if (stale > 0) {
		c.out << "[warn] " << stale << " leftover staging dir(s); `vb prune` removes them\n";
	}
	return problems == 0 ? kExitOk : kExitFailure;
}

int cmd_launch(const Ctx &c, const std::vector<std::string> &raw) {
	auto [opts, passthrough] = split_passthrough(raw);
	std::string version;
	if (!take_version_flag(opts, version)) {
		return usage_error(c, "--version needs a value");
	}
	std::optional<ConnectTarget> connect;
	for (std::size_t i = 0; i < opts.size(); ++i) {
		std::string value;
		if (opts[i] == "--connect" && i + 1 < opts.size()) {
			value = opts[i + 1];
			opts.erase(opts.begin() + static_cast<std::ptrdiff_t>(i),
					opts.begin() + static_cast<std::ptrdiff_t>(i) + 2);
		} else if (opts[i].rfind("--connect=", 0) == 0) {
			value = opts[i].substr(10);
			opts.erase(opts.begin() + static_cast<std::ptrdiff_t>(i));
		} else {
			continue;
		}
		ConnectTarget target;
		if (const Status st = parse_connect_target(value, target); !st) {
			return usage_error(c, st.error);
		}
		connect = target;
		break;
	}
	if (std::find(opts.begin(), opts.end(), "--connect") != opts.end()) {
		return usage_error(c, "--connect needs host[:port]");
	}
	if (!opts.empty()) {
		return usage_error(c, "unexpected argument '" + opts[0] +
				"' (pass client arguments after `--`)");
	}
	std::string why;
	const auto e = resolve_entry(c.layout, version, &why);
	if (!e) {
		return failure(c, why);
	}
	if (connect) {
		// Only a warning: the launch goes ahead (the client's own handshake is
		// the authority), but a mismatch with a server we manage is worth a line.
		if (const std::string warning = protocol_warning(c.layout, *e, *connect); !warning.empty()) {
			c.err << "vb: warning: " << warning << "\n";
		}
		const auto has = [&](const char *name) {
			return std::any_of(passthrough.begin(), passthrough.end(), [&](const std::string &a) {
				return a == name || a.rfind(std::string(name) + "=", 0) == 0;
			});
		};
		if (has("--server") || has("--port")) {
			return usage_error(c, "--connect cannot be combined with --server / --port");
		}
		passthrough.insert(passthrough.begin(),
				{ "--server", connect->host, "--port", std::to_string(connect->port) });
	}
	const auto bin = find_binary(*e, Binary::Client);
	if (!bin) {
		return failure(c, e->name + " has no " + binary_file_name(Binary::Client) + " in " +
				e->root.string());
	}
	// Settings live in the user config dir so every installed version shares
	// them; an explicit --config from the user wins.
	const bool has_config = std::any_of(passthrough.begin(), passthrough.end(),
			[](const std::string &a) { return a == "--config" || a.rfind("--config=", 0) == 0; });
	const auto has_opt = [&](const std::string &name) {
		return std::any_of(passthrough.begin(), passthrough.end(), [&](const std::string &a) {
			return a == name || a.rfind(name + "=", 0) == 0;
		});
	};
	std::vector<std::string> args;
	if (!has_config) {
		args = { "--config", c.layout.client_toml().string() };
	}
	// Singleplayer worlds live in the user's data dir, not in (or next to) the
	// version they were created with, so they survive uninstall/update.
	if (!has_opt("--world-dir")) {
		args.push_back("--world-dir");
		args.push_back(c.layout.singleplayer_world_dir().string());
	}
	// The bundled content/base of *this* version (for a link: the source tree
	// beside the build dir), whatever the working directory is.
	if (!has_opt("--content-pack")) {
		if (const auto pack = resolve_pack("builtin:base", e->root)) {
			args.push_back("--content-pack");
			args.push_back(pack->string());
		}
	}
	args.insert(args.end(), passthrough.begin(), passthrough.end());
	const RunResult r = run_foreground(*bin, args);
	if (!r.error.empty()) {
		return failure(c, r.error);
	}
	return r.exit_code;
}

// ---- hosting -----------------------------------------------------------

struct Opts {
	std::map<std::string, std::string> values;
	std::set<std::string> flags;
	std::vector<std::string> positional;
	std::string error;
};

// Minimal option parser: `--name value`, `--name=value`, bare flags, positionals.
Opts parse_opts(const std::vector<std::string> &args, const std::set<std::string> &value_opts,
		const std::set<std::string> &flag_opts) {
	Opts o;
	for (std::size_t i = 0; i < args.size(); ++i) {
		const std::string &a = args[i];
		const std::size_t eq = a.find('=');
		const std::string key = (a.rfind("--", 0) == 0 && eq != std::string::npos) ? a.substr(0, eq) : a;
		if (value_opts.count(key) != 0) {
			if (key.size() != a.size()) {
				o.values[key] = a.substr(eq + 1);
			} else if (i + 1 < args.size()) {
				o.values[key] = args[++i];
			} else {
				o.error = key + " needs a value";
				return o;
			}
		} else if (flag_opts.count(a) != 0) {
			o.flags.insert(a);
		} else if (!a.empty() && a[0] == '-') {
			o.error = "unknown option '" + a + "'";
			return o;
		} else {
			o.positional.push_back(a);
		}
	}
	return o;
}

bool parse_port(const std::string &text, std::uint16_t &out) {
	if (text.empty() || text.size() > 5 || text.find_first_not_of("0123456789") != std::string::npos) {
		return false;
	}
	const int v = std::stoi(text);
	if (v < 1 || v > 65535) {
		return false;
	}
	out = static_cast<std::uint16_t>(v);
	return true;
}

// Pack argument as typed: `builtin:<name>` stays, anything else is a directory
// made absolute against the current working directory.
std::string normalise_pack(const std::string &pack) {
	if (pack.empty() || pack.rfind("builtin:", 0) == 0) {
		return pack;
	}
	std::error_code ec;
	return std::filesystem::absolute(pack, ec).lexically_normal().string();
}

// `vb host --watch`: keeps the server running and restarts it when the pack
// changes on disk. The server runs in a worker thread (foreground, so its
// output stays on this terminal) while this thread polls the pack.
int host_watch(const Ctx &c, const Instance &inst, const Overrides &ov) {
	constexpr auto kPoll = std::chrono::milliseconds(400);
	constexpr auto kQuiet = std::chrono::milliseconds(700); // wait out a burst of saves
	LaunchPlan plan;
	if (const Status s = plan_launch(c.layout, inst, ov, plan); !s) {
		return failure(c, s.error);
	}
	c.out << "watching " << plan.pack.string() << " for changes\n"
		  << std::flush;
	PackSnapshot snapshot = snapshot_pack(plan.pack);
	for (;;) {
		StartResult result;
		std::atomic<bool> finished{ false };
		std::thread server([&] {
			result = start_instance(c.layout, inst, ov, true, c.out);
			finished = true;
		});
		bool restart = false;
		bool waiting_for_edit = false; // the server stopped by itself with an error
		while (!restart) {
			std::this_thread::sleep_for(kPoll);
			if (finished && !waiting_for_edit) {
				// 130/143: stopped by SIGINT/SIGTERM (a user's Ctrl+C, `vb server stop`,
				// or one that landed before the server's handlers were up) -- a
				// request to stop, not a crash worth waiting for an edit over.
				const bool stopped_on_request = result.exit_code == 0 || result.exit_code == 130 ||
						result.exit_code == 143;
				if (!result.status || stopped_on_request) {
					break; // a start failure, Ctrl+C or a normal exit: we are done
				}
				c.out << "[vb] server exited with code " << result.exit_code
					  << "; waiting for the pack to change...\n"
					  << std::flush;
				waiting_for_edit = true;
			}
			PackSnapshot now = snapshot_pack(plan.pack);
			if (now == snapshot) {
				continue;
			}
			for (;;) { // debounce: act once the files have stopped changing
				std::this_thread::sleep_for(kQuiet);
				PackSnapshot again = snapshot_pack(plan.pack);
				const bool stable = again == now;
				now = std::move(again);
				if (stable) {
					break;
				}
			}
			c.out << "[vb] pack changed (" << describe_changes(snapshot, now) << ") -- restarting\n"
				  << std::flush;
			snapshot = std::move(now);
			if (!finished) {
				// The record appears a moment after the thread starts the server.
				for (int i = 0; i < 100 && !finished && !running_record(inst); ++i) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
				std::ostringstream quiet;
				stop_instance(inst, 30, true, quiet);
			}
			restart = true;
		}
		server.join();
		if (!restart) {
			if (!result.status) {
				return failure(c, result.status.error);
			}
			return result.exit_code == 130 || result.exit_code == 143 ? 0 : result.exit_code;
		}
	}
}

int cmd_host(const Ctx &c, std::vector<std::string> raw) {
	auto [rest, extra] = split_passthrough(raw);
	const bool watch = take_flag(rest, "--watch");
	const Opts o = parse_opts(rest, { "--version", "--port", "--pack" }, {});
	if (!o.error.empty() || !o.positional.empty()) {
		return usage_error(c, o.error.empty() ? "unexpected argument '" + o.positional[0] + "' (pass server arguments after `--`)" : o.error);
	}
	Overrides ov;
	if (const auto it = o.values.find("--version"); it != o.values.end()) {
		ov.version = it->second;
	}
	if (const auto it = o.values.find("--pack"); it != o.values.end()) {
		ov.pack = normalise_pack(it->second);
	}
	if (const auto it = o.values.find("--port"); it != o.values.end()) {
		std::uint16_t port = 0;
		if (!parse_port(it->second, port)) {
			return usage_error(c, "--port needs a number from 1 to 65535");
		}
		ov.port = port;
	}
	ov.extra_args = extra;

	// The "default" instance is created on first use; per-run flags above are
	// deliberately not saved into it.
	std::string why;
	auto inst = load_instance(c.layout, kDefaultInstanceName, &why);
	if (!inst) {
		if (const Status s = create_instance(c.layout, kDefaultInstanceName, "default",
					"builtin:base", std::nullopt);
				!s) {
			return failure(c, s.error);
		}
		inst = load_instance(c.layout, kDefaultInstanceName, &why);
		if (!inst) {
			return failure(c, why);
		}
	}
	if (watch) {
		return host_watch(c, *inst, ov);
	}
	const StartResult r = start_instance(c.layout, *inst, ov, true, c.out);
	if (!r.status) {
		return failure(c, r.status.error);
	}
	return r.exit_code;
}

std::string format_uptime(std::int64_t seconds) {
	if (seconds < 0) {
		seconds = 0;
	}
	std::ostringstream os;
	if (seconds >= 86400) {
		os << seconds / 86400 << "d " << (seconds % 86400) / 3600 << "h";
	} else if (seconds >= 3600) {
		os << seconds / 3600 << "h " << std::setw(2) << std::setfill('0') << (seconds % 3600) / 60 << "m";
	} else if (seconds >= 60) {
		os << seconds / 60 << "m " << std::setw(2) << std::setfill('0') << seconds % 60 << "s";
	} else {
		os << seconds << "s";
	}
	return os.str();
}

std::string configured_port(const Instance &inst) {
	auto cfg = vb::core::load_server_config(instance_server_toml(inst).string());
	return cfg ? std::to_string(cfg->port) : "?";
}

// One instance as the user sees it: config + run record + (fresh) server status.
struct InstanceView {
	Instance inst;
	std::optional<RunRecord> rec;
	std::optional<ServerStatus> status; // only when the server wrote it recently
	std::string port;
};

InstanceView view_of(const Instance &inst, std::int64_t now) {
	InstanceView v{ inst, running_record(inst), std::nullopt, configured_port(inst) };
	if (v.rec) {
		if (auto st = read_server_status(inst); st && status_is_fresh(*st, now)) {
			v.status = std::move(st);
		}
	}
	return v;
}

json to_json(const InstanceView &v, std::int64_t now) {
	json j = { { "name", v.inst.name },
		{ "version", v.inst.version },
		{ "pack", v.inst.pack },
		{ "port", v.port == "?" ? json(nullptr) : json(std::stoi(v.port)) },
		{ "dir", v.inst.dir.string() },
		{ "log", instance_log_file(v.inst).string() },
		{ "running", v.rec.has_value() } };
	if (v.rec) {
		j["pid"] = v.rec->pid;
		j["running_version"] = v.rec->version;
		j["uptime_seconds"] = now - v.rec->started_unix;
	}
	if (v.status) {
		j["players"] = v.status->players;
		j["max_players"] = v.status->max_players;
		j["tick_rate"] = v.status->tick_rate;
		j["target_tick_rate"] = v.status->target_tick_rate;
		j["seed"] = v.status->seed;
		j["motd"] = v.status->motd;
	}
	return j;
}

void print_instance_table(const Ctx &c, const std::vector<Instance> &instances) {
	const std::int64_t now = unix_now();
	std::vector<InstanceView> views;
	std::size_t name_w = 4;
	std::size_t ver_w = 7;
	for (const Instance &i : instances) {
		views.push_back(view_of(i, now));
		name_w = std::max(name_w, i.name.size());
		ver_w = std::max(ver_w, i.version.size());
	}
	c.out << std::left << std::setw(static_cast<int>(name_w) + 2) << "NAME"
		  << std::setw(static_cast<int>(ver_w) + 2) << "VERSION" << std::setw(8) << "PORT"
		  << std::setw(9) << "PLAYERS"
		  << "STATE\n";
	for (const InstanceView &v : views) {
		const std::string players = v.status
				? std::to_string(v.status->players.size()) + "/" + std::to_string(v.status->max_players)
				: "-";
		c.out << std::left << std::setw(static_cast<int>(name_w) + 2) << v.inst.name
			  << std::setw(static_cast<int>(ver_w) + 2) << v.inst.version << std::setw(8) << v.port
			  << std::setw(9) << players;
		if (v.rec) {
			c.out << "running (pid " << v.rec->pid << ", up "
				  << format_uptime(now - v.rec->started_unix) << ")\n";
		} else {
			c.out << "stopped\n";
		}
	}
}

int server_list(const Ctx &c, std::vector<std::string> args) {
	const bool as_json = take_flag(args, "--json");
	if (!args.empty()) {
		return usage_error(c, "usage: vb server list [--json]");
	}
	const auto instances = list_instances(c.layout);
	if (as_json) {
		const std::int64_t now = unix_now();
		json arr = json::array();
		for (const Instance &i : instances) {
			arr.push_back(to_json(view_of(i, now), now));
		}
		print_json(c, arr);
		return kExitOk;
	}
	if (instances.empty()) {
		c.out << "no servers yet. `vb host` runs one right away; "
				 "`vb server new <name>` creates a persistent one.\n";
		return kExitOk;
	}
	print_instance_table(c, instances);
	return kExitOk;
}

int server_new(const Ctx &c, const std::vector<std::string> &args) {
	const Opts o = parse_opts(args, { "--version", "--pack", "--port" }, {});
	if (!o.error.empty() || o.positional.size() != 1) {
		return usage_error(c, o.error.empty() ? "usage: vb server new <name> [--version <v>] [--pack <dir>|builtin:<name>] [--port <n>]" : o.error);
	}
	std::optional<std::uint16_t> port;
	if (const auto it = o.values.find("--port"); it != o.values.end()) {
		std::uint16_t p = 0;
		if (!parse_port(it->second, p)) {
			return usage_error(c, "--port needs a number from 1 to 65535");
		}
		port = p;
	}
	const auto get = [&](const char *k, const char *fallback) {
		const auto it = o.values.find(k);
		return it == o.values.end() ? std::string(fallback) : it->second;
	};
	const std::string pack = normalise_pack(get("--pack", "builtin:base"));
	if (const Status s = create_instance(c.layout, o.positional[0], get("--version", "default"),
				pack, port);
			!s) {
		return failure(c, s.error);
	}
	const auto inst = load_instance(c.layout, o.positional[0]);
	c.out << "created server '" << o.positional[0] << "' in " << inst->dir.string()
		  << "\nstart it with `vb server start " << o.positional[0] << "`\n";
	return kExitOk;
}

// Loads the instance named by the single positional argument.
std::optional<Instance> instance_arg(const Ctx &c, const Opts &o, const char *usage, int &code) {
	if (!o.error.empty() || o.positional.size() != 1) {
		code = usage_error(c, o.error.empty() ? usage : o.error);
		return std::nullopt;
	}
	std::string why;
	auto inst = load_instance(c.layout, o.positional[0], &why);
	if (!inst) {
		code = failure(c, why);
	}
	return inst;
}

int server_start(const Ctx &c, const std::vector<std::string> &args) {
	const Opts o = parse_opts(args, {}, { "--foreground" });
	int code = kExitOk;
	const auto inst = instance_arg(c, o, "usage: vb server start <name> [--foreground]", code);
	if (!inst) {
		return code;
	}
	const bool fg = o.flags.count("--foreground") != 0;
	const StartResult r = start_instance(c.layout, *inst, {}, fg, c.out);
	if (!r.status) {
		return failure(c, r.status.error);
	}
	return r.exit_code;
}

bool parse_seconds(const std::string &text, int &out) {
	if (text.empty() || text.size() > 5 || text.find_first_not_of("0123456789") != std::string::npos) {
		return false;
	}
	out = std::stoi(text);
	return true;
}

int server_stop(const Ctx &c, const std::vector<std::string> &args) {
	const Opts o = parse_opts(args, { "--timeout" }, { "--kill" });
	int timeout = 30;
	if (const auto it = o.values.find("--timeout"); it != o.values.end() &&
			!parse_seconds(it->second, timeout)) {
		return usage_error(c, "--timeout needs a number of seconds");
	}
	int code = kExitOk;
	const auto inst = instance_arg(c, o, "usage: vb server stop <name> [--timeout <seconds>] [--kill]", code);
	if (!inst) {
		return code;
	}
	if (const Status s = stop_instance(*inst, timeout, o.flags.count("--kill") != 0, c.out); !s) {
		return failure(c, s.error);
	}
	return kExitOk;
}

int server_restart(const Ctx &c, const std::vector<std::string> &args) {
	const Opts o = parse_opts(args, { "--timeout" }, { "--kill" });
	int timeout = 30;
	if (const auto it = o.values.find("--timeout"); it != o.values.end() &&
			!parse_seconds(it->second, timeout)) {
		return usage_error(c, "--timeout needs a number of seconds");
	}
	int code = kExitOk;
	const auto inst = instance_arg(c, o, "usage: vb server restart <name> [--timeout <seconds>] [--kill]", code);
	if (!inst) {
		return code;
	}
	if (const Status s = stop_instance(*inst, timeout, o.flags.count("--kill") != 0, c.out); !s) {
		return failure(c, s.error);
	}
	const StartResult r = start_instance(c.layout, *inst, {}, false, c.out);
	return r.status ? kExitOk : failure(c, r.status.error);
}

int server_status(const Ctx &c, std::vector<std::string> args) {
	const bool as_json = take_flag(args, "--json");
	const Opts o = parse_opts(args, {}, {});
	if (!o.error.empty() || o.positional.size() > 1) {
		return usage_error(c, o.error.empty() ? "usage: vb server status [<name>] [--json]" : o.error);
	}
	if (o.positional.empty()) {
		return server_list(c, as_json ? std::vector<std::string>{ "--json" } : std::vector<std::string>{});
	}
	std::string why;
	const auto inst = load_instance(c.layout, o.positional[0], &why);
	if (!inst) {
		return failure(c, why);
	}
	const std::int64_t now = unix_now();
	const InstanceView v = view_of(*inst, now);
	if (as_json) {
		print_json(c, to_json(v, now));
		return kExitOk;
	}
	c.out << "name:    " << inst->name << "\n"
		  << "state:   ";
	if (v.rec) {
		c.out << "running (pid " << v.rec->pid << ", up " << format_uptime(now - v.rec->started_unix)
			  << ")\n";
	} else {
		c.out << "stopped\n";
	}
	c.out << "version: " << inst->version << (v.rec ? " (running " + v.rec->version + ")" : "") << "\n"
		  << "pack:    " << inst->pack << "\n"
		  << "port:    " << v.port << "\n";
	if (v.status) {
		std::ostringstream tps;
		if (v.status->tick_rate > 0.0) {
			tps << std::fixed << std::setprecision(1) << v.status->tick_rate << " / "
				<< v.status->target_tick_rate << " Hz";
		} else {
			tps << "measuring... / " << v.status->target_tick_rate << " Hz"; // first interval
		}
		c.out << "ticks:   " << tps.str() << "\n"
			  << "players: " << v.status->players.size() << "/" << v.status->max_players;
		for (std::size_t i = 0; i < v.status->players.size(); ++i) {
			c.out << (i == 0 ? "  (" : ", ") << v.status->players[i];
		}
		c.out << (v.status->players.empty() ? "" : ")") << "\n"
			  << "seed:    " << v.status->seed << "\n";
	} else if (v.rec) {
		c.out << "players: (no recent status from the server)\n";
	}
	c.out << "dir:     " << inst->dir.string() << "\n"
		  << "log:     " << instance_log_file(*inst).string() << "\n";
	if (const std::string tail = tail_log(*inst, 5); !tail.empty()) {
		c.out << "recent log:\n"
			  << tail;
	}
	return kExitOk;
}

int server_logs(const Ctx &c, const std::vector<std::string> &args) {
	const Opts o = parse_opts(args, { "-n", "--lines" }, { "-f", "--follow" });
	int code = kExitOk;
	const auto inst = instance_arg(c, o, "usage: vb server logs <name> [-f] [-n <lines>]", code);
	if (!inst) {
		return code;
	}
	int lines = 50;
	for (const char *key : { "-n", "--lines" }) {
		if (const auto it = o.values.find(key); it != o.values.end() &&
				!parse_seconds(it->second, lines)) {
			return usage_error(c, "-n needs a number of lines");
		}
	}
	c.out << tail_log(*inst, lines) << std::flush;
	if (o.flags.count("-f") == 0 && o.flags.count("--follow") == 0) {
		return kExitOk;
	}
	// Follow until interrupted (Ctrl+C). Re-opened each round so a log that is
	// rotated or truncated underneath us is picked up from its start.
	std::error_code ec;
	std::uintmax_t pos = std::filesystem::exists(instance_log_file(*inst), ec)
			? std::filesystem::file_size(instance_log_file(*inst), ec)
			: 0;
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		const std::uintmax_t size = std::filesystem::exists(instance_log_file(*inst), ec)
				? std::filesystem::file_size(instance_log_file(*inst), ec)
				: 0;
		if (size < pos) {
			pos = 0;
		}
		if (size > pos) {
			std::ifstream f(instance_log_file(*inst), std::ios::binary);
			f.seekg(static_cast<std::streamoff>(pos));
			c.out << f.rdbuf() << std::flush;
			pos = size;
		}
	}
}

// PATH lookup for the editor (run_foreground does no PATH search of its own).
std::filesystem::path find_executable(const std::string &name) {
	namespace fs = std::filesystem;
	if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
		return name;
	}
	const char *path_env = std::getenv("PATH");
	if (path_env == nullptr) {
		return {};
	}
#if defined(_WIN32)
	const char sep = ';';
	const std::vector<std::string> suffixes = { ".exe", ".com" };
#else
	const char sep = ':';
	const std::vector<std::string> suffixes = { "" };
#endif
	std::string rest = path_env;
	std::size_t pos = 0;
	while (pos <= rest.size()) {
		std::size_t end = rest.find(sep, pos);
		if (end == std::string::npos) {
			end = rest.size();
		}
		const std::string dir = rest.substr(pos, end - pos);
		for (const std::string &suffix : suffixes) {
			std::error_code ec;
			const fs::path candidate = fs::path(dir.empty() ? "." : dir) / (name + suffix);
			if (fs::is_regular_file(candidate, ec)) {
				return candidate;
			}
		}
		pos = end + 1;
	}
	return {};
}

// $VISUAL / $EDITOR (which may carry arguments: "code --wait"), else a default.
std::vector<std::string> editor_command() {
	for (const char *var : { "VISUAL", "EDITOR" }) {
		if (const char *v = std::getenv(var); v != nullptr && *v != '\0') {
			std::vector<std::string> parts;
			std::istringstream in(v);
			for (std::string w; in >> w;) {
				parts.push_back(w);
			}
			if (!parts.empty()) {
				return parts;
			}
		}
	}
#if defined(_WIN32)
	return { "notepad" };
#else
	return { "vi" };
#endif
}

int server_config(const Ctx &c, const std::vector<std::string> &args) {
	constexpr const char *kUsage =
			"usage: vb server config <name> [get [<key>] | set <key> <value> | unset <key> | edit]";
	if (args.empty()) {
		return usage_error(c, kUsage);
	}
	std::string why;
	const auto inst = load_instance(c.layout, args[0], &why);
	if (!inst) {
		return failure(c, why);
	}
	const std::filesystem::path file = instance_server_toml(*inst);
	const std::string sub = args.size() > 1 ? args[1] : "get";
	const auto note_if_running = [&] {
		if (running_record(*inst)) {
			c.out << "'" << inst->name << "' is running; restart it to apply (`vb server restart "
				  << inst->name << "`)\n";
		}
	};
	if (sub == "get") {
		if (args.size() > 3) {
			return usage_error(c, kUsage);
		}
		if (args.size() == 3) {
			const auto v = get_server_config_value(file, args[2], &why);
			if (!v) {
				return failure(c, why);
			}
			c.out << *v << "\n";
			return kExitOk;
		}
		const auto all = dump_server_config(file, &why);
		if (!all) {
			return failure(c, why);
		}
		c.out << *all;
		return kExitOk;
	}
	if (sub == "set" && args.size() == 4) {
		if (const Status s = set_server_config_value(file, args[2], args[3]); !s) {
			return failure(c, s.error);
		}
		c.out << args[2] << " = " << args[3] << "\n";
		note_if_running();
		return kExitOk;
	}
	if (sub == "unset" && args.size() == 3) {
		if (const Status s = unset_server_config_key(file, args[2]); !s) {
			return failure(c, s.error);
		}
		c.out << args[2] << " reset to the default\n";
		note_if_running();
		return kExitOk;
	}
	if (sub == "edit" && args.size() == 2) {
		std::vector<std::string> cmd = editor_command();
		const std::filesystem::path exe = find_executable(cmd.front());
		if (exe.empty()) {
			return failure(c, "editor '" + cmd.front() + "' not found; set $EDITOR");
		}
		std::error_code ec;
		std::string before;
		{
			std::ifstream in(file, std::ios::binary);
			std::ostringstream ss;
			ss << in.rdbuf();
			before = ss.str();
		}
		cmd.erase(cmd.begin());
		cmd.push_back(file.string());
		const RunResult r = run_foreground(exe, cmd);
		if (!r.error.empty()) {
			return failure(c, r.error);
		}
		// An edit the engine would reject must not be left in place for the
		// next start to trip over: restore the old file, keep the bad one aside.
		if (auto loaded = vb::core::load_server_config(file.string()); !loaded) {
			std::filesystem::path rejected = file;
			rejected += ".rejected";
			std::filesystem::copy_file(file, rejected,
					std::filesystem::copy_options::overwrite_existing, ec);
			std::ofstream(file, std::ios::binary | std::ios::trunc) << before;
			return failure(c, "your edit is not valid (" + std::string(vb::core::message(loaded.error())) + "); restored the previous " + "server.toml and kept yours as " + rejected.string());
		}
		note_if_running();
		return kExitOk;
	}
	return usage_error(c, kUsage);
}

int server_service(const Ctx &c, const std::vector<std::string> &args) {
	constexpr const char *kUsage =
			"usage: vb server service print <name> [--platform systemd|launchd|task]";
	if (args.empty() || args[0] != "print") {
		return usage_error(c, kUsage);
	}
	const Opts o = parse_opts(std::vector<std::string>(args.begin() + 1, args.end()), { "--platform" }, {});
	if (!o.error.empty() || o.positional.size() != 1) {
		return usage_error(c, o.error.empty() ? kUsage : o.error);
	}
	ServiceKind kind = host_service_kind();
	if (const auto it = o.values.find("--platform"); it != o.values.end() &&
			!parse_service_kind(it->second, kind)) {
		return usage_error(c, "--platform must be systemd, launchd or task");
	}
	std::string why;
	const auto inst = load_instance(c.layout, o.positional[0], &why);
	if (!inst) {
		return failure(c, why);
	}
	ServiceSpec spec;
	spec.name = inst->name;
	spec.vb_exe = current_executable_path();
	if (spec.vb_exe.empty() || !spec.vb_exe.is_absolute()) {
		return failure(c, "cannot determine vb's own path for the service definition");
	}
	spec.instance_dir = inst->dir;
	spec.log_file = instance_log_file(*inst);
	if (const char *home = std::getenv("VB_HOME"); home != nullptr && *home != '\0') {
		spec.vb_home = home; // the service must see the same data directory vb does
	}
	c.out << render_service(kind, spec);
	c.err << service_install_hint(kind, spec);
	return kExitOk;
}

int server_rm(const Ctx &c, const std::vector<std::string> &args) {
	const Opts o = parse_opts(args, {}, { "--keep-world", "--yes" });
	int code = kExitOk;
	const auto inst = instance_arg(c, o, "usage: vb server rm <name> [--keep-world] [--yes]", code);
	if (!inst) {
		return code;
	}
	const bool keep_world = o.flags.count("--keep-world") != 0;
	std::error_code ec;
	if (!keep_world && !o.flags.count("--yes") &&
			std::filesystem::exists(instance_world_dir(*inst), ec)) {
		return failure(c, "'" + inst->name + "' has a saved world (" + instance_world_dir(*inst).string() + "); pass --yes to delete it too, or --keep-world to keep it");
	}
	if (const Status s = remove_instance(c.layout, inst->name, keep_world); !s) {
		return failure(c, s.error);
	}
	c.out << "removed server '" << inst->name << "'" << (keep_world ? " (world kept)" : "") << "\n";
	return kExitOk;
}

const std::vector<std::string> &server_subcommand_names() {
	static const std::vector<std::string> names = { "new", "list", "start", "stop", "restart",
		"status", "logs", "config", "service", "rm" };
	return names;
}

int cmd_server(const Ctx &c, const std::vector<std::string> &args) {
	static const std::map<std::string, std::function<int(const Ctx &, const std::vector<std::string> &)>>
			sub = {
				{ "config", server_config },
				{ "service", server_service },
				{ "new", server_new },
				{ "list", [](const Ctx &ctx, const std::vector<std::string> &a) { return server_list(ctx, a); } },
				{ "start", server_start },
				{ "stop", server_stop },
				{ "restart", server_restart },
				{ "status", [](const Ctx &ctx, const std::vector<std::string> &a) { return server_status(ctx, a); } },
				{ "logs", server_logs },
				{ "rm", server_rm },
			};
	if (args.empty()) {
		return usage_error(c, "usage: vb server <new|list|start|stop|restart|status|logs|config|service|rm> ...");
	}
	const auto it = sub.find(args[0]);
	if (it == sub.end()) {
		return usage_error(c, "unknown server command '" + args[0] + "'");
	}
	return it->second(c, std::vector<std::string>(args.begin() + 1, args.end()));
}

const std::map<std::string, Command> &commands();

int cmd_self(const Ctx &c, std::vector<std::string> args) {
	if (args.empty() || args[0] != "update") {
		return usage_error(c, "usage: vb self update [--check] [--force]");
	}
	args.erase(args.begin());
	SelfUpdateOptions opts;
	opts.check_only = take_flag(args, "--check");
	opts.force = take_flag(args, "--force");
	if (!args.empty()) {
		return usage_error(c, "usage: vb self update [--check] [--force]");
	}
	opts.current_version = vb::kVersionString;
	opts.progress = make_progress(c);
	std::string trust_warning;
	opts.trust = load_trust_policy(c.layout, &trust_warning);
	if (!trust_warning.empty()) {
		c.err << "vb: warning: " << trust_warning << "\n";
	}
	std::string why;
	const auto source = make_source(configured_source_spec(c.layout), &why);
	if (!source) {
		return failure(c, why);
	}
	const SelfUpdateResult r = self_update(c.layout, *source, opts);
	if (opts.progress) {
		c.out << "\n";
	}
	if (!r.status) {
		return failure(c, r.status.error);
	}
	if (r.updated) {
		c.out << "vb updated: " << opts.current_version << " -> " << r.latest << "\n";
	} else if (r.up_to_date) {
		c.out << "vb is up to date (" << opts.current_version << ")\n";
	} else {
		c.out << "update available: " << opts.current_version << " -> " << r.latest
			  << " (run `vb self update`)\n";
	}
	return kExitOk;
}

bool dir_on_path(const std::filesystem::path &dir) {
	const char *path_env = std::getenv("PATH");
	if (path_env == nullptr) {
		return false;
	}
#if defined(_WIN32)
	const char sep = ';';
#else
	const char sep = ':';
#endif
	std::string rest = path_env;
	std::size_t pos = 0;
	while (pos <= rest.size()) {
		std::size_t end = rest.find(sep, pos);
		if (end == std::string::npos) {
			end = rest.size();
		}
		if (std::filesystem::path(rest.substr(pos, end - pos)).lexically_normal() == dir.lexically_normal()) {
			return true;
		}
		pos = end + 1;
	}
	return false;
}

int cmd_shim(const Ctx &c, const std::vector<std::string> &args) {
	if (args.size() != 1 || (args[0] != "install" && args[0] != "remove")) {
		return usage_error(c, "usage: vb shim <install|remove>");
	}
	std::vector<std::filesystem::path> files;
	if (args[0] == "remove") {
		if (const Status s = remove_shims(c.layout, files); !s) {
			return failure(c, s.error);
		}
		for (const auto &f : files) {
			c.out << "removed " << f.string() << "\n";
		}
		if (files.empty()) {
			c.out << "no shims installed\n";
		}
		return kExitOk;
	}
	const std::filesystem::path self = current_executable_path();
	if (const Status s = install_shims(c.layout, self, files); !s) {
		return failure(c, s.error);
	}
	for (const auto &f : files) {
		c.out << "wrote " << f.string() << "\n";
	}
	if (!dir_on_path(shim_dir(c.layout))) {
		c.out << "add " << shim_dir(c.layout).string() << " to your PATH to use them from any shell\n";
	}
	return kExitOk;
}

int cmd_completions(const Ctx &c, const std::vector<std::string> &args) {
	if (args.size() != 1) {
		return usage_error(c, "usage: vb completions <bash|zsh|fish|powershell>");
	}
	CompletionSpec spec;
	for (const auto &[name, cmd] : commands()) {
		spec.commands.emplace_back(name, cmd.summary);
	}
	spec.server_subcommands = server_subcommand_names();
	spec.config_subcommands = { "get", "set", "unset", "edit" };
	const std::string text = generate_completions(args[0], spec);
	if (text.empty()) {
		return usage_error(c, "unknown shell '" + args[0] + "' (bash, zsh, fish or powershell)");
	}
	c.out << text;
	return kExitOk;
}

// Hidden helper the completion scripts call: names, one per line.
int cmd_complete_helper(const Ctx &c, const std::vector<std::string> &args) {
	if (args.size() == 1 && args[0] == "instances") {
		for (const Instance &i : list_instances(c.layout)) {
			c.out << i.name << "\n";
		}
		return kExitOk;
	}
	if (args.size() == 1 && args[0] == "versions") {
		for (const Entry &e : list_entries(c.layout)) {
			c.out << e.name << "\n";
		}
		return kExitOk;
	}
	return kExitUsage;
}

const std::map<std::string, Command> &commands() {
	static const std::map<std::string, Command> table = {
		{ "install",
				{ "install [<version>] [--build debug] [--force]",
						"download, verify and install a release", cmd_install } },
		{ "update", { "update", "install the latest release", cmd_update } },
		{ "prune", { "prune [--keep N]", "remove old, non-default versions", cmd_prune } },
		{ "doctor", { "doctor", "check paths, versions and configuration", cmd_doctor } },
		{ "list",
				{ "list [--remote] [--json]", "show installed versions and links (* = default)",
						[](const Ctx &c, const std::vector<std::string> &a) { return cmd_list(c, a); } } },
		{ "use", { "use <version>", "set the default version", cmd_use } },
		{ "which",
				{ "which [client|server] [--version <v>] [--json]", "print a binary's path",
						[](const Ctx &c, const std::vector<std::string> &a) {
							return cmd_which(c, a);
						} } },
		{ "uninstall",
				{ "uninstall <version> [--force]", "remove an installed version (or a link)",
						[](const Ctx &c, const std::vector<std::string> &a) {
							return cmd_uninstall(c, a);
						} } },
		{ "link",
				{ "link <name> <build-dir>", "register a local build as a pseudo-version",
						cmd_link } },
		{ "unlink",
				{ "unlink <name> [--force]", "remove a link",
						[](const Ctx &c, const std::vector<std::string> &a) {
							return cmd_unlink(c, a);
						} } },
		{ "launch",
				{ "launch [--version <v>] [--connect host[:port]] [-- client args...]", "start the client",
						cmd_launch } },
		{ "host",
				{ "host [--version v] [--port n] [--pack dir] [--watch] [-- server args...]",
						"run a server in the foreground (Ctrl+C stops it)",
						[](const Ctx &c, const std::vector<std::string> &a) { return cmd_host(c, a); } } },
		{ "server",
				{ "server <new|list|start|stop|restart|status|logs|config|service|rm> ...",
						"manage named, background server instances", cmd_server } },
		{ "self",
				{ "self update [--check] [--force]", "update vb itself to the latest release",
						[](const Ctx &c, const std::vector<std::string> &a) { return cmd_self(c, a); } } },
		{ "shim",
				{ "shim <install|remove>", "put voxel_browser / voxel_browser_server launchers in <data>/bin",
						cmd_shim } },
		{ "completions",
				{ "completions <bash|zsh|fish|powershell>", "print a shell completion script",
						cmd_completions } },
		{ "paths",
				{ "paths [--json]", "print the data/config/cache directories",
						[](const Ctx &c, const std::vector<std::string> &a) { return cmd_paths(c, a); } } },
	};
	return table;
}

void print_help(std::ostream &out) {
	out << "vb -- manage and run Voxel Browser (user-scoped, no admin rights)\n\n"
		   "usage: vb <command> [args]\n\ncommands:\n";
	for (const auto &[name, cmd] : commands()) {
		out << "  " << cmd.usage;
		const std::size_t pad = std::string(cmd.usage).size();
		out << std::string(pad < 44 ? 44 - pad : 2, ' ') << cmd.summary << "\n";
	}
	out << "\noptions:\n  -h, --help     show this help\n  -V, --version  print the vb build\n"
		   "\nenvironment:\n  VB_HOME=<dir>    keep everything under <dir> (portable/CI)\n"
		   "  VB_SOURCE=<src>  release source: owner/repo[@base-url] or dir:/path (default: the project's GitHub)\n";
}

} // namespace

int run_cli(const std::vector<std::string> &args, const Layout &layout, std::ostream &out,
		std::ostream &err) {
	const Ctx ctx{ layout, out, err };
	if (args.empty()) {
		print_help(err);
		return kExitUsage;
	}
	const std::string &first = args.front();
	if (first == "-h" || first == "--help" || first == "help") {
		print_help(out);
		return kExitOk;
	}
	if (first == "-V" || first == "--version") {
		out << vb::core::describe_build() << "\n";
		return kExitOk;
	}
	if (first == "__complete") { // hidden: used by the generated completion scripts
		return cmd_complete_helper(ctx, std::vector<std::string>(args.begin() + 1, args.end()));
	}
	const auto it = commands().find(first);
	if (it == commands().end()) {
		return usage_error(ctx, "unknown command '" + first + "'");
	}
	return it->second.run(ctx, std::vector<std::string>(args.begin() + 1, args.end()));
}

} // namespace vb::cli
