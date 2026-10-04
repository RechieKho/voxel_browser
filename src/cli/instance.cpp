#include "vb/cli/instance.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <ostream>
#include <sstream>
#include <system_error>
#include <thread>

#include <toml++/toml.hpp>

#include "vb/cli/lock.hpp"
#include "vb/cli/portcheck.hpp"
#include "vb/cli/version.hpp"
#include "vb/core/config.hpp"

namespace vb::cli {

namespace fs = std::filesystem;

namespace {

using std::chrono::steady_clock;

fs::path run_file(const Instance &i) { return instance_run_dir(i) / "server.pid"; }
fs::path stop_file(const Instance &i) { return instance_run_dir(i) / "stop"; }

bool is_name_char(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '.' || c == '_' || c == '-';
}

bool valid_builtin_pack_name(const std::string &n) {
	return !n.empty() && n.size() <= 64 &&
			std::all_of(n.begin(), n.end(), [](char c) { return is_name_char(c) && c != '.'; });
}

// Release tags are stored canonically so "0.2.0" and "v0.2.0" mean the same.
std::string canonical_version(const std::string &v) {
	if (const auto parsed = parse_version(v)) {
		return to_tag(*parsed);
	}
	return v;
}

void remove_run_files(const Instance &i) {
	std::error_code ec;
	fs::remove(run_file(i), ec);
	fs::remove(stop_file(i), ec);
	fs::remove(instance_status_file(i), ec);
}

Status write_run_record(const Instance &inst, const RunRecord &r) {
	std::ostringstream os;
	os << "pid = " << r.pid << "\n"
	   << "start_token = " << r.start_token << "\n"
	   << "started = " << r.started_unix << "\n"
	   << "version = " << toml_quote(r.version) << "\n";
	return write_text_atomic(run_file(inst), os.str());
}

std::optional<RunRecord> read_run_record(const Instance &inst) {
	try {
		const toml::table t = toml::parse_file(run_file(inst).string());
		RunRecord r;
		const auto pid = t["pid"].value<std::int64_t>();
		const auto token = t["start_token"].value<std::int64_t>();
		if (!pid || !token || *pid <= 0) {
			return std::nullopt;
		}
		r.pid = static_cast<Pid>(*pid);
		r.start_token = static_cast<std::uint64_t>(*token);
		r.started_unix = t["started"].value_or<std::int64_t>(0);
		r.version = t["version"].value_or<std::string>("");
		return r;
	} catch (const toml::parse_error &) {
		return std::nullopt;
	}
}

// Polls until `done()` or `seconds` pass; true when done.
template <typename Fn>
bool wait_until(Fn done, double seconds) {
	const auto deadline = steady_clock::now() + std::chrono::duration<double>(seconds);
	while (true) {
		if (done()) {
			return true;
		}
		if (steady_clock::now() >= deadline) {
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

bool process_gone(const RunRecord &r) {
	const auto token = process_start_token(r.pid);
	return !token || *token != r.start_token;
}

std::int64_t now_unix() {
	return std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count();
}

} // namespace

fs::path instance_toml(const Instance &i) { return i.dir / "instance.toml"; }
fs::path instance_server_toml(const Instance &i) { return i.dir / "server.toml"; }
fs::path instance_world_dir(const Instance &i) { return i.dir / "world"; }
fs::path instance_log_file(const Instance &i) { return i.dir / "logs" / "server.log"; }
fs::path instance_run_dir(const Instance &i) { return i.dir / "run"; }
fs::path instance_status_file(const Instance &i) { return instance_run_dir(i) / "status.toml"; }

std::string validate_instance_name(const std::string &name) {
	if (name.empty() || name.size() > 64) {
		return "instance name must be 1-64 characters";
	}
	if (!std::all_of(name.begin(), name.end(), is_name_char) || name.front() == '.' ||
			name.front() == '-') {
		return "instance name '" + name +
				"' may only contain letters, digits, '.', '_' and '-' (not starting with '.' or '-')";
	}
	return {};
}

Status create_instance(const Layout &layout, const std::string &name,
		const std::string &version, const std::string &pack, std::optional<std::uint16_t> port) {
	if (const std::string e = validate_instance_name(name); !e.empty()) {
		return { e };
	}
	if (pack.rfind("builtin:", 0) == 0) {
		if (!valid_builtin_pack_name(pack.substr(8))) {
			return { "bad pack '" + pack + "' (expected builtin:<name> or a directory)" };
		}
	} else if (!fs::path(pack).is_absolute()) {
		return { "pack must be builtin:<name> or an absolute directory" };
	}
	Instance inst;
	inst.name = name;
	inst.dir = layout.servers_dir() / name;
	inst.version = version.empty() ? "default" : canonical_version(version);
	inst.pack = pack;
	std::error_code ec;
	if (fs::exists(instance_toml(inst), ec)) {
		return { "server '" + name + "' already exists" };
	}
	fs::create_directories(inst.dir, ec);
	if (ec) {
		return { "cannot create " + inst.dir.string() + ": " + ec.message() };
	}
	std::ostringstream it;
	it << "# vb server instance. `version` is a release tag, a link name or \"default\"\n"
	   << "# (follow `vb use`); `pack` is builtin:<name> or an absolute directory.\n"
	   << "version = " << toml_quote(inst.version) << "\n"
	   << "pack = " << toml_quote(inst.pack) << "\n";
	if (const Status s = write_text_atomic(instance_toml(inst), it.str()); !s) {
		return s;
	}
	if (!fs::exists(instance_server_toml(inst), ec)) { // adopting a kept world keeps its config
		std::ostringstream st;
		st << "# voxel_browser_server configuration for this instance (see server.toml.example\n"
		   << "# in the installed version for every key). `vb` supplies content_pack itself.\n";
		if (port) {
			st << "port = " << *port << "\n";
		}
		return write_text_atomic(instance_server_toml(inst), st.str());
	}
	return {};
}

std::optional<Instance> load_instance(const Layout &layout, const std::string &name,
		std::string *why) {
	const auto fail = [&](std::string m) -> std::optional<Instance> {
		if (why != nullptr) {
			*why = std::move(m);
		}
		return std::nullopt;
	};
	if (const std::string e = validate_instance_name(name); !e.empty()) {
		return fail(e);
	}
	Instance inst;
	inst.name = name;
	inst.dir = layout.servers_dir() / name;
	try {
		const toml::table t = toml::parse_file(instance_toml(inst).string());
		inst.version = t["version"].value_or<std::string>("default");
		inst.pack = t["pack"].value_or<std::string>("builtin:base");
	} catch (const toml::parse_error &e) {
		std::error_code ec;
		if (!fs::exists(instance_toml(inst), ec)) {
			return fail("no server named '" + name + "' (see `vb server list`)");
		}
		return fail("cannot read " + instance_toml(inst).string() + ": " +
				std::string(e.description()));
	}
	return inst;
}

std::vector<Instance> list_instances(const Layout &layout) {
	std::vector<Instance> out;
	std::error_code ec;
	for (fs::directory_iterator it(layout.servers_dir(), ec), end; !ec && it != end;
			it.increment(ec)) {
		if (!it->is_directory(ec)) {
			continue;
		}
		if (const auto inst = load_instance(layout, it->path().filename().string())) {
			out.push_back(*inst);
		}
	}
	std::sort(out.begin(), out.end(),
			[](const Instance &a, const Instance &b) { return a.name < b.name; });
	return out;
}

Status remove_instance(const Layout &layout, const std::string &name, bool keep_world) {
	std::string why;
	const auto inst = load_instance(layout, name, &why);
	if (!inst) {
		return { why };
	}
	if (const auto rec = running_record(*inst)) {
		return { "server '" + name + "' is running (pid " + std::to_string(rec->pid) +
			"); stop it first" };
	}
	std::error_code ec;
	if (!keep_world) {
		fs::remove_all(inst->dir, ec);
		return ec ? Status{ "cannot remove " + inst->dir.string() + ": " + ec.message() } : Status{};
	}
	for (fs::directory_iterator it(inst->dir, ec), end; !ec && it != end; it.increment(ec)) {
		if (it->path().filename() == "world") {
			continue;
		}
		std::error_code rec;
		fs::remove_all(it->path(), rec);
		if (rec) {
			return { "cannot remove " + it->path().string() + ": " + rec.message() };
		}
	}
	return {};
}

std::optional<fs::path> resolve_pack(const std::string &pack, const fs::path &entry_root,
		std::string *why) {
	const auto fail = [&](std::string m) -> std::optional<fs::path> {
		if (why != nullptr) {
			*why = std::move(m);
		}
		return std::nullopt;
	};
	std::error_code ec;
	std::vector<fs::path> candidates;
	if (pack.rfind("builtin:", 0) == 0) {
		const std::string n = pack.substr(8);
		if (!valid_builtin_pack_name(n)) {
			return fail("bad pack '" + pack + "'");
		}
		candidates.push_back(entry_root / "content" / n);
		candidates.push_back(entry_root.parent_path() / "content" / n);
	} else {
		candidates.push_back(fs::path(pack));
	}
	for (const fs::path &c : candidates) {
		if (fs::is_regular_file(c / "pack.toml", ec)) {
			return fs::absolute(c, ec).lexically_normal();
		}
	}
	return fail("content pack '" + pack + "' not found (looked for " +
			(candidates.front() / "pack.toml").string() + ")");
}

std::optional<RunRecord> running_record(const Instance &inst) {
	const auto rec = read_run_record(inst);
	if (rec && !process_gone(*rec)) {
		return rec;
	}
	remove_run_files(inst); // stale (crash, reboot, pid reuse) or unreadable
	return std::nullopt;
}

Status plan_launch(const Layout &layout, const Instance &inst, const Overrides &ov,
		LaunchPlan &out) {
	const std::string spec = ov.version.empty() ? inst.version : ov.version;
	std::string why;
	const auto entry = resolve_entry(layout, spec, &why);
	if (!entry) {
		if (parse_version(spec)) {
			why += "; install it with `vb install " + canonical_version(spec) + "`";
		}
		return { why };
	}
	const auto exe = find_binary(*entry, Binary::Server);
	if (!exe) {
		return { entry->name + " has no " + binary_file_name(Binary::Server) + " in " +
			entry->root.string() };
	}
	const auto pack = resolve_pack(ov.pack.empty() ? inst.pack : ov.pack, entry->root, &why);
	if (!pack) {
		return { why };
	}
	// The engine's own loader: a typo in server.toml fails here, loudly, instead
	// of in the log of a process nobody is watching.
	const fs::path config = instance_server_toml(inst);
	auto loaded = vb::core::load_server_config(config.string());
	if (!loaded) {
		return { config.string() + ": " + std::string(vb::core::message(loaded.error())) };
	}
	out.exe = *exe;
	out.pack = *pack;
	out.cwd = inst.dir;
	out.version_name = entry->name;
	out.port = ov.port.value_or(loaded->port);
	out.args = { "--config", config.string(), "--content-pack", pack->string() };
	if (ov.port) {
		out.args.push_back("--port");
		out.args.push_back(std::to_string(*ov.port));
	}
	out.args.push_back("--stop-file");
	out.args.push_back(stop_file(inst).string());
	out.args.push_back("--status-file");
	out.args.push_back(instance_status_file(inst).string());
	out.args.insert(out.args.end(), ov.extra_args.begin(), ov.extra_args.end());
	return {};
}

std::string tail_log(const Instance &inst, int lines) {
	std::ifstream f(instance_log_file(inst), std::ios::binary | std::ios::ate);
	if (!f || lines <= 0) {
		return {};
	}
	// The last 256 KiB is plenty for any sane number of lines.
	const std::streamoff size = f.tellg();
	const std::streamoff want = std::min<std::streamoff>(size, 256 * 1024);
	f.seekg(size - want);
	std::string buf(static_cast<std::size_t>(want), '\0');
	f.read(buf.data(), want);
	std::size_t pos = buf.size();
	if (pos > 0 && buf[pos - 1] == '\n') {
		--pos;
	}
	for (int seen = 0; pos > 0; --pos) {
		if (buf[pos - 1] == '\n' && ++seen == lines) {
			break;
		}
	}
	std::string out = buf.substr(pos);
	if (!out.empty() && out.back() != '\n') {
		out += '\n';
	}
	return out;
}

StartResult start_instance(const Layout &layout, const Instance &inst, const Overrides &ov,
		bool foreground, std::ostream &out) {
	const auto fail = [](std::string m) { return StartResult{ { std::move(m) }, 1 }; };

	std::error_code ec;
	fs::create_directories(instance_run_dir(inst), ec);
	// Serialises concurrent `start`s of one instance (check-then-spawn).
	std::unique_ptr<FileLock> start_lock;
	if (const Status s = FileLock::acquire(instance_run_dir(inst) / "start.lock", false, start_lock);
			!s) {
		return fail("server '" + inst.name + "' is being started by another vb command");
	}
	if (const auto rec = running_record(inst)) {
		return fail("server '" + inst.name + "' is already running (pid " +
				std::to_string(rec->pid) + ")");
	}
	LaunchPlan plan;
	if (const Status s = plan_launch(layout, inst, ov, plan); !s) {
		return fail(s.error);
	}
	if (!udp_port_available(plan.port)) {
		return fail("UDP port " + std::to_string(plan.port) +
				" is already in use (another server? pick one with --port)");
	}
	fs::remove(stop_file(inst), ec); // a stale stop request would end the new run at once

	const auto record_for = [&](Pid pid) {
		RunRecord r;
		r.pid = pid;
		r.start_token = process_start_token(pid).value_or(0);
		r.started_unix = now_unix();
		r.version = plan.version_name;
		return r;
	};

	if (foreground) {
		out << "hosting '" << inst.name << "' (" << plan.version_name << ") on UDP port "
			<< plan.port << " -- Ctrl+C to stop\n"
			<< std::flush;
		Status record_status;
		const RunResult r = run_foreground(plan.exe, plan.args, plan.cwd, [&](Pid pid) {
			record_status = write_run_record(inst, record_for(pid));
			start_lock.reset(); // the record now answers "is it running?"
		});
		remove_run_files(inst);
		if (!r.error.empty()) {
			return fail(r.error);
		}
		return { record_status, r.exit_code };
	}

	rotate_log(inst);
	const SpawnResult sp = spawn_detached(plan.exe, plan.args, plan.cwd, instance_log_file(inst));
	if (!sp.error.empty()) {
		return fail(sp.error);
	}
	if (const Status s = write_run_record(inst, record_for(sp.pid)); !s) {
		force_kill(sp.pid); // never leave a server we can't stop again
		return fail(s.error);
	}
	// A bad pack / unreadable world makes the server exit within moments; say so
	// now rather than reporting success for a process that is already gone.
	const RunRecord rec = *read_run_record(inst);
	if (wait_until([&] { return process_gone(rec); }, 1.5)) {
		remove_run_files(inst);
		std::string msg = "server '" + inst.name + "' exited right after starting";
		if (const std::string tail = tail_log(inst, 15); !tail.empty()) {
			msg += "; last log lines:\n" + tail;
		} else {
			msg += " (log: " + instance_log_file(inst).string() + ")";
		}
		return fail(msg);
	}
	out << "started '" << inst.name << "' (" << plan.version_name << ", pid " << sp.pid
		<< ") on UDP port " << plan.port << "\nlog: " << instance_log_file(inst).string() << "\n";
	return { {}, 0 };
}

Status stop_instance(const Instance &inst, int timeout_seconds, bool kill, std::ostream &out) {
	const auto rec = running_record(inst);
	if (!rec) {
		out << "'" << inst.name << "' is not running\n";
		return {};
	}
	std::error_code ec;
	fs::create_directories(instance_run_dir(inst), ec);
	std::ofstream(stop_file(inst)) << "stop\n"; // the Windows path; harmless duplicate on POSIX
	request_stop(rec->pid);
	out << "stopping '" << inst.name << "' (pid " << rec->pid << ")..." << std::flush;
	if (!wait_until([&] { return process_gone(*rec); }, std::max(timeout_seconds, 0))) {
		if (!kill) {
			out << "\n";
			return { "'" + inst.name + "' did not stop within " + std::to_string(timeout_seconds) +
				" s (still saving?); wait longer with --timeout, or force it with --kill" };
		}
		out << " not responding, killing" << std::flush;
		force_kill(rec->pid);
		if (!wait_until([&] { return process_gone(*rec); }, 10)) {
			out << "\n";
			return { "could not kill pid " + std::to_string(rec->pid) };
		}
	}
	remove_run_files(inst);
	out << " stopped\n";
	return {};
}

std::map<std::string, std::vector<std::string>> instance_version_users(const Layout &layout) {
	std::map<std::string, std::vector<std::string>> users;
	const auto add = [&](const std::string &version, const std::string &who) {
		auto &v = users[version];
		if (std::find(v.begin(), v.end(), who) == v.end()) {
			v.push_back(who);
		}
	};
	for (const Instance &inst : list_instances(layout)) {
		if (inst.version != "default") {
			add(canonical_version(inst.version), inst.name);
		}
		if (const auto rec = running_record(inst); rec && !rec->version.empty()) {
			add(rec->version, inst.name);
		}
	}
	return users;
}

std::optional<ServerStatus> read_server_status(const Instance &inst) {
	try {
		const toml::table t = toml::parse_file(instance_status_file(inst).string());
		ServerStatus st;
		st.running = t["running"].value_or(false);
		st.updated_unix = t["updated"].value_or<std::int64_t>(0);
		st.uptime_seconds = t["uptime_seconds"].value_or<std::int64_t>(0);
		st.tick = t["tick"].value_or<std::int64_t>(0);
		st.target_tick_rate = static_cast<int>(t["target_tick_rate"].value_or<std::int64_t>(0));
		st.tick_rate = t["tick_rate"].value_or(0.0);
		st.max_players = static_cast<int>(t["max_players"].value_or<std::int64_t>(0));
		st.seed = t["seed"].value_or<std::string>("");
		st.motd = t["motd"].value_or<std::string>("");
		if (const toml::array *players = t["players"].as_array()) {
			for (const toml::node &n : *players) {
				if (const auto name = n.value<std::string>()) {
					st.players.push_back(*name);
				}
			}
		}
		return st;
	} catch (const toml::parse_error &) {
		return std::nullopt;
	}
}

bool status_is_fresh(const ServerStatus &s, std::int64_t now_unix, std::int64_t max_age_seconds) {
	return s.running && now_unix - s.updated_unix <= max_age_seconds;
}

void rotate_log(const Instance &inst, std::uintmax_t max_bytes, int keep) {
	std::error_code ec;
	const fs::path log = instance_log_file(inst);
	const auto size = fs::file_size(log, ec);
	if (ec || size <= max_bytes || keep < 1) {
		return;
	}
	const auto numbered = [&](int n) {
		fs::path p = log;
		p += "." + std::to_string(n);
		return p;
	};
	fs::remove(numbered(keep), ec);
	for (int n = keep - 1; n >= 1; --n) {
		fs::rename(numbered(n), numbered(n + 1), ec); // a missing .n is fine
	}
	fs::rename(log, numbered(1), ec);
}

} // namespace vb::cli
