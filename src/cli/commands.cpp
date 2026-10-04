#include "vb/cli/commands.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <thread>
#if defined(_WIN32)
#include <io.h>
#define VB_ISATTY _isatty
#define VB_STDOUT_FD 1
#else
#include <unistd.h>
#define VB_ISATTY isatty
#define VB_STDOUT_FD STDOUT_FILENO
#endif

#include "vb/cli/installer.hpp"
#include "vb/cli/instance.hpp"
#include "vb/cli/process.hpp"
#include "vb/cli/release_manifest.hpp"
#include "vb/cli/source.hpp"
#include "vb/cli/store.hpp"
#include "vb/cli/version.hpp"
#include "vb/core/build_info.hpp"
#include "vb/core/config.hpp"

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

int cmd_paths(const Ctx &c, const std::vector<std::string> &args) {
	if (!args.empty()) {
		return usage_error(c, "`paths` takes no arguments");
	}
	c.out << "data:   " << c.layout.data().string() << "\n"
		  << "config: " << c.layout.config().string() << "\n"
		  << "cache:  " << c.layout.cache().string() << "\n";
	return kExitOk;
}

int cmd_list(const Ctx &c, const std::vector<std::string> &args) {
	if (!args.empty()) {
		return usage_error(c, "`list` takes no arguments yet (`--remote` arrives with 8.5)");
	}
	const auto entries = list_entries(c.layout);
	if (entries.empty()) {
		c.out << "nothing installed. Register a local build with "
				 "`vb link dev <build-dir>`.\n";
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
	std::string version;
	if (!take_version_flag(args, version)) {
		return usage_error(c, "--version needs a value");
	}
	Binary which = Binary::Client;
	if (args.size() > 1) {
		return usage_error(c, "usage: vb which [client|server] [--version <v>]");
	}
	if (args.size() == 1) {
		if (args[0] == "server") {
			which = Binary::Server;
		} else if (args[0] != "client") {
			return usage_error(c, "usage: vb which [client|server] [--version <v>]");
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
	if (!opts.empty()) {
		return usage_error(c, "unexpected argument '" + opts[0] +
				"' (pass client arguments after `--`)");
	}
	std::string why;
	const auto e = resolve_entry(c.layout, version, &why);
	if (!e) {
		return failure(c, why);
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

int cmd_host(const Ctx &c, const std::vector<std::string> &raw) {
	auto [rest, extra] = split_passthrough(raw);
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

void print_instance_table(const Ctx &c, const std::vector<Instance> &instances) {
	std::size_t name_w = 4;
	std::size_t ver_w = 7;
	for (const Instance &i : instances) {
		name_w = std::max(name_w, i.name.size());
		ver_w = std::max(ver_w, i.version.size());
	}
	c.out << std::left << std::setw(static_cast<int>(name_w) + 2) << "NAME"
		  << std::setw(static_cast<int>(ver_w) + 2) << "VERSION" << std::setw(8) << "PORT"
		  << "STATE\n";
	const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch())
									 .count();
	for (const Instance &i : instances) {
		const auto rec = running_record(i);
		c.out << std::left << std::setw(static_cast<int>(name_w) + 2) << i.name
			  << std::setw(static_cast<int>(ver_w) + 2) << i.version << std::setw(8)
			  << configured_port(i);
		if (rec) {
			c.out << "running (pid " << rec->pid << ", up " << format_uptime(now - rec->started_unix)
				  << ")\n";
		} else {
			c.out << "stopped\n";
		}
	}
}

int server_list(const Ctx &c, const std::vector<std::string> &args) {
	if (!args.empty()) {
		return usage_error(c, "usage: vb server list");
	}
	const auto instances = list_instances(c.layout);
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

int server_status(const Ctx &c, const std::vector<std::string> &args) {
	const Opts o = parse_opts(args, {}, {});
	if (!o.error.empty() || o.positional.size() > 1) {
		return usage_error(c, o.error.empty() ? "usage: vb server status [<name>]" : o.error);
	}
	if (o.positional.empty()) {
		return server_list(c, {});
	}
	std::string why;
	const auto inst = load_instance(c.layout, o.positional[0], &why);
	if (!inst) {
		return failure(c, why);
	}
	const auto rec = running_record(*inst);
	const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch())
									 .count();
	c.out << "name:    " << inst->name << "\n"
		  << "state:   ";
	if (rec) {
		c.out << "running (pid " << rec->pid << ", up " << format_uptime(now - rec->started_unix)
			  << ")\n";
	} else {
		c.out << "stopped\n";
	}
	c.out << "version: " << inst->version << (rec ? " (running " + rec->version + ")" : "") << "\n"
		  << "pack:    " << inst->pack << "\n"
		  << "port:    " << configured_port(*inst) << "\n"
		  << "dir:     " << inst->dir.string() << "\n"
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

int cmd_server(const Ctx &c, const std::vector<std::string> &args) {
	static const std::map<std::string, std::function<int(const Ctx &, const std::vector<std::string> &)>>
			sub = {
				{ "new", server_new },
				{ "list", server_list },
				{ "start", server_start },
				{ "stop", server_stop },
				{ "restart", server_restart },
				{ "status", server_status },
				{ "logs", server_logs },
				{ "rm", server_rm },
			};
	if (args.empty()) {
		return usage_error(c, "usage: vb server <new|list|start|stop|restart|status|logs|rm> ...");
	}
	const auto it = sub.find(args[0]);
	if (it == sub.end()) {
		return usage_error(c, "unknown server command '" + args[0] + "'");
	}
	return it->second(c, std::vector<std::string>(args.begin() + 1, args.end()));
}

const std::map<std::string, Command> &commands() {
	static const std::map<std::string, Command> table = {
		{ "install",
				{ "install [<version>] [--build debug] [--force]",
						"download, verify and install a release", cmd_install } },
		{ "update", { "update", "install the latest release", cmd_update } },
		{ "prune", { "prune [--keep N]", "remove old, non-default versions", cmd_prune } },
		{ "doctor", { "doctor", "check paths, versions and configuration", cmd_doctor } },
		{ "list", { "list", "show installed versions and links (* = default)", cmd_list } },
		{ "use", { "use <version>", "set the default version", cmd_use } },
		{ "which",
				{ "which [client|server] [--version <v>]", "print a binary's path",
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
				{ "launch [--version <v>] [-- client args...]", "start the client",
						cmd_launch } },
		{ "host",
				{ "host [--version v] [--port n] [--pack dir] [-- server args...]",
						"run a server in the foreground (Ctrl+C stops it)", cmd_host } },
		{ "server",
				{ "server <new|list|start|stop|restart|status|logs|rm> ...",
						"manage named, background server instances", cmd_server } },
		{ "paths", { "paths", "print the data/config/cache directories", cmd_paths } },
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
	const auto it = commands().find(first);
	if (it == commands().end()) {
		return usage_error(ctx, "unknown command '" + first + "'");
	}
	return it->second.run(ctx, std::vector<std::string>(args.begin() + 1, args.end()));
}

} // namespace vb::cli
